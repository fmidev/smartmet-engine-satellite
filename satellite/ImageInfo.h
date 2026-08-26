// ======================================================================
/*!
 * \brief Metadata of a single satellite image file
 *
 * Filled once when the scanner first sees the file. The geographic
 * metadata is read with GDAL at that point so that request time never
 * needs to open the file merely to find out what it contains.
 */
// ======================================================================

#pragma once

#include <macgyver/DateTime.h>
#include <array>
#include <memory>
#include <optional>
#include <string>

namespace SmartMet
{
namespace Engine
{
namespace Satellite
{
// Band layout of the image. Satellite products are precoloured, hence
// the only interesting question is how to get RGBA out of the bands.

enum class BandModel
{
  RGBA,       // 4 x Byte: red, green, blue, alpha
  RGB,        // 3 x Byte: no alpha channel present
  GrayAlpha,  // 2 x Byte: gray replicated to RGB, plus alpha
  Gray,       // 1 x Byte: gray replicated to RGB, opaque
  Float       // 1 x Float32: uncoloured data, not supported yet
};

std::string to_string(BandModel model);

struct ImageInfo
{
  std::string path;    // Absolute path of the image file
  Fmi::DateTime time;  // Valid time parsed from the file name
  std::size_t hash{};  // Hash of path + modification time + size: the ETag basis

  // Geographic metadata as reported by GDAL

  std::string wkt;                       // Coordinate reference system of the image
  std::array<double, 6> geotransform{};  // GDAL geotransform
  int width{0};
  int height{0};
  int bands{0};
  BandModel model{BandModel::RGBA};

  // Alpha handling. GDAL numbers bands from one, zero means "none".
  int alphaband{0};
  bool maskband{false};  // True if the dataset has a per-dataset mask band

  // Estimated WGS84 bounding box: minx miny maxx maxy. Not set if the
  // image corners could not be transformed to geographic coordinates.
  std::optional<std::array<double, 4>> bbox;
};

using ImageInfoPtr = std::shared_ptr<const ImageInfo>;

}  // namespace Satellite
}  // namespace Engine
}  // namespace SmartMet
