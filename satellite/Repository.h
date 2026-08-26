// ======================================================================
/*!
 * \brief Catalog of the available satellite images
 *
 * Images of one producer are kept sorted by valid time, hence finding
 * the latest one or the one closest to a requested time is a simple
 * map lookup. All public methods are thread safe.
 */
// ======================================================================

#pragma once

#include "ImageInfo.h"
#include "Producer.h"
#include <spine/Thread.h>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace SmartMet
{
namespace Engine
{
namespace Satellite
{
// Estimated WGS84 bounding box of a producer plus the metadata needed
// for GetCapabilities responses.

struct ProducerInfo
{
  std::string name;
  std::string title;
  std::string abstract;
  std::vector<std::string> keywords;

  // WGS84 bounding box: minx miny maxx maxy. Empty if not known yet.
  std::optional<std::array<double, 4>> bbox;
};

class Repository
{
 public:
  // Called once at startup for each configured producer
  void add(const Producer& theProducer);

  // Scanner interface

  void insert(const std::string& theProducer, const ImageInfoPtr& theImage);
  void remove(const std::string& theProducer, const std::string& thePath);

  // Query interface

  std::vector<std::string> producers() const;
  bool hasProducer(const std::string& theProducer) const;
  ProducerInfo producerInfo(const std::string& theProducer) const;
  std::vector<Fmi::DateTime> times(const std::string& theProducer) const;
  Fmi::DateTime latestTime(const std::string& theProducer) const;
  std::size_t size(const std::string& theProducer) const;

  // Image closest to the requested time, or the latest one if no time is
  // given. Returns nullptr if the producer is unknown or no image is
  // within the given tolerance. A zero tolerance requires an exact match.
  ImageInfoPtr find(const std::string& theProducer,
                    const std::optional<Fmi::DateTime>& theTime,
                    const Fmi::TimeDuration& theTolerance) const;

 private:
  struct Contents
  {
    Producer producer;
    std::map<Fmi::DateTime, ImageInfoPtr> images;
    std::optional<std::array<double, 4>> bbox;  // Estimated from the newest image
  };

  // Erase the oldest images if the producer has more than max_files of them.
  // Must be called with the write lock held.
  static void limitSize(Contents& theContents);

  mutable Spine::MutexType itsMutex;
  std::map<std::string, Contents> itsProducers;
};

}  // namespace Satellite
}  // namespace Engine
}  // namespace SmartMet
