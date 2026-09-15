// ======================================================================
/*!
 * \brief Satellite engine configuration
 */
// ======================================================================

#pragma once

#include "Product.h"
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

  const std::map<ProductKey, Product>& products() const { return itsProducts; }

  // Threads reading image metadata during the first scan
  int maxThreads() const { return itsMaxThreads; }

 private:
  void parseProduct(const libconfig::Setting& theSetting);

  libconfig::Config itsConfig;
  std::filesystem::path itsRootDir;
  int itsMaxThreads{10};
  std::map<ProductKey, Product> itsProducts;
};

}  // namespace Satellite
}  // namespace Engine
}  // namespace SmartMet
