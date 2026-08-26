// ======================================================================
/*!
 * \brief GDAL access to satellite images
 *
 * All GDAL and PROJ usage of the engine is isolated here.
 *
 * Note that GDALDataset is not thread safe and must not be shared
 * between threads. Every function below opens its own dataset and
 * closes it before returning. Likewise coordinate reference systems are
 * always parsed locally, never shared, since sharing them has been
 * observed to corrupt the heap.
 */
// ======================================================================

#pragma once

#include "ImageInfo.h"
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
};

// Target of a warp operation

struct WarpOptions
{
  std::string crs;               // Target CRS as WKT or as any string GDAL understands
  std::array<double, 4> bbox{};  // Target bounding box: xmin ymin xmax ymax
  int width{0};
  int height{0};
};

namespace Gdal
{
// Register GDAL drivers once per process
void initialize();

// Read the metadata of an image. The valid time is not stored in the
// file in a reliable way, hence it is passed in by the caller.
ImageInfo readMetadata(const std::string& thePath, const Fmi::DateTime& theTime);

// Warp an image to the requested projection using nearest neighbour
// interpolation. Areas not covered by the image are left transparent.
Image warp(const ImageInfo& theImage, const WarpOptions& theOptions);

}  // namespace Gdal
}  // namespace Satellite
}  // namespace Engine
}  // namespace SmartMet
