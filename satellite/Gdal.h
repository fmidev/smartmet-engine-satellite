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

#include "Image.h"
#include "ImageInfo.h"
#include <string>

namespace SmartMet
{
namespace Engine
{
namespace Satellite
{
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

// Warp the values of an uncoloured image. Nearest neighbour again, since
// interpolating across the edge of the data or across a cloud mask would
// invent values which are not there.
ValueImage warpValues(const ImageInfo& theImage, const WarpOptions& theOptions);

}  // namespace Gdal
}  // namespace Satellite
}  // namespace Engine
}  // namespace SmartMet
