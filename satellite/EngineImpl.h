// ======================================================================
/*!
 * \brief Satellite engine implementation
 *
 * The class doing the actual work behind the Engine API. It is internal
 * to the engine: the header is not installed, and nothing outside the
 * engine library may refer to it.
 */
// ======================================================================

#pragma once

#include "Config.h"
#include "Engine.h"
#include "Repository.h"
#include "Scanner.h"
#include <memory>
#include <string>

namespace SmartMet
{
namespace Engine
{
namespace Satellite
{
class EngineImpl : public Engine
{
 public:
  EngineImpl() = delete;
  explicit EngineImpl(const std::string& theConfigFile);

  std::vector<std::string> producers() const override;
  std::vector<std::string> parameters(const std::string& theProducer) const override;

  bool hasProducer(const std::string& theProducer) const override;
  bool hasProduct(const std::string& theProducer, const std::string& theParameter) const override;

  ProductInfo productInfo(const std::string& theProducer,
                          const std::string& theParameter) const override;

  std::vector<Fmi::DateTime> times(const std::string& theProducer,
                                   const std::string& theParameter) const override;

  Fmi::DateTime latestTime(const std::string& theProducer,
                           const std::string& theParameter) const override;

  std::size_t imageCount(const std::string& theProducer,
                         const std::string& theParameter) const override;

  ImageInfoPtr find(const std::string& theProducer,
                    const std::string& theParameter,
                    const std::optional<Fmi::DateTime>& theTime,
                    const Fmi::TimeDuration& theTolerance) const override;

  Image warp(const ImageInfo& theImage, const WarpOptions& theOptions) const override;

  ValueImage warpValues(const ImageInfo& theImage, const WarpOptions& theOptions) const override;

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
