// ======================================================================
/*!
 * \brief Satellite engine implementation
 */
// ======================================================================

#include "Engine.h"
#include <macgyver/Exception.h>

namespace SmartMet
{
namespace Engine
{
namespace Satellite
{
// ----------------------------------------------------------------------
/*!
 * \brief The constructor only stores the configuration file name
 *
 * The real work is done in init(), which the server calls in a separate
 * thread. Plugins asking for the engine are made to wait until it is
 * finished.
 */
// ----------------------------------------------------------------------

Engine::Engine(const std::string& theConfigFile) : itsConfigFile(theConfigFile) {}

// ----------------------------------------------------------------------
/*!
 * \brief Read the configuration and scan the image directories
 */
// ----------------------------------------------------------------------

void Engine::init()
{
  try
  {
    Gdal::initialize();

    itsConfig = std::make_unique<Config>(itsConfigFile);

    for (const auto& [key, product] : itsConfig->products())
      itsRepository.add(product);

    itsScanner = std::make_unique<Scanner>(itsRepository);
    itsScanner->start(itsConfig->products());
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Satellite engine initialization failed");
  }
}

// ----------------------------------------------------------------------

void Engine::shutdown()
{
  if (itsScanner)
    itsScanner->stop();
}

// ----------------------------------------------------------------------

std::vector<std::string> Engine::producers() const
{
  return itsRepository.producers();
}

// ----------------------------------------------------------------------

std::vector<std::string> Engine::parameters(const std::string& theProducer) const
{
  return itsRepository.parameters(theProducer);
}

// ----------------------------------------------------------------------

bool Engine::hasProducer(const std::string& theProducer) const
{
  return itsRepository.hasProducer(theProducer);
}

// ----------------------------------------------------------------------

bool Engine::hasProduct(const std::string& theProducer, const std::string& theParameter) const
{
  return itsRepository.hasProduct(make_key(theProducer, theParameter));
}

// ----------------------------------------------------------------------

ProductInfo Engine::productInfo(const std::string& theProducer,
                                const std::string& theParameter) const
{
  return itsRepository.productInfo(make_key(theProducer, theParameter));
}

// ----------------------------------------------------------------------

std::vector<Fmi::DateTime> Engine::times(const std::string& theProducer,
                                         const std::string& theParameter) const
{
  return itsRepository.times(make_key(theProducer, theParameter));
}

// ----------------------------------------------------------------------

Fmi::DateTime Engine::latestTime(const std::string& theProducer,
                                 const std::string& theParameter) const
{
  return itsRepository.latestTime(make_key(theProducer, theParameter));
}

// ----------------------------------------------------------------------

std::size_t Engine::imageCount(const std::string& theProducer,
                               const std::string& theParameter) const
{
  return itsRepository.size(make_key(theProducer, theParameter));
}

// ----------------------------------------------------------------------

ImageInfoPtr Engine::find(const std::string& theProducer,
                          const std::string& theParameter,
                          const std::optional<Fmi::DateTime>& theTime,
                          const Fmi::TimeDuration& theTolerance) const
{
  return itsRepository.find(make_key(theProducer, theParameter), theTime, theTolerance);
}

// ----------------------------------------------------------------------

Image Engine::warp(const ImageInfo& theImage, const WarpOptions& theOptions) const
{
  return Gdal::warp(theImage, theOptions);
}

// ----------------------------------------------------------------------
/*!
 * \brief Cache statistics for the admin plugin
 *
 * The engine does not cache pixel data yet: the operating system page
 * cache serves the image files and the WMS plugin caches the rendered
 * images.
 */
// ----------------------------------------------------------------------

Fmi::Cache::CacheStatistics Engine::getCacheStats() const
{
  return {};
}

}  // namespace Satellite
}  // namespace Engine
}  // namespace SmartMet

// ======================================================================
//  Dynamic loading entry points
// ======================================================================

extern "C" void* engine_class_creator(const char* configfile, void* /* user_data */)
{
  return new SmartMet::Engine::Satellite::Engine(configfile);
}

extern "C" const char* engine_name()
{
  return "Satellite";
}
