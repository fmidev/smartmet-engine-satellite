// ======================================================================
/*!
 * \brief Directory scanner keeping the image catalog up to date
 */
// ======================================================================

#include "Scanner.h"
#include "Gdal.h"
#include <fmt/format.h>
#include <macgyver/Exception.h>
#include <macgyver/StringConversion.h>
#include <macgyver/ThreadName.h>
#include <macgyver/TimeParser.h>
#include <sys/stat.h>
#include <algorithm>
#include <functional>
#include <iostream>
#include <set>

namespace SmartMet
{
namespace Engine
{
namespace Satellite
{
namespace
{
// Modification time of a directory in seconds, -1 if it cannot be read
std::time_t directory_time(const std::filesystem::path& thePath)
{
  struct stat status;
  if (::stat(thePath.c_str(), &status) != 0 || !S_ISDIR(status.st_mode))
    return -1;
  return status.st_mtime;
}

// Threads used for the first scan, which is where the time goes. The
// directories are independent and the wait is for NFS, not the CPU.
const std::size_t first_scan_threads = 8;

}  // namespace

// ----------------------------------------------------------------------

Scanner::Scanner(Repository& theRepository) : itsRepository(theRepository) {}

// ----------------------------------------------------------------------

Scanner::~Scanner()
{
  try
  {
    stop();
  }
  catch (...)
  {
    // Destructors must not throw
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Parse the valid time from the beginning of the file name
 *
 * The satellite products are named YYYYMMDD_HHMM_Platform_area_composite,
 * and the same time is stored in the TIFF metadata. Reading it from the
 * name avoids opening the file just to find out whether it is wanted.
 */
// ----------------------------------------------------------------------

Fmi::DateTime Scanner::parseTime(const std::string& theFileName)
{
  try
  {
    // YYYYMMDD_HHMM
    if (theFileName.size() < 13 || theFileName[8] != '_')
      return {};

    for (std::size_t i = 0; i < 13; i++)
    {
      if (i == 8)
        continue;
      if (std::isdigit(static_cast<unsigned char>(theFileName[i])) == 0)
        return {};
    }

    // Fmi::TimeParser wants YYYYMMDDHHMM
    const auto stamp = theFileName.substr(0, 8) + theFileName.substr(9, 4);
    return Fmi::TimeParser::parse_fmi(stamp);
  }
  catch (...)
  {
    return {};
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Scan the product directories once and start polling them
 */
// ----------------------------------------------------------------------

void Scanner::start(const std::map<ProductKey, Product>& theProducts)
{
  try
  {
    // Group the products by directory

    std::map<std::filesystem::path, Directory> directories;

    for (const auto& [key, product] : theProducts)
    {
      auto& directory = directories[product.directory];
      directory.path = product.directory;
      directory.watches.push_back(Watch{key, product.regex, product.max_files});

      const std::chrono::seconds interval(std::max(1, product.refresh_interval_secs));
      if (directory.interval.count() == 0 || interval < directory.interval)
        directory.interval = interval;
    }

    for (auto& [path, directory] : directories)
    {
      // A missing directory must not prevent the other products from
      // working, and the production has moved directories from one
      // machine to another before, so it is polled like the others and
      // picked up if it appears.
      if (!std::filesystem::is_directory(path))
        std::cerr << fmt::format(
            "Warning: satellite directory '{}' does not exist, its {} product(s) will be empty "
            "until it appears\n",
            path.string(),
            directory.watches.size());

      itsDirectories.push_back(std::move(directory));
    }

    if (itsDirectories.empty())
    {
      itsReady = true;
      return;
    }

    // The first scan, directories in parallel. The engine must not
    // report itself ready before it is complete, otherwise the first
    // requests would find no images.

    const auto started = std::chrono::steady_clock::now();
    {
      std::atomic<std::size_t> next{0};
      std::vector<std::thread> workers;
      const auto count = std::min(itsDirectories.size(), first_scan_threads);
      for (std::size_t i = 0; i < count; i++)
      {
        workers.emplace_back(
            [this, &next]()
            {
              Fmi::set_thread_name("sat-scan");
              for (;;)
              {
                const auto index = next++;
                if (index >= itsDirectories.size() || itsShutdownRequested)
                  return;
                scan(itsDirectories[index]);
              }
            });
      }
      for (auto& worker : workers)
        worker.join();
    }

    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started);
    std::cout << fmt::format(
        "Satellite engine: first scan of {} directories read {} images in {:.1f} seconds\n",
        itsDirectories.size(),
        itsImagesRead.load(),
        elapsed.count());

    itsReady = true;

    itsThread = std::thread(
        [this]()
        {
          Fmi::set_thread_name("sat-monitor");
          run();
        });
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Failed to start the satellite directory scanner");
  }
}

// ----------------------------------------------------------------------

void Scanner::stop()
{
  {
    std::lock_guard<std::mutex> lock(itsMutex);
    itsShutdownRequested = true;
  }
  itsCondition.notify_all();

  if (itsThread.joinable())
    itsThread.join();
}

// ----------------------------------------------------------------------
/*!
 * \brief Poll the directories until asked to stop
 */
// ----------------------------------------------------------------------

void Scanner::run()
{
  std::unique_lock<std::mutex> lock(itsMutex);

  while (!itsShutdownRequested)
  {
    lock.unlock();

    const auto now = std::chrono::steady_clock::now();
    auto wake = now + std::chrono::seconds(60);

    for (auto& directory : itsDirectories)
    {
      if (itsShutdownRequested)
        break;
      if (now >= directory.next_scan)
        scan(directory);
      wake = std::min(wake, directory.next_scan);
    }

    lock.lock();
    itsCondition.wait_until(lock, wake, [this]() { return itsShutdownRequested.load(); });
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief One tick of one directory: a stat, and a listing if needed
 */
// ----------------------------------------------------------------------

void Scanner::scan(Directory& theDirectory)
{
  try
  {
    theDirectory.next_scan = std::chrono::steady_clock::now() + theDirectory.interval;

    const auto mtime = directory_time(theDirectory.path);

    if (mtime < 0)
    {
      // Missing or unreadable. Its images are gone, and it is tried
      // again at the next tick.
      forget(theDirectory);
      return;
    }

    if (mtime == theDirectory.mtime && theDirectory.settled)
      return;  // Nothing has arrived or left

    list(theDirectory, mtime);
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Satellite directory scan failed");
    exception.addParameter("Directory", theDirectory.path.string());
    exception.printError();
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief List a directory and act on what has changed
 *
 * Only the file names are read. Each new name is matched against the
 * patterns of the products sharing the directory, known names keep the
 * matches of the previous listing.
 */
// ----------------------------------------------------------------------

void Scanner::list(Directory& theDirectory, std::time_t theTime)
{
  ++itsListings;
  const std::time_t listed_at = std::time(nullptr);

  std::map<std::string, std::vector<std::size_t>> names;
  std::vector<std::vector<Candidate>> candidates(theDirectory.watches.size());

  std::error_code error;
  for (std::filesystem::directory_iterator it(theDirectory.path, error), end; it != end;
       it.increment(error))
  {
    if (error)
      break;

    const auto name = it->path().filename().string();
    if (name.empty() || name[0] == '.')
      continue;

    auto known = theDirectory.names.find(name);
    if (known != theDirectory.names.end())
    {
      names.insert(*known);
      continue;
    }

    // A new file. Not every name matches a configured product, one
    // directory may hold composites nobody has asked for.

    std::vector<std::size_t> matches;
    for (std::size_t i = 0; i < theDirectory.watches.size(); i++)
      if (boost::regex_match(name, theDirectory.watches[i].regex))
        matches.push_back(i);

    if (matches.empty())
      continue;

    const auto time = parseTime(name);
    if (time.is_not_a_date_time())
      continue;  // Not a satellite product file name

    for (auto i : matches)
      candidates[i].emplace_back(time, it->path());

    names.emplace(name, std::move(matches));
  }

  // Deleted files

  for (const auto& [name, matches] : theDirectory.names)
  {
    if (names.count(name) > 0)
      continue;
    const auto path = (theDirectory.path / name).string();
    for (auto i : matches)
      itsRepository.remove(theDirectory.watches[i].key, path);
  }

  // New files, per product

  for (std::size_t i = 0; i < theDirectory.watches.size(); i++)
  {
    if (itsShutdownRequested)
      return;
    if (!candidates[i].empty())
      read(theDirectory.watches[i], std::move(candidates[i]));
  }

  theDirectory.names = std::move(names);
  theDirectory.mtime = theTime;

  // The file times have a one second resolution, hence a file may still
  // arrive within the same second without changing the directory time
  theDirectory.settled = (listed_at - theTime >= 2);
}

// ----------------------------------------------------------------------
/*!
 * \brief Forget the images of a directory which has disappeared
 */
// ----------------------------------------------------------------------

void Scanner::forget(Directory& theDirectory)
{
  for (const auto& [name, matches] : theDirectory.names)
  {
    const auto path = (theDirectory.path / name).string();
    for (auto i : matches)
      itsRepository.remove(theDirectory.watches[i].key, path);
  }

  theDirectory.names.clear();
  theDirectory.mtime = -1;
  theDirectory.settled = false;
}

// ----------------------------------------------------------------------
/*!
 * \brief Read the metadata of the new images of one product
 *
 * Only the newest max_files images stay in the repository, so reading
 * the metadata of the older ones would be work thrown away at once. The
 * first scan is where this matters: the production directories hold
 * weeks of history, and every file read is a GDAL open over NFS. The
 * cutoff is the max_files'th newest time of the images already known
 * and the candidates together, which is exactly the set the repository
 * would keep.
 */
// ----------------------------------------------------------------------

void Scanner::read(const Watch& theWatch, std::vector<Candidate> theCandidates)
{
  const auto max_files = theWatch.max_files;

  if (max_files > 0)
  {
    auto times = itsRepository.times(theWatch.key);
    times.reserve(times.size() + theCandidates.size());
    for (const auto& candidate : theCandidates)
      times.push_back(candidate.first);

    if (times.size() > max_files)
    {
      std::nth_element(
          times.begin(), times.begin() + (max_files - 1), times.end(), std::greater<>());
      const auto cutoff = times[max_files - 1];

      theCandidates.erase(std::remove_if(theCandidates.begin(),
                                         theCandidates.end(),
                                         [&cutoff](const Candidate& candidate)
                                         { return candidate.first < cutoff; }),
                          theCandidates.end());
    }
  }

  // Newest first, so that the latest image is available as early as
  // possible while a long first scan is still going on

  std::sort(theCandidates.begin(), theCandidates.end(), std::greater<>());

  for (const auto& [time, path] : theCandidates)
  {
    if (itsShutdownRequested)
      return;

    try
    {
      ++itsImagesRead;
      auto info = std::make_shared<ImageInfo>(Gdal::readMetadata(path.string(), time));
      itsRepository.insert(theWatch.key, info);
    }
    catch (const std::exception& e)
    {
      // A single unreadable file must not stop the scan. Incomplete
      // files appear in the directories while they are being written.
      std::cerr << fmt::format(
          "Warning: satellite engine skipped '{}': {}\n", path.string(), e.what());
    }
  }
}

}  // namespace Satellite
}  // namespace Engine
}  // namespace SmartMet
