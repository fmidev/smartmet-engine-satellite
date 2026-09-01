// ======================================================================
/*!
 * \brief Satellite engine implementation
 */
// ======================================================================

#include "EngineImpl.h"
#include "Gdal.h"
#include <macgyver/AnsiEscapeCodes.h>
#include <macgyver/Exception.h>
#include <spine/ConfigBase.h>
#include <spine/Convenience.h>
#include <iostream>

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

EngineImpl::EngineImpl(const std::string& theConfigFile) : itsConfigFile(theConfigFile) {}

// ----------------------------------------------------------------------
/*!
 * \brief Read the configuration and scan the image directories
 */
// ----------------------------------------------------------------------

void EngineImpl::init()
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

void EngineImpl::shutdown()
{
  if (itsScanner)
    itsScanner->stop();
}

// ----------------------------------------------------------------------

std::vector<std::string> EngineImpl::producers() const
{
  return itsRepository.producers();
}

// ----------------------------------------------------------------------

std::vector<std::string> EngineImpl::parameters(const std::string& theProducer) const
{
  return itsRepository.parameters(theProducer);
}

// ----------------------------------------------------------------------

bool EngineImpl::hasProducer(const std::string& theProducer) const
{
  return itsRepository.hasProducer(theProducer);
}

// ----------------------------------------------------------------------

bool EngineImpl::hasProduct(const std::string& theProducer, const std::string& theParameter) const
{
  return itsRepository.hasProduct(make_key(theProducer, theParameter));
}

// ----------------------------------------------------------------------

ProductInfo EngineImpl::productInfo(const std::string& theProducer,
                                    const std::string& theParameter) const
{
  return itsRepository.productInfo(make_key(theProducer, theParameter));
}

// ----------------------------------------------------------------------

std::vector<Fmi::DateTime> EngineImpl::times(const std::string& theProducer,
                                             const std::string& theParameter) const
{
  return itsRepository.times(make_key(theProducer, theParameter));
}

// ----------------------------------------------------------------------

Fmi::DateTime EngineImpl::latestTime(const std::string& theProducer,
                                     const std::string& theParameter) const
{
  return itsRepository.latestTime(make_key(theProducer, theParameter));
}

// ----------------------------------------------------------------------

std::size_t EngineImpl::imageCount(const std::string& theProducer,
                                   const std::string& theParameter) const
{
  return itsRepository.size(make_key(theProducer, theParameter));
}

// ----------------------------------------------------------------------

ImageInfoPtr EngineImpl::find(const std::string& theProducer,
                              const std::string& theParameter,
                              const std::optional<Fmi::DateTime>& theTime,
                              const Fmi::TimeDuration& theTolerance) const
{
  return itsRepository.find(make_key(theProducer, theParameter), theTime, theTolerance);
}

// ----------------------------------------------------------------------

Image EngineImpl::warp(const ImageInfo& theImage, const WarpOptions& theOptions) const
{
  return Gdal::warp(theImage, theOptions);
}

// ----------------------------------------------------------------------

ValueImage EngineImpl::warpValues(const ImageInfo& theImage, const WarpOptions& theOptions) const
{
  return Gdal::warpValues(theImage, theOptions);
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

Fmi::Cache::CacheStatistics EngineImpl::getCacheStats() const
{
  return {};
}

}  // namespace Satellite
}  // namespace Engine
}  // namespace SmartMet

// ======================================================================
//  Dynamic loading entry points
// ======================================================================

// ----------------------------------------------------------------------
/*!
 * \brief Create the engine
 *
 * A disabled engine is an object of the API base class: every method of
 * it throws, hence the plugin loads and runs, and only the requests
 * which would need satellite imagery fail. This is what lets a server
 * configuration keep the engine listed without the imagery being
 * available, for example on a machine which has no image directories.
 */
// ----------------------------------------------------------------------

extern "C" void* engine_class_creator(const char* configfile, void* /* user_data */)
{
  try
  {
    using SmartMet::Spine::log_time_str;

    const bool disabled = [&configfile]()
    {
      const char* name = "SmartMet::Engine::Satellite::Engine::create";

      if (configfile == nullptr || *configfile == '\0')
      {
        std::cout << log_time_str() << ' ' << ANSI_FG_RED << name
                  << ": configuration file not specified or its name is empty string: "
                  << "engine disabled." << ANSI_FG_DEFAULT << '\n';
        return true;
      }

      SmartMet::Spine::ConfigBase cfg(configfile);
      const bool result = cfg.get_optional_config_param<bool>("disabled", false);
      if (result)
        std::cout << log_time_str() << ' ' << ANSI_FG_RED << name << ": engine disabled"
                  << ANSI_FG_DEFAULT << '\n';
      return result;
    }();

    if (disabled)
      return new SmartMet::Engine::Satellite::Engine();

    return new SmartMet::Engine::Satellite::EngineImpl(configfile);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Satellite engine creation failed");
  }
}

extern "C" const char* engine_name()
{
  return "Satellite";
}
