// ======================================================================
/*!
 * \brief GDAL access to satellite images
 */
// ======================================================================

#include "Gdal.h"
#include <fmt/format.h>
#include <macgyver/Exception.h>
#include <macgyver/Hash.h>
#include <algorithm>
#include <cmath>
#include <cpl_conv.h>
#include <cpl_string.h>
#include <filesystem>
#include <gdal.h>
#include <gdal_alg.h>
#include <gdalwarper.h>
#include <memory>
#include <mutex>
#include <ogr_spatialref.h>

namespace SmartMet
{
namespace Engine
{
namespace Satellite
{
std::string to_string(BandModel model)
{
  switch (model)
  {
    case BandModel::RGBA:
      return "RGBA";
    case BandModel::RGB:
      return "RGB";
    case BandModel::GrayAlpha:
      return "GrayAlpha";
    case BandModel::Gray:
      return "Gray";
    case BandModel::Float:
      return "Float";
  }
  return "Unknown";
}

namespace Gdal
{
namespace
{
// Maximum error of the approximated coordinate transformation in pixels.
// The gdalwarp utility uses the same value by default.
const double itsErrorThreshold = 0.125;

// Parsing WKT reads the PROJ database, and doing so from several threads
// simultaneously has caused both deadlocks and heap corruption in the
// past. Serializing the parsing is cheap compared to the warping itself,
// which is left fully parallel.

std::mutex& proj_mutex()
{
  static std::mutex mutex;
  return mutex;
}

// Automatic closing of GDAL resources

struct DatasetCloser
{
  void operator()(GDALDatasetH ds) const
  {
    if (ds != nullptr)
      GDALClose(ds);
  }
};

using DatasetPtr = std::unique_ptr<void, DatasetCloser>;

struct TransformerDestroyer
{
  void operator()(void* transformer) const
  {
    if (transformer != nullptr)
      GDALDestroyGenImgProjTransformer(transformer);
  }
};

using TransformerPtr = std::unique_ptr<void, TransformerDestroyer>;

// ----------------------------------------------------------------------
/*!
 * \brief Deduce how the bands of the image should be mapped to RGBA
 */
// ----------------------------------------------------------------------

void deduce_band_model(GDALDatasetH ds, ImageInfo& info)
{
  info.bands = GDALGetRasterCount(ds);

  if (info.bands < 1)
    throw Fmi::Exception(BCP, "The image has no raster bands");

  auto* band1 = GDALGetRasterBand(ds, 1);
  auto type = GDALGetRasterDataType(band1);

  if (type != GDT_Byte)
  {
    // Uncoloured data. Recognized so that a clear error message can be
    // given, but not supported yet.
    info.model = BandModel::Float;
    info.alphaband = 0;
    return;
  }

  // Find an alpha band. Satellite composites store it as the last band,
  // but trust the colour interpretation when it is available.
  info.alphaband = 0;
  for (int i = 1; i <= info.bands; i++)
  {
    if (GDALGetRasterColorInterpretation(GDALGetRasterBand(ds, i)) == GCI_AlphaBand)
    {
      info.alphaband = i;
      break;
    }
  }

  switch (info.bands)
  {
    case 1:
      info.model = BandModel::Gray;
      break;
    case 2:
      // Gray + alpha. Assume the second band is the alpha channel even if
      // the colour interpretation is missing.
      info.model = BandModel::GrayAlpha;
      if (info.alphaband == 0)
        info.alphaband = 2;
      break;
    case 3:
      info.model = BandModel::RGB;
      break;
    default:
      info.model = BandModel::RGBA;
      if (info.alphaband == 0)
        info.alphaband = 4;
      break;
  }

  // A per dataset mask band can substitute for a missing alpha band
  int flags = GDALGetMaskFlags(band1);
  info.maskband = ((flags & GMF_PER_DATASET) != 0);
}

// ----------------------------------------------------------------------
/*!
 * \brief Estimate the WGS84 bounding box of the image
 *
 * The corners of a geostationary full disc image are not on the Earth,
 * hence transforming just the corners is not enough. A grid of points
 * is transformed instead and the failures are ignored.
 */
// ----------------------------------------------------------------------

std::optional<std::array<double, 4>> estimate_bbox(const ImageInfo& info)
{
  if (info.wkt.empty())
    return {};

  OGRSpatialReference source;
  OGRSpatialReference wgs84;
  std::unique_ptr<OGRCoordinateTransformation> transformation;

  {
    std::lock_guard<std::mutex> lock(proj_mutex());

    if (source.SetFromUserInput(info.wkt.c_str()) != OGRERR_NONE)
      return {};
    if (wgs84.SetFromUserInput("EPSG:4326") != OGRERR_NONE)
      return {};

    source.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    wgs84.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    transformation.reset(OGRCreateCoordinateTransformation(&source, &wgs84));
  }

  if (!transformation)
    return {};

  // Sample the image in projected coordinates
  const int steps = 64;
  std::vector<double> xs;
  std::vector<double> ys;
  xs.reserve((steps + 1) * (steps + 1));
  ys.reserve((steps + 1) * (steps + 1));

  for (int j = 0; j <= steps; j++)
  {
    for (int i = 0; i <= steps; i++)
    {
      const double px = info.width * static_cast<double>(i) / steps;
      const double py = info.height * static_cast<double>(j) / steps;
      xs.push_back(info.geotransform[0] + px * info.geotransform[1] + py * info.geotransform[2]);
      ys.push_back(info.geotransform[3] + px * info.geotransform[4] + py * info.geotransform[5]);
    }
  }

  std::vector<int> ok(xs.size(), FALSE);
  transformation->Transform(static_cast<int>(xs.size()), xs.data(), ys.data(), nullptr, ok.data());

  double minx = 180;
  double miny = 90;
  double maxx = -180;
  double maxy = -90;
  bool found = false;

  for (std::size_t i = 0; i < xs.size(); i++)
  {
    if (ok[i] == FALSE || std::isnan(xs[i]) || std::isnan(ys[i]))
      continue;
    if (std::abs(xs[i]) > 180 || std::abs(ys[i]) > 90)
      continue;

    minx = std::min(minx, xs[i]);
    miny = std::min(miny, ys[i]);
    maxx = std::max(maxx, xs[i]);
    maxy = std::max(maxy, ys[i]);
    found = true;
  }

  if (!found || minx >= maxx || miny >= maxy)
    return {};

  return std::array<double, 4>{minx, miny, maxx, maxy};
}

// ----------------------------------------------------------------------
/*!
 * \brief Choose the overview level to read the source image from
 *
 * Cloud optimized GeoTIFF files contain a pyramid of downscaled images.
 * Reading a zoomed out view from a suitable overview instead of the full
 * resolution data is what makes serving large images fast, but the GDAL
 * warper does not choose the overview by itself.
 *
 * Returns -1 for the full resolution image.
 */
// ----------------------------------------------------------------------

int select_overview(GDALDatasetH src, void* transformer, const WarpOptions& options)
{
  auto* band = GDALGetRasterBand(src, 1);
  const int levels = GDALGetOverviewCount(band);
  if (levels <= 0)
    return -1;

  // Transform a grid of target pixel coordinates to source pixel
  // coordinates and estimate how many source pixels one target pixel
  // covers. Using the median of the local estimates avoids being misled
  // by the areas outside the image.

  const int steps = 8;
  std::vector<double> xs;
  std::vector<double> ys;
  std::vector<double> zs;

  for (int j = 0; j <= steps; j++)
    for (int i = 0; i <= steps; i++)
    {
      xs.push_back(options.width * static_cast<double>(i) / steps);
      ys.push_back(options.height * static_cast<double>(j) / steps);
      zs.push_back(0);
    }

  std::vector<int> ok(xs.size(), FALSE);
  GDALGenImgProjTransform(
      transformer, TRUE, static_cast<int>(xs.size()), xs.data(), ys.data(), zs.data(), ok.data());

  const auto index = [steps](int i, int j) { return j * (steps + 1) + i; };

  std::vector<double> scales;

  for (int j = 0; j <= steps; j++)
  {
    for (int i = 0; i < steps; i++)
    {
      const auto a = index(i, j);
      const auto b = index(i + 1, j);
      if (ok[a] == FALSE || ok[b] == FALSE)
        continue;
      const double dx = xs[b] - xs[a];
      const double dy = ys[b] - ys[a];
      const double dist = std::sqrt(dx * dx + dy * dy);
      const double target = options.width / static_cast<double>(steps);
      if (target > 0 && std::isfinite(dist))
        scales.push_back(dist / target);
    }
  }

  for (int j = 0; j < steps; j++)
  {
    for (int i = 0; i <= steps; i++)
    {
      const auto a = index(i, j);
      const auto b = index(i, j + 1);
      if (ok[a] == FALSE || ok[b] == FALSE)
        continue;
      const double dx = xs[b] - xs[a];
      const double dy = ys[b] - ys[a];
      const double dist = std::sqrt(dx * dx + dy * dy);
      const double target = options.height / static_cast<double>(steps);
      if (target > 0 && std::isfinite(dist))
        scales.push_back(dist / target);
    }
  }

  if (scales.empty())
    return -1;

  auto middle = scales.begin() + scales.size() / 2;
  std::nth_element(scales.begin(), middle, scales.end());
  const double scale = *middle;

  if (scale <= 1)
    return -1;  // Zoomed in: full resolution is needed

  // Choose the coarsest overview which is still at least as detailed as
  // the request. Overviews are not required to be powers of two, hence
  // the actual sizes are compared.

  const int width = GDALGetRasterXSize(src);
  int best = -1;
  double bestfactor = 1;

  for (int level = 0; level < levels; level++)
  {
    auto* overview = GDALGetOverview(band, level);
    if (overview == nullptr)
      continue;

    const int overview_width = GDALGetRasterBandXSize(overview);
    if (overview_width <= 0)
      continue;

    const double factor = static_cast<double>(width) / overview_width;
    if (factor <= scale && factor > bestfactor)
    {
      best = level;
      bestfactor = factor;
    }
  }

  return best;
}

}  // namespace

// ----------------------------------------------------------------------

void initialize()
{
  static std::once_flag flag;
  std::call_once(flag, []() { GDALAllRegister(); });
}

// ----------------------------------------------------------------------
/*!
 * \brief Read the metadata of an image
 */
// ----------------------------------------------------------------------

ImageInfo readMetadata(const std::string& thePath, const Fmi::DateTime& theTime)
{
  try
  {
    initialize();

    DatasetPtr ds(
        GDALOpenEx(thePath.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    if (!ds)
      throw Fmi::Exception(BCP, "Failed to open the image")
          .addParameter("Reason", CPLGetLastErrorMsg());

    ImageInfo info;
    info.path = thePath;
    info.time = theTime;
    info.width = GDALGetRasterXSize(ds.get());
    info.height = GDALGetRasterYSize(ds.get());

    if (info.width <= 0 || info.height <= 0)
      throw Fmi::Exception(BCP, "The image has no pixels");

    if (GDALGetGeoTransform(ds.get(), info.geotransform.data()) != CE_None)
      throw Fmi::Exception(BCP, "The image has no geotransform");

    const char* wkt = GDALGetProjectionRef(ds.get());
    if (wkt != nullptr)
      info.wkt = wkt;
    if (info.wkt.empty())
      throw Fmi::Exception(BCP, "The image has no coordinate reference system");

    deduce_band_model(ds.get(), info);

    // The identity of the image for hashing purposes. The file name
    // contains the valid time and the files are never modified in place,
    // but the size and the modification time are included to be safe.
    std::error_code error;
    const auto size = std::filesystem::file_size(thePath, error);
    const auto mtime = std::filesystem::last_write_time(thePath, error);

    info.hash = Fmi::hash_value(thePath);
    Fmi::hash_combine(info.hash, Fmi::hash_value(static_cast<std::size_t>(size)));
    Fmi::hash_combine(info.hash,
                      Fmi::hash_value(static_cast<std::size_t>(mtime.time_since_epoch().count())));

    info.bbox = estimate_bbox(info);

    return info;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Failed to read satellite image metadata")
        .addParameter("Path", thePath);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Warp an image to the requested projection
 */
// ----------------------------------------------------------------------

Image warp(const ImageInfo& theImage, const WarpOptions& theOptions)
{
  try
  {
    initialize();

    if (theOptions.width <= 0 || theOptions.height <= 0)
      throw Fmi::Exception(BCP, "The requested image size must be positive");

    if (theImage.model == BandModel::Float)
      throw Fmi::Exception(BCP, "Uncoloured satellite data is not supported yet")
          .addParameter("Path", theImage.path);

    Image result;
    result.width = theOptions.width;
    result.height = theOptions.height;
    result.pixels.resize(static_cast<std::size_t>(theOptions.width) * theOptions.height, 0);

    // Target CRS
    std::string dst_wkt;
    {
      std::lock_guard<std::mutex> lock(proj_mutex());
      OGRSpatialReference dst_srs;
      if (dst_srs.SetFromUserInput(theOptions.crs.c_str()) != OGRERR_NONE)
        throw Fmi::Exception(BCP, "Failed to parse the target projection")
            .addParameter("CRS", theOptions.crs);
      char* tmp = nullptr;
      dst_srs.exportToWkt(&tmp);
      if (tmp != nullptr)
      {
        dst_wkt = tmp;
        CPLFree(tmp);
      }
    }

    if (dst_wkt.empty())
      throw Fmi::Exception(BCP, "Failed to export the target projection")
          .addParameter("CRS", theOptions.crs);

    // The target of the warp is the ARGB buffer of the result. The bands
    // are mapped so that GDAL writes the bytes directly in the order the
    // WMS plugin expects: on a little endian machine an ARGB integer is
    // stored as B, G, R, A.
    auto* memdriver = GDALGetDriverByName("MEM");
    if (memdriver == nullptr)
      throw Fmi::Exception(BCP, "The GDAL MEM driver is not available");

    DatasetPtr dst(
        GDALCreate(memdriver, "", theOptions.width, theOptions.height, 0, GDT_Byte, nullptr));
    if (!dst)
      throw Fmi::Exception(BCP, "Failed to create the target image");

    const int pixeloffset = 4;
    const int lineoffset = 4 * theOptions.width;
    auto* base = reinterpret_cast<unsigned char*>(result.pixels.data());

    // Band order: red, green, blue, alpha
    const std::array<int, 4> byteoffsets{2, 1, 0, 3};

    for (int band = 0; band < 4; band++)
    {
      char** options = nullptr;
      options = CSLSetNameValue(
          options,
          "DATAPOINTER",
          fmt::format("{}", reinterpret_cast<std::uintptr_t>(base + byteoffsets[band])).c_str());
      options = CSLSetNameValue(options, "PIXELOFFSET", fmt::format("{}", pixeloffset).c_str());
      options = CSLSetNameValue(options, "LINEOFFSET", fmt::format("{}", lineoffset).c_str());

      const auto err = GDALAddBand(dst.get(), GDT_Byte, options);
      CSLDestroy(options);

      if (err != CE_None)
        throw Fmi::Exception(BCP, "Failed to add a band to the target image");
    }

    GDALSetRasterColorInterpretation(GDALGetRasterBand(dst.get(), 4), GCI_AlphaBand);

    // Geotransform of the target: the bounding box maps to the whole image
    const double dx = (theOptions.bbox[2] - theOptions.bbox[0]) / theOptions.width;
    const double dy = (theOptions.bbox[3] - theOptions.bbox[1]) / theOptions.height;

    if (dx <= 0 || dy <= 0)
      throw Fmi::Exception(BCP, "The requested bounding box is empty");

    std::array<double, 6> dst_geotransform{theOptions.bbox[0], dx, 0, theOptions.bbox[3], 0, -dy};

    if (GDALSetGeoTransform(dst.get(), dst_geotransform.data()) != CE_None)
      throw Fmi::Exception(BCP, "Failed to set the target geotransform");

    if (GDALSetProjection(dst.get(), dst_wkt.c_str()) != CE_None)
      throw Fmi::Exception(BCP, "Failed to set the target projection");

    // Open the source image and choose the overview level to use
    DatasetPtr src(GDALOpenEx(
        theImage.path.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    if (!src)
      throw Fmi::Exception(BCP, "Failed to open the image")
          .addParameter("Reason", CPLGetLastErrorMsg());

    int overview = -1;
    {
      TransformerPtr probe;
      {
        std::lock_guard<std::mutex> lock(proj_mutex());
        probe.reset(GDALCreateGenImgProjTransformer2(src.get(), dst.get(), nullptr));
      }
      if (probe)
        overview = select_overview(src.get(), probe.get(), theOptions);
    }

    if (overview >= 0)
    {
      char** openoptions = nullptr;
      openoptions =
          CSLSetNameValue(openoptions, "OVERVIEW_LEVEL", fmt::format("{}", overview).c_str());
      DatasetPtr overview_ds(GDALOpenEx(
          theImage.path.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY, nullptr, openoptions, nullptr));
      CSLDestroy(openoptions);

      // Fall back to the full resolution image if the overview cannot be
      // opened for some reason
      if (overview_ds)
        src = std::move(overview_ds);
    }

    // Warp
    auto* warpoptions = GDALCreateWarpOptions();

    warpoptions->hSrcDS = src.get();
    warpoptions->hDstDS = dst.get();
    warpoptions->eResampleAlg = GRA_NearestNeighbour;
    warpoptions->eWorkingDataType = GDT_Byte;

    // Colour bands. A gray image is expanded to gray, gray, gray.
    const bool gray = (theImage.model == BandModel::Gray || theImage.model == BandModel::GrayAlpha);

    warpoptions->nBandCount = 3;
    warpoptions->panSrcBands = static_cast<int*>(CPLMalloc(3 * sizeof(int)));
    warpoptions->panDstBands = static_cast<int*>(CPLMalloc(3 * sizeof(int)));
    for (int i = 0; i < 3; i++)
    {
      warpoptions->panSrcBands[i] = gray ? 1 : i + 1;
      warpoptions->panDstBands[i] = i + 1;
    }

    // Alpha handling. The target always has an alpha band, and GDAL
    // fills it with 255 for the pixels covered by the source image.
    warpoptions->nSrcAlphaBand = theImage.alphaband;
    warpoptions->nDstAlphaBand = 4;

    // Everything outside the source image stays transparent
    warpoptions->papszWarpOptions =
        CSLSetNameValue(warpoptions->papszWarpOptions, "INIT_DEST", "0");

    {
      std::lock_guard<std::mutex> lock(proj_mutex());
      warpoptions->pTransformerArg =
          GDALCreateGenImgProjTransformer2(src.get(), dst.get(), nullptr);
    }

    if (warpoptions->pTransformerArg == nullptr)
    {
      GDALDestroyWarpOptions(warpoptions);
      throw Fmi::Exception(BCP, "Failed to create the coordinate transformation")
          .addParameter("Path", theImage.path);
    }

    warpoptions->pfnTransformer = GDALGenImgProjTransform;

    // Transforming every pixel exactly means calling PROJ for every pixel,
    // which dominates the cost of a nearest neighbour warp. Approximating
    // the transformation to within a fraction of a pixel is what the
    // gdalwarp utility does by default, and is far cheaper.
    void* approx = GDALCreateApproxTransformer(
        warpoptions->pfnTransformer, warpoptions->pTransformerArg, itsErrorThreshold);

    if (approx != nullptr)
    {
      // The approximating transformer now owns the exact one, hence
      // GDALDestroyTransformer below releases both
      GDALApproxTransformerOwnsSubtransformer(approx, TRUE);
      warpoptions->pTransformerArg = approx;
      warpoptions->pfnTransformer = GDALApproxTransform;
    }

    CPLErr err = CE_None;
    {
      GDALWarpOperation operation;
      err = operation.Initialize(warpoptions);
      if (err == CE_None)
        err = operation.ChunkAndWarpImage(0, 0, theOptions.width, theOptions.height);
    }

    GDALDestroyTransformer(warpoptions->pTransformerArg);
    warpoptions->pTransformerArg = nullptr;
    GDALDestroyWarpOptions(warpoptions);

    if (err != CE_None)
      throw Fmi::Exception(BCP, "Failed to warp the image")
          .addParameter("Path", theImage.path)
          .addParameter("Reason", CPLGetLastErrorMsg());

    return result;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Failed to warp a satellite image")
        .addParameter("Path", theImage.path);
  }
}

}  // namespace Gdal
}  // namespace Satellite
}  // namespace Engine
}  // namespace SmartMet
