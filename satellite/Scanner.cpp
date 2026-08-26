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
#include <iostream>

namespace SmartMet
{
namespace Engine
{
namespace Satellite
{
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
 * \brief Start watching the producer directories
 */
// ----------------------------------------------------------------------

void Scanner::start(const std::map<std::string, Producer>& theProducers)
{
  try
  {
    for (const auto& [name, producer] : theProducers)
    {
      if (!std::filesystem::is_directory(producer.directory))
      {
        // A missing directory must not prevent the other producers from
        // working. The producer will simply have no images.
        std::cerr << fmt::format("Warning: satellite producer '{}' directory '{}' does not exist\n",
                                 name,
                                 producer.directory.string());
        continue;
      }

      auto watcher = itsMonitor.watch(
          producer.directory,
          producer.regex,
          [this](Fmi::DirectoryMonitor::Watcher id,
                 const std::filesystem::path& path,
                 const boost::regex& pattern,
                 const Fmi::DirectoryMonitor::Status& status)
          { this->update(id, path, pattern, status); },
          [this](Fmi::DirectoryMonitor::Watcher id,
                 const std::filesystem::path& path,
                 const boost::regex& pattern,
                 const std::string& message) { this->error(id, path, pattern, message); },
          producer.refresh_interval_secs,
          Fmi::DirectoryMonitor::CREATE | Fmi::DirectoryMonitor::DELETE |
              Fmi::DirectoryMonitor::MODIFY);

      itsWatchers.insert({watcher, name});
    }

    if (itsWatchers.empty())
      return;

    itsMonitorThread = boost::thread(
        [this]()
        {
          Fmi::set_thread_name("sat-monitor");
          itsMonitor.run();
        });

    // The engine must not report itself ready before the first scan is
    // complete, otherwise the first requests would find no images
    itsMonitor.wait_until_ready();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Failed to start the satellite directory scanner");
  }
}

// ----------------------------------------------------------------------

void Scanner::stop()
{
  itsShutdownRequested = true;

  if (itsMonitorThread.joinable())
  {
    itsMonitor.stop();
    itsMonitorThread.join();
  }
}

// ----------------------------------------------------------------------

bool Scanner::ready() const
{
  return itsWatchers.empty() || itsMonitor.ready();
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle the changes of one directory
 */
// ----------------------------------------------------------------------

void Scanner::update(Fmi::DirectoryMonitor::Watcher theWatcher,
                     const std::filesystem::path& thePath,
                     const boost::regex& /* thePattern */,
                     const Fmi::DirectoryMonitor::Status& theStatus)
{
  try
  {
    auto pos = itsWatchers.find(theWatcher);
    if (pos == itsWatchers.end())
      return;

    const auto& producer = pos->second;

    for (const auto& [path, change] : *theStatus)
    {
      if (itsShutdownRequested)
        return;

      if ((change & (Fmi::DirectoryMonitor::DELETE | Fmi::DirectoryMonitor::MODIFY)) != 0)
        itsRepository.remove(producer, path.string());

      if ((change & (Fmi::DirectoryMonitor::CREATE | Fmi::DirectoryMonitor::MODIFY)) != 0)
      {
        const auto time = parseTime(path.filename().string());
        if (time.is_not_a_date_time())
          continue;  // Not a satellite product file name

        try
        {
          auto info = std::make_shared<ImageInfo>(Gdal::readMetadata(path.string(), time));
          itsRepository.insert(producer, info);
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
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Satellite directory scan failed");
    exception.addParameter("Directory", thePath.string());
    exception.printError();
  }
}

// ----------------------------------------------------------------------

void Scanner::error(Fmi::DirectoryMonitor::Watcher /* theWatcher */,
                    const std::filesystem::path& thePath,
                    const boost::regex& /* thePattern */,
                    const std::string& theMessage)
{
  std::cerr << fmt::format(
      "Warning: satellite engine failed to scan '{}': {}\n", thePath.string(), theMessage);
}

}  // namespace Satellite
}  // namespace Engine
}  // namespace SmartMet
