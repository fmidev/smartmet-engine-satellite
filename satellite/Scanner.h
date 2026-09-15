// ======================================================================
/*!
 * \brief Directory scanner keeping the image catalog up to date
 *
 * The products are grouped by directory and each directory is polled
 * the way the querydata engine polls its directories: the modification
 * time of the directory is checked at every tick, and the directory is
 * listed only when the time has changed. The assumption behind this is
 * that files arrive and get deleted but are never rewritten in place,
 * which holds for the satellite production: images are written under a
 * temporary name and renamed into place.
 *
 * A listing reads the file names only, no file is touched. The names
 * are compared with those of the previous listing, new files are
 * credited to the products whose patterns match them, and the metadata
 * of the ones which fit under max_files is read. Deleted files are
 * removed from the repository.
 */
// ======================================================================

#pragma once

#include "Product.h"
#include "Repository.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

  // Scan the directories of the given products once and start polling
  // them. Returns when the first scan of every directory has completed.
  // The first scan reads the metadata of the images with the given
  // number of threads.
  void start(const std::map<ProductKey, Product>& theProducts, int theMaxThreads);

  void stop();

  bool ready() const { return itsReady; }

  // Number of image files whose metadata the scanner has tried to read
  // since it was started. Files beyond max_files are never read, which
  // is what keeps the first scan of a directory holding weeks of history
  // short, and this counter is how the tests verify it.
  std::size_t imagesRead() const { return itsImagesRead; }

  // Number of directory listings made since the start. A quiet directory
  // costs one stat per tick and no listing.
  std::size_t listings() const { return itsListings; }

  // Parse the valid time from a file name of the form
  // YYYYMMDD_HHMM_Platform_area_composite.tif. Returns NOT_A_DATE_TIME
  // if the name does not begin with a timestamp.
  static Fmi::DateTime parseTime(const std::string& theFileName);

 private:
  // One product watching a directory
  struct Watch
  {
    ProductKey key;
    boost::regex regex;
    std::size_t max_files;
  };

  // One directory and the products sharing it
  struct Directory
  {
    std::filesystem::path path;
    std::vector<Watch> watches;
    std::chrono::seconds interval{0};  // The shortest interval of the products
    std::chrono::steady_clock::time_point next_scan;

    // The directory modification time when it was last listed, -1 if
    // never, and whether that listing happened clearly after the last
    // change. A file arriving within the same second as the listing
    // leaves the directory time unchanged, hence an unsettled directory
    // is listed again at the next tick regardless of its time.
    std::time_t mtime{-1};
    bool settled{false};

    // The matching file names of the last listing and the indices of the
    // watches each of them matched, so that a name is matched against
    // the patterns once only
    std::map<std::string, std::vector<std::size_t>> names;
  };

  using Candidate = std::pair<Fmi::DateTime, std::filesystem::path>;

  // A metadata read postponed from a listing, so that the first scan can
  // list first and read with many threads afterwards
  struct Job
  {
    const Watch* watch;
    Candidate candidate;
  };

  // With a job list the reads are collected into it instead of being done
  void scan(Directory& theDirectory, std::vector<Job>* theJobs = nullptr);
  void list(Directory& theDirectory, std::time_t theTime, std::vector<Job>* theJobs);
  void forget(Directory& theDirectory);
  void read(const Watch& theWatch, std::vector<Candidate> theCandidates, std::vector<Job>* theJobs);
  void readOne(const Watch& theWatch, const Candidate& theCandidate);
  void run();

  Repository& itsRepository;
  std::vector<Directory> itsDirectories;

  std::thread itsThread;
  std::mutex itsMutex;
  std::condition_variable itsCondition;

  std::atomic<bool> itsShutdownRequested{false};
  std::atomic<bool> itsReady{false};
  std::atomic<std::size_t> itsImagesRead{0};
  std::atomic<std::size_t> itsListings{0};
};

}  // namespace Satellite
}  // namespace Engine
}  // namespace SmartMet
