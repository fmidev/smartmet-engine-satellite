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
#include <spine/Convenience.h>
#include <sys/stat.h>
#include <algorithm>
#include <functional>
#include <iostream>
#include <iterator>
#include <optional>
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

void Scanner::start(const std::map<ProductKey, Product>& theProducts,
                    int theMaxThreads,
                    int theMinFileAge)
{
  try
  {
    const std::size_t max_threads = std::max(1, theMaxThreads);
    itsMinFileAge = std::max(0, theMinFileAge);

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
        std::cerr << Spine::log_time_str()
                  << fmt::format(
                         " Warning: satellite directory '{}' does not exist, its {} "
                         "product(s) will be empty until it appears\n",
                         path.string(),
                         directory.watches.size());

      itsDirectories.push_back(std::move(directory));
    }

    if (itsDirectories.empty())
    {
      itsReady = true;
      return;
    }

    // The first scan. The engine must not report itself ready before it
    // is complete, otherwise the first requests would find no images.
    // The directories are listed first, in parallel, and the reads they
    // call for are collected. Then the reads are done with max_threads
    // threads from one queue, newest images first, so that the biggest
    // directory does not hold the others back and every product gets
    // its latest image early. Each read is a GDAL open over NFS, so the
    // wait is for the network rather than the CPU.

    const auto started = std::chrono::steady_clock::now();

    const auto run_workers = [&](std::size_t count, const std::function<void(std::size_t)>& work)
    {
      std::vector<std::thread> workers;
      for (std::size_t i = 0; i < count; i++)
        workers.emplace_back(
            [&work, i]()
            {
              Fmi::set_thread_name("sat-scan");
              work(i);
            });
      for (auto& worker : workers)
        worker.join();
    };

    // Phase 1: list

    std::vector<std::vector<Job>> listed(itsDirectories.size());
    {
      std::atomic<std::size_t> next{0};
      run_workers(std::min(itsDirectories.size(), max_threads),
                  [&](std::size_t)
                  {
                    for (;;)
                    {
                      const auto index = next++;
                      if (index >= itsDirectories.size() || itsShutdownRequested)
                        return;
                      scan(itsDirectories[index], &listed[index]);
                    }
                  });
    }

    std::vector<Job> jobs;
    for (auto& part : listed)
      jobs.insert(
          jobs.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
    listed.clear();

    std::stable_sort(jobs.begin(),
                     jobs.end(),
                     [](const Job& a, const Job& b)
                     { return a.candidate.first > b.candidate.first; });

    // Phase 2: read

    {
      std::atomic<std::size_t> next{0};
      run_workers(std::min(jobs.size(), max_threads),
                  [&](std::size_t)
                  {
                    for (;;)
                    {
                      const auto index = next++;
                      if (index >= jobs.size() || itsShutdownRequested)
                        return;
                      auto& job = jobs[index];
                      job.ok = readOne(job.directory->watches[job.watch], job.candidate);
                    }
                  });
    }

    for (const auto& job : jobs)
      done(job);

    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started);
    std::cout << Spine::log_time_str()
              << fmt::format(
                     " Satellite engine: first scan of {} directories read {} images "
                     "with {} threads in {:.1f} seconds\n",
                     itsDirectories.size(),
                     itsImagesRead.load(),
                     std::min(jobs.size(), max_threads),
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

void Scanner::scan(Directory& theDirectory, std::vector<Job>* theJobs)
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

    // Unless nothing has arrived or left
    if (mtime != theDirectory.mtime || !theDirectory.settled)
      list(theDirectory, mtime);

    if (!theDirectory.waiting.empty())
      examine(theDirectory, theJobs);
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
 * matches of the previous listing. New files are left waiting for
 * examine() to decide when they are ready to be read.
 */
// ----------------------------------------------------------------------

void Scanner::list(Directory& theDirectory, std::time_t theTime)
{
  ++itsListings;
  const std::time_t listed_at = std::time(nullptr);

  std::map<std::string, File> names;

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
      names.insert(std::move(*known));
      continue;
    }

    // A new file. Not every name matches a configured product, one
    // directory may hold composites nobody has asked for.

    File file;
    for (std::size_t i = 0; i < theDirectory.watches.size(); i++)
      if (boost::regex_match(name, theDirectory.watches[i].regex))
        file.matches.push_back(i);

    if (file.matches.empty())
      continue;

    if (parseTime(name).is_not_a_date_time())
      continue;  // Not a satellite product file name

    file.unread = file.matches;
    theDirectory.waiting.insert(name);
    names.emplace(name, std::move(file));
  }

  // Deleted files. The moved-from entries of the known names are still
  // in the old map, hence the lookup is in the new one.

  for (const auto& [name, file] : theDirectory.names)
  {
    if (names.count(name) > 0)
      continue;
    const auto path = (theDirectory.path / name).string();
    for (auto i : file.matches)
      itsRepository.remove(theDirectory.watches[i].key, path);
    theDirectory.waiting.erase(name);
  }

  theDirectory.names = std::move(names);
  theDirectory.mtime = theTime;

  // The file times have a one second resolution, hence a file may still
  // arrive within the same second without changing the directory time
  theDirectory.settled = (listed_at - theTime >= 2);
}

