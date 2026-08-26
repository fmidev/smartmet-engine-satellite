// ======================================================================
/*!
 * \brief Configuration of a single satellite product
 *
 * A product is identified by a producer and a parameter: the producer
 * says which satellite or data stream the images come from, and the
 * parameter says which composite of it. Both are needed because one
 * directory holds many composites of many times, and because clients
 * build their menus by listing the parameters of a producer.
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
struct Product
{
  std::string id;         // Name of the configuration group, for messages
  std::string producer;   // Satellite or data stream, for example "meteosat"
  std::string parameter;  // Composite, for example "natural"

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

// A product is addressed by the producer and parameter pair
using ProductKey = std::pair<std::string, std::string>;

inline ProductKey make_key(const std::string& theProducer, const std::string& theParameter)
{
  return {theProducer, theParameter};
}

}  // namespace Satellite
}  // namespace Engine
}  // namespace SmartMet
