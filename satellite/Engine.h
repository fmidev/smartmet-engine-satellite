// ======================================================================
/*!
 * \brief Satellite engine
 *
 * Serves precoloured satellite images to the WMS plugin. Unlike the
 * other data sources of the server the images are ready for display:
 * the pixels are RGBA and no styling is applied, hence the projection
 * transformations use nearest neighbour interpolation.
 *
 * One producer is one composite of one instrument, for example the
 * natural colour composite of SEVIRI in EPSG:3035. The producer name
 * identifies the data completely: there is no parameter to choose.
 *
 * The images are stored as cloud optimized GeoTIFF files by the data
 * production system. The engine watches the configured directories,
 * reads the metadata of the new files, and serves the images warped to
 * the projection requested by the client.
 */
// ======================================================================

#pragma once

#include "Config.h"
#include "Gdal.h"
#include "ImageInfo.h"
#include "Repository.h"
#include "Scanner.h"
#include <spine/SmartMetEngine.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace SmartMet
{
namespace Engine
{
namespace Satellite
{
class Engine : public SmartMet::Spine::SmartMetEngine
{
 public:
  Engine() = delete;
  explicit Engine(const std::string& theConfigFile);

  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;
  Engine(Engine&&) = delete;
  Engine& operator=(Engine&&) = delete;

  // Available producers, sorted by name
  std::vector<std::string> producers() const;

  bool hasProducer(const std::string& theProducer) const;

  // Metadata needed for GetCapabilities responses
  ProducerInfo producerInfo(const std::string& theProducer) const;

  // Valid times of the available images, sorted
  std::vector<Fmi::DateTime> times(const std::string& theProducer) const;

  // Time of the newest image, NOT_A_DATE_TIME if there are none
  Fmi::DateTime latestTime(const std::string& theProducer) const;

  // Number of images available
  std::size_t imageCount(const std::string& theProducer) const;

  // Find the image closest to the requested time, or the newest image if
  // no time is requested. Returns nullptr if the producer is unknown or
  // no image is within the tolerance.
  ImageInfoPtr find(const std::string& theProducer,
                    const std::optional<Fmi::DateTime>& theTime,
                    const Fmi::TimeDuration& theTolerance) const;

  // Warp an image to the requested projection. The result is an ARGB
  // pixel buffer; the areas not covered by the image are transparent.
  Image warp(const ImageInfo& theImage, const WarpOptions& theOptions) const;

 protected:
  void init() override;
  void shutdown() override;

 private:
  Fmi::Cache::CacheStatistics getCacheStats() const override;

  std::string itsConfigFile;
  std::unique_ptr<Config> itsConfig;
  Repository itsRepository;
  std::unique_ptr<Scanner> itsScanner;
};

}  // namespace Satellite
}  // namespace Engine
}  // namespace SmartMet
