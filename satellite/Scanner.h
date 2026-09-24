// ======================================================================
/*!
 * \brief Directory scanner keeping the image catalog up to date
 *
 * The products are grouped by directory and each directory is polled
 * the way the querydata engine polls its directories: the modification
 * time of the directory is checked at every tick, and the directory is
 * listed only when the time has changed. The assumption behind this is
 * that files arrive and get deleted but are never rewritten in place
 * once complete.
 *
 * A listing reads the file names only. The names are compared with
 * those of the previous listing, new files are credited to the products
 * whose patterns match them, and deleted files are removed from the
 * repository. A new file is not read at once, though: the production
 * writes some of its images directly under their final names, the big
 * full disc ones among them, and a file caught while it is being
 * written either fails to open or, worse, opens with its pixels still
 * missing. Hence a new file waits until it has not been modified for
 * min_file_age seconds, and a file which fails to read anyway is tried
 * again once its size or modification time has changed. The waiting
 * files cost one stat per tick, the directory is not listed for them.
 * Of the files which are ready, the ones which fit under max_files are
 * read.
 */
// ======================================================================

#pragma once

#include "Product.h"
#include "Repository.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <ctime>
#include <map>
#include <mutex>
#include <set>
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
  // number of threads. A file is read only once it has not been
  // modified for the given number of seconds.
  void start(const std::map<ProductKey, Product>& theProducts,
             int theMaxThreads,
             int theMinFileAge);

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

  // A matching file of a directory
  struct File
  {
    // The indices of the watches whose patterns the name matched, and
    // of those the ones which have not read the file yet
    std::vector<std::size_t> matches;
    std::vector<std::size_t> unread;

    // The size and modification time of the file when a read last
    // failed, so that a broken file is not tried again until it changes
    bool failed{false};
    std::uintmax_t failed_size{0};
    std::time_t failed_mtime{-1};
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

    // The matching file names of the last listing, so that a name is
    // matched against the patterns once only
    std::map<std::string, File> names;

    // The names whose file has not been read by every product it
    // matched yet
    std::set<std::string> waiting;
  };

  using Candidate = std::pair<Fmi::DateTime, std::filesystem::path>;

  // A metadata read of a file which is ready. The first scan collects
  // them, so that it can list first and read with many threads
  // afterwards, and records the outcome for the scanner to act on once
  // the threads are done.
  struct Job
  {
    Directory* directory;
    std::size_t watch;
    Candidate candidate;
    std::uintmax_t size;
    std::time_t mtime;
    bool ok{false};
  };

  // With a job list the reads are collected into it instead of being done
  void scan(Directory& theDirectory, std::vector<Job>* theJobs = nullptr);
  void list(Directory& theDirectory, std::time_t theTime);
  void examine(Directory& theDirectory, std::vector<Job>* theJobs);
  void forget(Directory& theDirectory);
  bool readOne(const Watch& theWatch, const Candidate& theCandidate);
  static void done(const Job& theJob);
  void run();

  Repository& itsRepository;
  std::vector<Directory> itsDirectories;
  std::time_t itsMinFileAge{30};

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
