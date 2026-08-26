// ======================================================================
/*!
 * \brief Configuration of a single satellite producer
 *
 * One producer is one WMS layer identity: a directory holding the
 * images of one composite, and a pattern selecting that composite from
 * the other composites stored in the same directory.
 */
// ======================================================================

#pragma once

#include <boost/regex.hpp>
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace SmartMet
{
namespace Engine
{
namespace Satellite
{
struct Producer
{
  std::string name;  // Producer name used in layer definitions

  std::filesystem::path directory;  // Absolute directory to scan

  // File name pattern selecting the composite. Note that the pattern
  // must match the whole file name, hence the usual form is
  // ".*_composite_name\.tif$".
  std::string pattern;
  boost::regex regex;  // Compiled pattern

  // GetCapabilities metadata

  std::string title;
  std::string abstract;
  std::vector<std::string> keywords;

  // Optional WGS84 bounding box override: minx miny maxx maxy. If not
  // set the bounding box is estimated from the newest image.
  std::optional<std::array<double, 4>> bbox;

  int refresh_interval_secs{60};  // Directory scan interval
  std::size_t max_files{200};     // Keep only this many newest images, 0 = all
};

}  // namespace Satellite
}  // namespace Engine
}  // namespace SmartMet