// ----------------------------------------------------------------------
/*!
 * \brief Read the waiting files of a directory which are ready
 *
 * A file is ready when it has not been modified for min_file_age
 * seconds, and if a read of it has failed before, when it has changed
 * since. The directory is looked at again as soon as the youngest
 * waiting file becomes old enough, not only at the next regular tick.
 *
 * Only the newest max_files images stay in the repository, so reading
 * the metadata of the older ones would be work thrown away at once. The
 * first scan is where this matters: the production directories hold
 * weeks of history, and every file read is a GDAL open over NFS. The
 * cutoff is the max_files'th newest time of the images already known
 * and the candidates together, which is exactly the set the repository
 * would keep. The files below it are never read.
 */
// ----------------------------------------------------------------------

void Scanner::examine(Directory& theDirectory, std::vector<Job>* theJobs)
{
  // The candidates of each product, known from the names alone

  std::vector<std::vector<Job>> candidates(theDirectory.watches.size());

  for (const auto& name : theDirectory.waiting)
  {
    const auto time = parseTime(name);
    const auto path = theDirectory.path / name;
    for (auto i : theDirectory.names.at(name).unread)
      candidates[i].push_back(Job{&theDirectory, i, Candidate(time, path), 0, -1});
  }

  // The files below the max_files cutoff are done with without being
  // read or even stat'ed. The first scan of a directory holding weeks of
  // history depends on this.

  for (std::size_t i = 0; i < candidates.size(); i++)
  {
    auto& jobs = candidates[i];
    const auto max_files = theDirectory.watches[i].max_files;
    if (jobs.empty() || max_files == 0)
      continue;

    auto times = itsRepository.times(theDirectory.watches[i].key);
    times.reserve(times.size() + jobs.size());
    for (const auto& job : jobs)
      times.push_back(job.candidate.first);

    if (times.size() <= max_files)
      continue;

    std::nth_element(times.begin(), times.begin() + (max_files - 1), times.end(), std::greater<>());
    const auto cutoff = times[max_files - 1];

    const auto below = [&cutoff](const Job& job) { return job.candidate.first < cutoff; };

    for (auto& job : jobs)
    {
      if (below(job))
      {
        job.ok = true;
        done(job);
      }
    }
    jobs.erase(std::remove_if(jobs.begin(), jobs.end(), below), jobs.end());
  }

  // Of the rest, the files which are ready. A file matching several
  // products is stat'ed once.

  const std::time_t now = std::time(nullptr);
  std::optional<std::time_t> due;
  std::map<std::string, std::optional<std::pair<std::uintmax_t, std::time_t>>> ready;

  const auto is_ready = [&](Job& job)
  {
    const auto name = job.candidate.second.filename().string();
    auto pos = ready.find(name);
    if (pos == ready.end())
    {
      std::optional<std::pair<std::uintmax_t, std::time_t>> result;
      struct stat status;
      // A file gone missing is forgotten by the next listing
      if (::stat(job.candidate.second.c_str(), &status) == 0)
      {
        const auto size = static_cast<std::uintmax_t>(status.st_size);
        const auto mtime = status.st_mtime;
        const auto& file = theDirectory.names.at(name);

        // Not if tried already and unchanged since
        const bool retry = !file.failed || size != file.failed_size || mtime != file.failed_mtime;
        if (retry && now - mtime < itsMinFileAge)
          due = std::min(due.value_or(mtime + itsMinFileAge), mtime + itsMinFileAge);
        else if (retry)
          result.emplace(size, mtime);
      }
      pos = ready.emplace(name, result).first;
    }

    if (!pos->second)
      return false;
    job.size = pos->second->first;
    job.mtime = pos->second->second;
    return true;
  };

  for (auto& jobs : candidates)
    jobs.erase(std::remove_if(jobs.begin(), jobs.end(), [&](Job& job) { return !is_ready(job); }),
               jobs.end());

  // Look again as soon as the youngest file is old enough

  if (due)
  {
    const auto wait = std::chrono::seconds(std::max<std::time_t>(1, *due - now));
    theDirectory.next_scan =
        std::min(theDirectory.next_scan, std::chrono::steady_clock::now() + wait);
  }

  for (std::size_t i = 0; i < candidates.size(); i++)
  {
    auto& jobs = candidates[i];

    // Newest first, so that the latest image is available as early as
    // possible while a long scan is still going on

    std::sort(jobs.begin(),
              jobs.end(),
              [](const Job& a, const Job& b) { return a.candidate > b.candidate; });

    if (theJobs != nullptr)
    {
      // The first scan reads later, with many threads. The directory
      // outlives the jobs: the directories are not touched once the scan
      // has begun.
      std::move(jobs.begin(), jobs.end(), std::back_inserter(*theJobs));
      continue;
    }

    for (auto& job : jobs)
    {
      if (itsShutdownRequested)
        return;
      job.ok = readOne(theDirectory.watches[i], job.candidate);
      done(job);
    }
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Record the outcome of reading a file for one product
 */
// ----------------------------------------------------------------------

void Scanner::done(const Job& theJob)
{
  auto& directory = *theJob.directory;
  const auto name = theJob.candidate.second.filename().string();

  auto pos = directory.names.find(name);
  if (pos == directory.names.end())
    return;

  auto& file = pos->second;

  if (!theJob.ok)
  {
    file.failed = true;
    file.failed_size = theJob.size;
    file.failed_mtime = theJob.mtime;
    return;
  }

  file.unread.erase(std::remove(file.unread.begin(), file.unread.end(), theJob.watch),
                    file.unread.end());
  if (file.unread.empty())
  {
    file.failed = false;
    directory.waiting.erase(name);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Forget the images of a directory which has disappeared
 */
// ----------------------------------------------------------------------

void Scanner::forget(Directory& theDirectory)
{
  for (const auto& [name, file] : theDirectory.names)
  {
    const auto path = (theDirectory.path / name).string();
    for (auto i : file.matches)
      itsRepository.remove(theDirectory.watches[i].key, path);
  }

  theDirectory.names.clear();
  theDirectory.waiting.clear();
  theDirectory.mtime = -1;
  theDirectory.settled = false;
}

// ----------------------------------------------------------------------
/*!
 * \brief Read the metadata of one image into the repository
 */
// ----------------------------------------------------------------------

bool Scanner::readOne(const Watch& theWatch, const Candidate& theCandidate)
{
  const auto& [time, path] = theCandidate;

  try
  {
    ++itsImagesRead;

    // The newest known image of the product shares the grid of this one
    // in all but the rarest cases, and lets the CRS be reused
    const auto sibling = itsRepository.find(theWatch.key, {}, Fmi::TimeDuration(0, 0, 0));

    auto info = std::make_shared<ImageInfo>(Gdal::readMetadata(path.string(), time, sibling.get()));
    itsRepository.insert(theWatch.key, info);
    return true;
  }
  catch (const std::exception& e)
  {
    // A single unreadable file must not stop the scan. The file is
    // tried again if it changes.
    std::cerr << Spine::log_time_str()
              << fmt::format(
                     " Warning: satellite engine could not read '{}', will retry if it "
                     "changes: {}\n",
                     path.string(),
                     e.what());
    return false;
  }
}

}  // namespace Satellite
}  // namespace Engine
}  // namespace SmartMet
