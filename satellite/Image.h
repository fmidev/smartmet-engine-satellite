// ======================================================================
/*!
 * \brief Results of a warp operation, and the options describing it
 *
 * Part of the engine API, hence defined separately from Gdal.h which is
 * internal to the engine and drags in the GDAL headers.
 */
// ======================================================================

#pragma once

#include <array>
#include <string>
#include <vector>

namespace SmartMet
{
namespace Engine
{
namespace Satellite
{
// A warped image in the layout expected by the WMS plugin: ARGB pixels
// in a row-major top-to-bottom buffer.

struct Image
{
  int width{0};
  int height{0};
  std::vector<unsigned int> pixels;

  // Overview level the pixels were read from, -1 for the full resolution
  // image. Reported because it decides the cost of the request, so it is
  // worth being able to see it.
  int overview{-1};
};

// Uncoloured data warped to the requested projection. Missing values are
// NaN regardless of what the image itself uses to mark them.

struct ValueImage
{
  int width{0};
  int height{0};
  std::vector<float> values;
};

// Target of a warp operation

struct WarpOptions
{
  std::string crs;               // Target CRS as WKT or as any string GDAL understands
  std::array<double, 4> bbox{};  // Target bounding box: xmin ymin xmax ymax
  int width{0};
  int height{0};
};

}  // namespace Satellite
}  // namespace Engine
}  // namespace SmartMet
