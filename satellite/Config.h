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

  // Seconds a file must go unmodified before it is read
  int minFileAge() const { return itsMinFileAge; }

 private:
  void parseProduct(const libconfig::Setting& theSetting);

  libconfig::Config itsConfig;
  std::filesystem::path itsRootDir;
  int itsMaxThreads{10};
  int itsMinFileAge{30};
  std::map<ProductKey, Product> itsProducts;
};

}  // namespace Satellite
}  // namespace Engine
}  // namespace SmartMet
