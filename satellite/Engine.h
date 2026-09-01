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
 *
 * This header declares the API only. Every method is defined inline so
 * that a plugin using the API links and loads even when the engine
 * library itself is not loaded; the actual work is done by the derived
 * class EngineImpl, which is internal to the engine. An object of this
 * base class is what the server gets when the engine is disabled, and
 * then every request for imagery fails with an exception the plugin
 * turns into an error response.
 */
// ======================================================================

#pragma once

#include "Image.h"
#include "ImageInfo.h"
#include "ProductInfo.h"
#include <macgyver/DateTime.h>
#include <macgyver/Exception.h>
#include <spine/SmartMetEngine.h>
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
  Engine() = default;
  ~Engine() override = default;

  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;
  Engine(Engine&&) = delete;
  Engine& operator=(Engine&&) = delete;

  // The satellites available, sorted. A client builds the first level
  // of its menu from these.
  virtual std::vector<std::string> producers() const { unavailable(BCP); }

  // The composites available for one satellite, sorted. The second level
  // of the menu.
  virtual std::vector<std::string> parameters(const std::string& /* theProducer */) const
  {
    unavailable(BCP);
  }

  virtual bool hasProducer(const std::string& /* theProducer */) const { unavailable(BCP); }

  virtual bool hasProduct(const std::string& /* theProducer */,
                          const std::string& /* theParameter */) const
  {
    unavailable(BCP);
  }

  // Metadata needed for GetCapabilities responses
  virtual ProductInfo productInfo(const std::string& /* theProducer */,
                                  const std::string& /* theParameter */) const
  {
    unavailable(BCP);
  }

  // Valid times of the available images, sorted
  virtual std::vector<Fmi::DateTime> times(const std::string& /* theProducer */,
                                           const std::string& /* theParameter */) const
  {
    unavailable(BCP);
  }

  // Time of the newest image, NOT_A_DATE_TIME if there are none
  virtual Fmi::DateTime latestTime(const std::string& /* theProducer */,
                                   const std::string& /* theParameter */) const
  {
    unavailable(BCP);
  }

  // Number of images available
  virtual std::size_t imageCount(const std::string& /* theProducer */,
                                 const std::string& /* theParameter */) const
  {
    unavailable(BCP);
  }

  // Find the image closest to the requested time, or the newest image if
  // no time is requested. Returns nullptr if the product is unknown or
  // no image is within the tolerance.
  virtual ImageInfoPtr find(const std::string& /* theProducer */,
                            const std::string& /* theParameter */,
                            const std::optional<Fmi::DateTime>& /* theTime */,
                            const Fmi::TimeDuration& /* theTolerance */) const
  {
    unavailable(BCP);
  }

  // Warp an image to the requested projection. The result is an ARGB
  // pixel buffer; the areas not covered by the image are transparent.
  virtual Image warp(const ImageInfo& /* theImage */, const WarpOptions& /* theOptions */) const
  {
    unavailable(BCP);
  }

  // Warp the values of an uncoloured image, for the caller to colour with
  // a colour map. Missing values are NaN.
  virtual ValueImage warpValues(const ImageInfo& /* theImage */,
                                const WarpOptions& /* theOptions */) const
  {
    unavailable(BCP);
  }

 protected:
  void init() override {}
  void shutdown() override {}

 private:
  [[noreturn]] static void unavailable(const char* file, int line, const char* function)
  {
    throw Fmi::Exception(file, line, function, "Satellite engine not available");
  }
};

// The actual implementation, internal to the engine

class EngineImpl;

}  // namespace Satellite
}  // namespace Engine
}  // namespace SmartMet
