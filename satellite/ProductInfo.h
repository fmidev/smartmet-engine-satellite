// ======================================================================
/*!
 * \brief Metadata of one product for GetCapabilities responses and menus
 *
 * Part of the engine API, hence defined separately from Repository.h
 * which is internal to the engine.
 */
// ======================================================================

#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace SmartMet
{
namespace Engine
{
namespace Satellite
{
struct ProductInfo
{
  std::string producer;
  std::string parameter;
  std::string title;
  std::string abstract;
  std::vector<std::string> keywords;

  // WGS84 bounding box: minx miny maxx maxy. Empty if not known yet.
  std::optional<std::array<double, 4>> bbox;
};

}  // namespace Satellite
}  // namespace Engine
}  // namespace SmartMet
