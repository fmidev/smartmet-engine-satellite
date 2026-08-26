// ======================================================================
/*!
 * \brief Satellite engine configuration
 */
// ======================================================================

#pragma once

#include "Producer.h"
#include <libconfig.h++>
#include <map>
#include <string>

namespace SmartMet
{
namespace Engine
{
namespace Satellite
{
class Config
{
 public:
  Config() = delete;
  explicit Config(const std::string& theFileName);

  const std::map<std::string, Producer>& producers() const { return itsProducers; }

 private:
  void parseProducer(const libconfig::Setting& theSetting);

  libconfig::Config itsConfig;
  std::filesystem::path itsRootDir;
  std::map<std::string, Producer> itsProducers;
};

}  // namespace Satellite
}  // namespace Engine
}  // namespace SmartMet
