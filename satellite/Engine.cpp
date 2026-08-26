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

    for (const auto& [name, producer] : itsConfig->producers())
      itsRepository.add(producer);

    itsScanner = std::make_unique<Scanner>(itsRepository);
    itsScanner->start(itsConfig->producers());
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

bool Engine::hasProducer(const std::string& theProducer) const
{
  return itsRepository.hasProducer(theProducer);
}

// ----------------------------------------------------------------------

ProducerInfo Engine::producerInfo(const std::string& theProducer) const
{
  return itsRepository.producerInfo(theProducer);
}

// ----------------------------------------------------------------------

std::vector<Fmi::DateTime> Engine::times(const std::string& theProducer) const
{
  return itsRepository.times(theProducer);
}

// ----------------------------------------------------------------------

Fmi::DateTime Engine::latestTime(const std::string& theProducer) const
{
  return itsRepository.latestTime(theProducer);
}

// ----------------------------------------------------------------------

std::size_t Engine::imageCount(const std::string& theProducer) const
{
  return itsRepository.size(theProducer);
}

// ----------------------------------------------------------------------

ImageInfoPtr Engine::find(const std::string& theProducer,
                          const std::optional<Fmi::DateTime>& theTime,
                          const Fmi::TimeDuration& theTolerance) const
{
  return itsRepository.find(theProducer, theTime, theTolerance);
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
