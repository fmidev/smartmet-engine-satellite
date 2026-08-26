// ======================================================================
/*!
 * \brief Satellite engine
 *
 * Serves precoloured satellite images to the WMS plugin. Unlike the
 * other data sources of the server the images are ready for display:
 * the pixels are RGBA and no styling is applied, hence the projection
 * transformations use nearest neighbour interpolation.
 *
 * A product is identified by a producer and a parameter: the producer
 * is the satellite or data stream, for example "meteosat", and the
 * parameter is the composite of it, for example "natural". Clients build
 * their menus by listing the producers and then the parameters of the
 * one the user picked.
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

  // The satellites available, sorted. A client builds the first level
  // of its menu from these.
  std::vector<std::string> producers() const;

  // The composites available for one satellite, sorted. The second level
  // of the menu.
  std::vector<std::string> parameters(const std::string& theProducer) const;

  bool hasProducer(const std::string& theProducer) const;
  bool hasProduct(const std::string& theProducer, const std::string& theParameter) const;

  // Metadata needed for GetCapabilities responses
  ProductInfo productInfo(const std::string& theProducer, const std::string& theParameter) const;

  // Valid times of the available images, sorted
  std::vector<Fmi::DateTime> times(const std::string& theProducer,
                                   const std::string& theParameter) const;

  // Time of the newest image, NOT_A_DATE_TIME if there are none
  Fmi::DateTime latestTime(const std::string& theProducer, const std::string& theParameter) const;

  // Number of images available
  std::size_t imageCount(const std::string& theProducer, const std::string& theParameter) const;

  // Find the image closest to the requested time, or the newest image if
  // no time is requested. Returns nullptr if the product is unknown or
  // no image is within the tolerance.
  ImageInfoPtr find(const std::string& theProducer,
                    const std::string& theParameter,
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
