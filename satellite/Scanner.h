// ======================================================================
/*!
 * \brief Directory scanner keeping the image catalog up to date
 *
 * One directory monitor watches all configured product directories.
 * New files are read for their metadata and inserted into the
 * repository, deleted files are removed from it.
 */
// ======================================================================

#pragma once

#include "Product.h"
#include "Repository.h"
#include <boost/thread.hpp>
#include <macgyver/DirectoryMonitor.h>
#include <atomic>
#include <map>
#include <string>

namespace SmartMet
{
namespace Engine
{
namespace Satellite
{
class Scanner
{
 public:
  explicit Scanner(Repository& theRepository);
  ~Scanner();

  Scanner(const Scanner&) = delete;
  Scanner& operator=(const Scanner&) = delete;
  Scanner(Scanner&&) = delete;
  Scanner& operator=(Scanner&&) = delete;

  // Start watching the directories of the given products. Returns when
  // the first scan of every directory has completed.
  void start(const std::map<ProductKey, Product>& theProducts);

  void stop();

  bool ready() const;

  // Number of image files whose metadata the scanner has tried to read
  // since it was started. Files beyond max_files are never read, which
  // is what keeps the first scan of a directory holding weeks of history
  // short, and this counter is how the tests verify it.
  std::size_t imagesRead() const { return itsImagesRead; }

  // Parse the valid time from a file name of the form
  // YYYYMMDD_HHMM_Platform_area_composite.tif. Returns NOT_A_DATE_TIME
  // if the name does not begin with a timestamp.
  static Fmi::DateTime parseTime(const std::string& theFileName);

 private:
  void update(Fmi::DirectoryMonitor::Watcher theWatcher,
              const std::filesystem::path& thePath,
              const boost::regex& thePattern,
              const Fmi::DirectoryMonitor::Status& theStatus);

  void error(Fmi::DirectoryMonitor::Watcher theWatcher,
             const std::filesystem::path& thePath,
             const boost::regex& thePattern,
             const std::string& theMessage);

  Repository& itsRepository;

  Fmi::DirectoryMonitor itsMonitor;
  boost::thread itsMonitorThread;

  // Watcher identity to the product it belongs to. Written before the
  // monitor is started, read only afterwards.
  struct Watched
  {
    ProductKey key;
    std::size_t max_files;
  };
  std::map<Fmi::DirectoryMonitor::Watcher, Watched> itsWatchers;

  std::atomic<bool> itsShutdownRequested{false};
  std::atomic<std::size_t> itsImagesRead{0};
};

}  // namespace Satellite
}  // namespace Engine
}  // namespace SmartMet
