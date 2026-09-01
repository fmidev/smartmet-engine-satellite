#include "Engine.h"
// The tests reach into the internals as well: the engine is built here,
// so the headers which are not installed are available too.
#include "EngineImpl.h"
#include "Product.h"
#include "Scanner.h"
#include <boost/algorithm/string/join.hpp>
#include <fmt/format.h>
#include <macgyver/DateTime.h>
#include <macgyver/StringConversion.h>
#include <regression/tframe.h>
#include <spine/Options.h>
#include <spine/Reactor.h>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <thread>
#include <vector>

using namespace std;

std::shared_ptr<SmartMet::Engine::Satellite::Engine> satellite;

namespace Tests
{
using namespace SmartMet::Engine::Satellite;

// The producers of the test configuration

// Products are addressed by a producer and a parameter
const ProductKey rgba_product = {"meteosat", "natural"};
const ProductKey grayalpha_product = {"metop", "ir108"};
const ProductKey cog_product = {"meteosat_eckert", "wv73"};
const ProductKey geos_product = {"meteosat", "ir108"};
const ProductKey float_product = {"meteosat", "ctth_tempe"};
const ProductKey series_product = {"meteosat", "fog_rgb"};
const ProductKey livescan_product = {"testsat", "livescan"};
const ProductKey livescan_other_product = {"testsat", "livescan_other"};

// These two share a directory and one name is a prefix of the other
const ProductKey shared_dir_a = {"metop", "ir108"};
const ProductKey shared_dir_b = {"metop", "vis06"};

std::string name_of(const ProductKey& key)
{
  return key.first + "/" + key.second;
}

// Shorthands so that the tests read the same as before
ImageInfoPtr find_image(const ProductKey& key,
                        const std::optional<Fmi::DateTime>& time,
                        const Fmi::TimeDuration& tolerance)
{
  return satellite->find(key.first, key.second, time, tolerance);
}

std::vector<Fmi::DateTime> times_of(const ProductKey& key)
{
  return satellite->times(key.first, key.second);
}

// ----------------------------------------------------------------------
/*!
 * \brief An empty configuration file must give empty listings, not errors
 *
 * The engine may be enabled in the server before any products are
 * configured. The startup then warns about the missing products, and
 * every query gets the empty answer of its return type.
 */
// ----------------------------------------------------------------------

// init() and shutdown() are protected because the reactor drives them;
// this test drives an instance of its own. It must be an EngineImpl,
// since Engine itself is the API of an engine which is not there.
class BareEngine : public EngineImpl
{
 public:
  using EngineImpl::EngineImpl;
  using EngineImpl::init;
  using EngineImpl::shutdown;
};

void minimal_config()
{
  BareEngine engine("cnf/empty.conf");
  engine.init();

  if (!engine.producers().empty())
    TEST_FAILED("An empty configuration listed producers");
  if (!engine.parameters("meteosat").empty())
    TEST_FAILED("An empty configuration listed parameters");
  if (engine.hasProducer("meteosat"))
    TEST_FAILED("An empty configuration reported a producer to exist");
  if (engine.hasProduct("meteosat", "natural"))
    TEST_FAILED("An empty configuration reported a product to exist");
  if (!engine.times("meteosat", "natural").empty())
    TEST_FAILED("An empty configuration listed times");
  if (!engine.latestTime("meteosat", "natural").is_not_a_date_time())
    TEST_FAILED("An empty configuration reported a latest time");
  if (engine.imageCount("meteosat", "natural") != 0)
    TEST_FAILED("An empty configuration counted images");
  if (engine.find("meteosat", "natural", {}, Fmi::TimeDuration(0, 0, 0)))
    TEST_FAILED("An empty configuration found an image");

  engine.shutdown();

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void producers()
{
  auto names = satellite->producers();

  const std::vector<std::string> expected = {"meteosat", "meteosat_eckert", "metop", "testsat"};
  if (names != expected)
    TEST_FAILED("Expected the producers " + boost::algorithm::join(expected, ",") + ", got " +
                boost::algorithm::join(names, ","));

  if (!satellite->hasProducer("meteosat"))
    TEST_FAILED("Producer 'meteosat' is missing");

  if (satellite->hasProducer("no_such_producer"))
    TEST_FAILED("Reported an unknown producer to exist");

  TEST_PASSED();
}

// ----------------------------------------------------------------------
/*!
 * \brief The parameters of a producer, which is the second menu level
 */
// ----------------------------------------------------------------------

void menu_parameters()
{
  auto params = satellite->parameters("meteosat");

  const std::vector<std::string> expected = {"ctth_tempe", "fog_rgb", "ir108", "natural"};
  if (params != expected)
    TEST_FAILED("Expected the parameters " + boost::algorithm::join(expected, ",") + ", got " +
                boost::algorithm::join(params, ","));

  auto metop = satellite->parameters("metop");
  const std::vector<std::string> metop_expected = {"ir108", "vis06"};
  if (metop != metop_expected)
    TEST_FAILED("Expected the metop parameters " + boost::algorithm::join(metop_expected, ",") +
                ", got " + boost::algorithm::join(metop, ","));

  // Another satellite must not see them
  auto others = satellite->parameters("meteosat_eckert");
  if (others.size() != 1 || others[0] != "wv73")
    TEST_FAILED("The parameters of meteosat_eckert are wrong");

  if (!satellite->parameters("no_such_producer").empty())
    TEST_FAILED("Found parameters for an unknown producer");

  // Two products may share a directory
  auto testsat = satellite->parameters("testsat");
  const std::vector<std::string> testsat_expected = {"livescan", "livescan_other"};
  if (testsat != testsat_expected)
    TEST_FAILED("Expected the testsat parameters " + boost::algorithm::join(testsat_expected, ",") +
                ", got " + boost::algorithm::join(testsat, ","));

  // The same parameter name may belong to several satellites
  if (!satellite->hasProduct("meteosat", "ir108") || !satellite->hasProduct("metop", "ir108"))
    TEST_FAILED("The ir108 parameter is missing from meteosat or metop");

  // But not every combination exists
  if (satellite->hasProduct("metop", "natural"))
    TEST_FAILED("Reported a product which is not configured");
  if (satellite->hasProduct("meteosat", "no_such_parameter"))
    TEST_FAILED("Reported an unknown parameter to exist");

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void times()
{
  for (const auto& product : {rgba_product, grayalpha_product, cog_product, geos_product})
  {
    auto times = times_of(product);

    if (times.empty())
      TEST_FAILED("No times found for product '" + name_of(product) + "'");

    // The times must be sorted and unique
    for (std::size_t i = 1; i < times.size(); i++)
      if (times[i - 1] >= times[i])
        TEST_FAILED("The times of '" + name_of(product) + "' are not sorted");

    if (satellite->latestTime(product.first, product.second) != times.back())
      TEST_FAILED("The latest time of '" + name_of(product) + "' is not the last one");
  }

  if (!satellite->times("no_such_producer", "x").empty())
    TEST_FAILED("Found times for an unknown producer");

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void parse_time()
{
  struct
  {
    std::string filename;
    std::string expected;
  } tests[] = {{"20260826_1415_Meteosat-10_EPSG3035_natural_with_colorized_ir_clouds.tif",
                "20260826T141500"},
               {"20260826_1415_Meteosat-10_geos_wv73.tif", "20260826T141500"},
               {"not_a_satellite_file.tif", ""},
               {"2026082_1415_broken.tif", ""},
               {"20260826x1415_broken.tif", ""},
               {"", ""}};

  for (const auto& test : tests)
  {
    auto result = Scanner::parseTime(test.filename);

    if (test.expected.empty())
    {
      if (!result.is_not_a_date_time())
        TEST_FAILED("Should not have parsed a time from '" + test.filename + "'");
    }
    else
    {
      if (result.is_not_a_date_time())
        TEST_FAILED("Failed to parse a time from '" + test.filename + "'");

      auto str = Fmi::to_iso_string(result);
      if (str != test.expected)
        TEST_FAILED("Parsed '" + str + "' instead of '" + test.expected + "' from '" +
                    test.filename + "'");
    }
  }

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void find()
{
  auto times = times_of(rgba_product);
  if (times.empty())
    TEST_FAILED("No images available for '" + name_of(rgba_product) + "'");

  // The latest image
  auto latest = find_image(rgba_product, {}, Fmi::TimeDuration(0, 0, 0));
  if (!latest)
    TEST_FAILED("Failed to find the latest image");
  if (latest->time != times.back())
    TEST_FAILED("Found the wrong image as the latest one");

  // Exact time
  auto exact = find_image(rgba_product, times.front(), Fmi::TimeDuration(0, 0, 0));
  if (!exact)
    TEST_FAILED("Failed to find an image by its exact time");
  if (exact->time != times.front())
    TEST_FAILED("Found the wrong image with an exact time");

  // A time between the images with a tolerance small enough to fail
  auto missing = find_image(
      rgba_product, times.back() + Fmi::TimeDuration(10, 0, 0), Fmi::TimeDuration(0, 1, 0));
  if (missing)
    TEST_FAILED("Found an image ten hours away with a one minute tolerance");

  // The same time with a tolerance large enough to succeed
  auto found = find_image(
      rgba_product, times.back() + Fmi::TimeDuration(10, 0, 0), Fmi::TimeDuration(24, 0, 0));
  if (!found)
    TEST_FAILED("Failed to find an image with a 24 hour tolerance");
  if (found->time != times.back())
    TEST_FAILED("Found the wrong image with a large tolerance");

  // Unknown producer
  if (satellite->find("no_such_producer", "x", {}, Fmi::TimeDuration(0, 0, 0)))
    TEST_FAILED("Found an image of an unknown producer");

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void metadata()
{
  // RGBA in EPSG:3035

  auto rgba = find_image(rgba_product, {}, Fmi::TimeDuration(0, 0, 0));
  if (!rgba)
    TEST_FAILED("Failed to find an RGBA image");
  if (rgba->model != BandModel::RGBA)
    TEST_FAILED("Expected an RGBA image, got " + to_string(rgba->model));
  if (rgba->bands != 4 || rgba->alphaband != 4)
    TEST_FAILED("Expected four bands with the fourth being alpha");
  if (rgba->width != 2028 || rgba->height != 2510)
    TEST_FAILED(fmt::format("Unexpected image size {}x{}", rgba->width, rgba->height));
  if (rgba->wkt.find("LAEA") == std::string::npos)
    TEST_FAILED("The projection of the RGBA image is not LAEA");
  if (!rgba->bbox)
    TEST_FAILED("Failed to estimate the bounding box of the RGBA image");
  if (rgba->hash == 0)
    TEST_FAILED("The hash of the RGBA image is zero");

  // Gray + alpha

  auto gray = find_image(grayalpha_product, {}, Fmi::TimeDuration(0, 0, 0));
  if (!gray)
    TEST_FAILED("Failed to find a gray+alpha image");
  if (gray->model != BandModel::GrayAlpha)
    TEST_FAILED("Expected a GrayAlpha image, got " + to_string(gray->model));
  if (gray->bands != 2 || gray->alphaband != 2)
    TEST_FAILED("Expected two bands with the second being alpha");

  // The geostationary projection

  auto geos = find_image(geos_product, {}, Fmi::TimeDuration(0, 0, 0));
  if (!geos)
    TEST_FAILED("Failed to find a geostationary image");
  if (geos->wkt.find("Geostationary") == std::string::npos)
    TEST_FAILED("The projection of the geostationary image is not geostationary");

  // The bounding box of a full disc image cannot be deduced from the
  // corners, since they are not on the Earth
  if (!geos->bbox)
    TEST_FAILED("Failed to estimate the bounding box of the geostationary image");

  const auto& bbox = *geos->bbox;
  if (bbox[0] < -90 || bbox[2] > 90)
    TEST_FAILED(fmt::format(
        "The longitude range {}...{} of the 0 degree service is too wide", bbox[0], bbox[2]));

  // Uncoloured data must be recognized

  auto uncoloured = find_image(float_product, {}, Fmi::TimeDuration(0, 0, 0));
  if (!uncoloured)
    TEST_FAILED("Failed to find an uncoloured image");
  if (uncoloured->model != BandModel::Float)
    TEST_FAILED("Expected a Float image, got " + to_string(uncoloured->model));

  TEST_PASSED();
}

// ----------------------------------------------------------------------
/*!
 * \brief Count the pixels which are not fully transparent
 */
// ----------------------------------------------------------------------

std::size_t opaque_pixels(const Image& image)
{
  std::size_t count = 0;
  for (const auto& pixel : image.pixels)
    if ((pixel >> 24U) != 0)
      count++;
  return count;
}

// ----------------------------------------------------------------------

void warp_same_projection()
{
  auto info = find_image(rgba_product, {}, Fmi::TimeDuration(0, 0, 0));
  if (!info)
    TEST_FAILED("No image available for '" + name_of(rgba_product) + "'");

  // The native bounding box of the EPSG:3035 products
  WarpOptions options;
  options.crs = "EPSG:3035";
  options.bbox = {2244000, 800000, 6300000, 5820000};
  options.width = 400;
  options.height = 500;

  auto image = satellite->warp(*info, options);

  if (image.width != 400 || image.height != 500)
    TEST_FAILED("The warped image has the wrong size");
  if (image.pixels.size() != 200000)
    TEST_FAILED("The warped image has the wrong number of pixels");

  auto opaque = opaque_pixels(image);
  if (opaque == 0)
    TEST_FAILED("The warped image is fully transparent");

  // The satellite covers most of the European window
  if (opaque < image.pixels.size() / 2)
    TEST_FAILED(fmt::format("Only {} of {} pixels have data", opaque, image.pixels.size()));

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void warp_web_mercator()
{
  auto info = find_image(rgba_product, {}, Fmi::TimeDuration(0, 0, 0));
  if (!info)
    TEST_FAILED("No image available for '" + name_of(rgba_product) + "'");

  // A web mercator tile over Finland
  WarpOptions options;
  options.crs = "EPSG:3857";
  options.bbox = {2504688, 8140237, 3130860, 8766409};
  options.width = 256;
  options.height = 256;

  auto image = satellite->warp(*info, options);

  if (opaque_pixels(image) == 0)
    TEST_FAILED("The tile over Finland is fully transparent");

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void warp_outside_data()
{
  auto info = find_image(rgba_product, {}, Fmi::TimeDuration(0, 0, 0));
  if (!info)
    TEST_FAILED("No image available for '" + name_of(rgba_product) + "'");

  // A tile in the Pacific: the European product has no data there, and
  // the result must be transparent rather than an error
  WarpOptions options;
  options.crs = "EPSG:3857";
  options.bbox = {-17000000, -1000000, -16000000, 0};
  options.width = 256;
  options.height = 256;

  auto image = satellite->warp(*info, options);

  auto opaque = opaque_pixels(image);
  if (opaque != 0)
    TEST_FAILED(fmt::format("Expected a transparent tile, {} pixels have data", opaque));

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void warp_gray_alpha()
{
  auto info = find_image(grayalpha_product, {}, Fmi::TimeDuration(0, 0, 0));
  if (!info)
    TEST_FAILED("No image available for '" + name_of(grayalpha_product) + "'");

  // The AVHRR products are single satellite passes: only a diagonal
  // swath of the European window has data, and its location depends on
  // the orbit. Requesting the native area of the image is therefore the
  // only way to be certain that some data is returned.
  WarpOptions options;
  options.crs = info->wkt;
  options.bbox = {info->geotransform[0],
                  info->geotransform[3] + info->height * info->geotransform[5],
                  info->geotransform[0] + info->width * info->geotransform[1],
                  info->geotransform[3]};
  options.width = 256;
  options.height = 256;

  auto image = satellite->warp(*info, options);

  std::size_t colored = 0;
  std::size_t opaque = 0;

  for (const auto& pixel : image.pixels)
  {
    if ((pixel >> 24U) == 0)
      continue;
    opaque++;

    const auto r = (pixel >> 16U) & 0xFFU;
    const auto g = (pixel >> 8U) & 0xFFU;
    const auto b = pixel & 0xFFU;
    if (r != g || g != b)
      colored++;
  }

  if (opaque == 0)
    TEST_FAILED("The gray+alpha tile is fully transparent");

  // A gray image must be expanded to equal red, green and blue values
  if (colored != 0)
    TEST_FAILED(fmt::format("{} pixels of a gray image are not gray", colored));

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void warp_geostationary()
{
  auto info = find_image(geos_product, {}, Fmi::TimeDuration(0, 0, 0));
  if (!info)
    TEST_FAILED("No image available for '" + name_of(geos_product) + "'");

  // The whole disc in web mercator, which is the hardest case: the
  // projection is not defined outside the disc
  WarpOptions options;
  options.crs = "EPSG:3857";
  options.bbox = {-20037508, -20037508, 20037508, 20037508};
  options.width = 512;
  options.height = 512;

  auto image = satellite->warp(*info, options);

  auto opaque = opaque_pixels(image);
  if (opaque == 0)
    TEST_FAILED("The geostationary image warped to nothing");

  // The 0 degree service covers Europe and Africa, not the whole world
  if (opaque > 3 * image.pixels.size() / 4)
    TEST_FAILED(
        fmt::format("{} of {} pixels have data, expected less", opaque, image.pixels.size()));

  TEST_PASSED();
}

// ----------------------------------------------------------------------
/*!
 * \brief Zooming out of a large image must use the overviews
 *
 * Without overview selection a 256x256 tile of the whole Earth would be
 * read from the full resolution image, which is what makes serving large
 * images slow.
 */
// ----------------------------------------------------------------------

void warp_uses_overviews()
{
  auto info = find_image(cog_product, {}, Fmi::TimeDuration(0, 0, 0));
  if (!info)
    TEST_FAILED("Failed to find a cloud optimized GeoTIFF");

  // Zoomed out to the whole Earth: one target pixel covers many source
  // pixels, so an overview must be used. Asserting the level rather than
  // the elapsed time keeps the test honest on a loaded machine.
  WarpOptions options;
  options.crs = "EPSG:3857";
  options.bbox = {-20037508, -20037508, 20037508, 20037508};
  options.width = 256;
  options.height = 256;

  auto zoomed_out = satellite->warp(*info, options);

  if (opaque_pixels(zoomed_out) == 0)
    TEST_FAILED("The overview tile is fully transparent");

  if (zoomed_out.overview < 0)
    TEST_FAILED("A whole Earth tile was read from the full resolution image");

  // Zoomed in far enough that the full resolution image is the right
  // choice: one target pixel is smaller than a source pixel
  const auto& geotransform = info->geotransform;
  const double x = geotransform[0] + info->width * geotransform[1] / 2;
  const double y = geotransform[3] + info->height * geotransform[5] / 2;
  const double halfwidth = 100 * std::abs(geotransform[1]);

  options.crs = info->wkt;
  options.bbox = {x - halfwidth, y - halfwidth, x + halfwidth, y + halfwidth};
  options.width = 256;
  options.height = 256;

  auto zoomed_in = satellite->warp(*info, options);

  if (zoomed_in.overview != -1)
    TEST_FAILED(fmt::format("A zoomed in tile was read from overview {} instead of the image",
                            zoomed_in.overview));

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void warp_values()
{
  auto info = find_image(float_product, {}, Fmi::TimeDuration(0, 0, 0));
  if (!info)
    TEST_FAILED("No image available for '" + name_of(float_product) + "'");

  if (info->model != BandModel::Float)
    TEST_FAILED("The product is not uncoloured data");

  // The NWC SAF products mark missing values with NaN
  if (!info->nodata || !std::isnan(*info->nodata))
    TEST_FAILED(fmt::format("Expected a NaN no data value, got set={} value={}",
                            info->nodata.has_value(),
                            info->nodata ? *info->nodata : 0.0));

  // The native area of the product
  WarpOptions options;
  options.crs = "EPSG:3035";
  options.bbox = {2244000, 800000, 6300000, 5820000};
  options.width = 300;
  options.height = 400;

  auto image = satellite->warpValues(*info, options);

  if (image.width != 300 || image.height != 400)
    TEST_FAILED("The warped image has the wrong size");
  if (image.values.size() != 120000)
    TEST_FAILED("The warped image has the wrong number of values");

  std::size_t valid = 0;
  float smallest = std::numeric_limits<float>::max();
  float largest = -std::numeric_limits<float>::max();

  for (const auto& value : image.values)
  {
    if (std::isnan(value))
      continue;
    valid++;
    smallest = std::min(smallest, value);
    largest = std::max(largest, value);
  }

  if (valid == 0)
    TEST_FAILED("All the values are missing");

  // Cloud top temperatures in Kelvin. The file itself reports a range of
  // 196...330 K, and nearest neighbour resampling cannot leave that.
  if (smallest < 150 || largest > 350)
    TEST_FAILED(fmt::format(
        "The value range {}...{} is not a cloud top temperature in Kelvin", smallest, largest));

  // Cloud top temperature exists only where there are clouds, so a good
  // part of the area must be missing
  if (valid == image.values.size())
    TEST_FAILED("Every pixel has a value, so the missing ones were not marked");

  TEST_PASSED();
}

// ----------------------------------------------------------------------
/*!
 * \brief Areas outside the image must be missing, not zero
 *
 * Zero is a valid temperature and a valid concentration, so filling the
 * uncovered area with zeroes would draw a cold or clean region which is
 * not there.
 */
// ----------------------------------------------------------------------

void warp_values_outside_data()
{
  auto info = find_image(float_product, {}, Fmi::TimeDuration(0, 0, 0));
  if (!info)
    TEST_FAILED("No image available for '" + name_of(float_product) + "'");

  // The Pacific: the European product has nothing there
  WarpOptions options;
  options.crs = "EPSG:3857";
  options.bbox = {-17000000, -1000000, -16000000, 0};
  options.width = 64;
  options.height = 64;

  auto image = satellite->warpValues(*info, options);

  for (const auto& value : image.values)
    if (!std::isnan(value))
      TEST_FAILED(fmt::format("Expected only missing values, got {}", value));

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void warp_wrong_kind_fails()
{
  WarpOptions options;
  options.crs = "EPSG:3035";
  options.bbox = {2244000, 800000, 6300000, 5820000};
  options.width = 100;
  options.height = 100;

  // Asking for the pixels of uncoloured data
  auto uncoloured = find_image(float_product, {}, Fmi::TimeDuration(0, 0, 0));
  if (!uncoloured)
    TEST_FAILED("No image available for '" + name_of(float_product) + "'");

  try
  {
    satellite->warp(*uncoloured, options);
    TEST_FAILED("Asking for the pixels of uncoloured data should have failed");
  }
  catch (const std::exception&)
  {
  }

  // And asking for the values of a precoloured image
  auto coloured = find_image(rgba_product, {}, Fmi::TimeDuration(0, 0, 0));
  if (!coloured)
    TEST_FAILED("No image available for '" + name_of(rgba_product) + "'");

  try
  {
    satellite->warpValues(*coloured, options);
    TEST_FAILED("Asking for the values of a precoloured image should have failed");
  }
  catch (const std::exception&)
  {
  }

  TEST_PASSED();
}

// ----------------------------------------------------------------------
/*!
 * \brief A producer with several timesteps
 */
// ----------------------------------------------------------------------

void time_series()
{
  auto times = times_of(series_product);

  if (times.size() < 2)
    TEST_FAILED("Expected several images for '" + name_of(series_product) + "', got " +
                Fmi::to_string(times.size()));

  if (satellite->imageCount(series_product.first, series_product.second) != times.size())
    TEST_FAILED("The image count does not match the number of times");

  // Every image must be found by its own time, and must be the one whose
  // time was asked for
  for (const auto& t : times)
  {
    auto image = find_image(series_product, t, Fmi::TimeDuration(0, 0, 0));
    if (!image)
      TEST_FAILED("Failed to find the image of " + Fmi::to_iso_string(t));
    if (image->time != t)
      TEST_FAILED("Found the image of " + Fmi::to_iso_string(image->time) + " instead of " +
                  Fmi::to_iso_string(t));
  }

  // Every image is a separate file, hence the hashes must differ
  std::set<std::size_t> hashes;
  for (const auto& t : times)
    hashes.insert(find_image(series_product, t, Fmi::TimeDuration(0, 0, 0))->hash);

  if (hashes.size() != times.size())
    TEST_FAILED("The images of different times have equal hash values");

  // A time between two images is rounded to the closer one
  const auto& t1 = times[0];
  const auto& t2 = times[1];
  const auto step = (t2 - t1).total_seconds();

  if (step > 0)
  {
    auto just_after = t1 + Fmi::Seconds(step / 4);
    auto image = find_image(series_product, just_after, Fmi::TimeDuration(24, 0, 0));
    if (!image)
      TEST_FAILED("Failed to find an image close to the first one");
    if (image->time != t1)
      TEST_FAILED("Rounded " + Fmi::to_iso_string(just_after) + " to " +
                  Fmi::to_iso_string(image->time) + " instead of " + Fmi::to_iso_string(t1));

    auto just_before = t2 - Fmi::Seconds(step / 4);
    image = find_image(series_product, just_before, Fmi::TimeDuration(24, 0, 0));
    if (!image)
      TEST_FAILED("Failed to find an image close to the second one");
    if (image->time != t2)
      TEST_FAILED("Rounded " + Fmi::to_iso_string(just_before) + " to " +
                  Fmi::to_iso_string(image->time) + " instead of " + Fmi::to_iso_string(t2));
  }

  TEST_PASSED();
}

// ----------------------------------------------------------------------
/*!
 * \brief Report the warping speed of the engine alone
 */
// ----------------------------------------------------------------------

void warp_speed()
{
  struct Case
  {
    std::string label;
    ProductKey product;
    std::array<double, 4> bbox;
    int size;
  };

  const std::vector<Case> cases = {
      {"256x256   EPSG:3857 zoom-in ", rgba_product, {2504688, 8140237, 3130860, 8766409}, 256},
      {"512x512   EPSG:3857 zoom-in ", rgba_product, {2504688, 8140237, 3130860, 8766409}, 512},
      {"1024x1024 EPSG:3857 zoom-in ", rgba_product, {2504688, 8140237, 3130860, 8766409}, 1024},
      {"1024x1024 EPSG:3857 europe  ", rgba_product, {-5000000, 3000000, 6000000, 12000000}, 1024},
      {"256x256   whole Earth COG   ",
       cog_product,
       {-20037508, -20037508, 20037508, 20037508},
       256},
      {"512x512   geos full disc    ",
       geos_product,
       {-20037508, -20037508, 20037508, 20037508},
       512}};

  std::cout << "\n";

  for (const auto& c : cases)
  {
    auto info = satellite->find(c.product.first, c.product.second, {}, Fmi::TimeDuration(0, 0, 0));
    if (!info)
      TEST_FAILED("No image available for '" + name_of(c.product) + "'");

    WarpOptions options;
    options.crs = "EPSG:3857";
    options.bbox = c.bbox;
    options.width = c.size;
    options.height = c.size;

    // The first call warms the page cache, the rest are measured
    satellite->warp(*info, options);

    const int rounds = 5;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < rounds; i++)
      satellite->warp(*info, options);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    const double ms =
        std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() / 1000.0 / rounds;

    std::cout << fmt::format("\t{}  {:7.1f} ms\n", c.label, ms);
  }

  TEST_PASSED();
}

// ----------------------------------------------------------------------
/*!
 * \brief New files must be noticed without restarting the server
 *
 * Satellite images arrive continuously, hence this is the one thing the
 * engine must never get wrong.
 */
// ----------------------------------------------------------------------

void live_scan()
{
  const char* dir = getenv("SATELLITE_SCAN_DIR");
  if (dir == nullptr)
    TEST_FAILED("SATELLITE_SCAN_DIR is not set");

  // The directory starts out empty
  if (satellite->imageCount(livescan_product.first, livescan_product.second) != 0)
    TEST_FAILED("The live scan directory was not empty at the start");

  // Take a real image as the source
  auto source = find_image(series_product, {}, Fmi::TimeDuration(0, 0, 0));
  if (!source)
    TEST_FAILED("No image available for '" + name_of(series_product) + "'");

  const std::filesystem::path target =
      std::filesystem::path(dir) / "20230929_2100_Meteosat-10_fog_rgb_ir.tif";

  std::filesystem::copy_file(
      source->path, target, std::filesystem::copy_options::overwrite_existing);

  // The scan interval of this producer is one second
  const auto wait_for = [](const std::function<bool()>& condition)
  {
    for (int i = 0; i < 100; i++)
    {
      if (condition())
        return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return false;
  };

  if (!wait_for(
          []()
          { return satellite->imageCount(livescan_product.first, livescan_product.second) == 1; }))
  {
    std::filesystem::remove(target);
    TEST_FAILED("The new file was not noticed within 20 seconds");
  }

  // The image must be usable, not merely counted
  auto found = find_image(livescan_product, {}, Fmi::TimeDuration(0, 0, 0));
  if (!found)
    TEST_FAILED("The new image was counted but cannot be found");
  if (Fmi::to_iso_string(found->time) != "20230929T210000")
    TEST_FAILED("The time of the new image is " + Fmi::to_iso_string(found->time));
  if (found->path != target.string())
    TEST_FAILED("The path of the new image is " + found->path);
  if (found->width != source->width || found->model != source->model)
    TEST_FAILED("The metadata of the new image was not read correctly");

  // And a deleted file must disappear
  std::filesystem::remove(target);

  if (!wait_for(
          []()
          { return satellite->imageCount(livescan_product.first, livescan_product.second) == 0; }))
    TEST_FAILED("The deleted file was not forgotten within 20 seconds");

  TEST_PASSED();
}

// ----------------------------------------------------------------------
/*!
 * \brief Two products of one directory must not see each other's files
 *
 * One directory holds all the composites of one instrument, so the file
 * name pattern is what separates the products. The names are not always
 * distinct enough to be careless about: this directory holds ir108,
 * vis06_with_ir108 and vis08_with_ir108, so all three file names end in
 * ir108.tif and a pattern which is not anchored at the end would pick up
 * all three.
 */
// ----------------------------------------------------------------------

void shared_directory()
{
  auto a_times = times_of(shared_dir_a);
  auto b_times = times_of(shared_dir_b);

  if (a_times.empty())
    TEST_FAILED("No images for '" + name_of(shared_dir_a) + "'");
  if (b_times.empty())
    TEST_FAILED("No images for '" + name_of(shared_dir_b) + "'");

  // Collect the files each product accepted
  std::set<std::string> a_paths;
  std::set<std::string> b_paths;

  for (const auto& t : a_times)
    a_paths.insert(find_image(shared_dir_a, t, Fmi::TimeDuration(0, 0, 0))->path);
  for (const auto& t : b_times)
    b_paths.insert(find_image(shared_dir_b, t, Fmi::TimeDuration(0, 0, 0))->path);

  // The same file must never belong to both products
  for (const auto& path : a_paths)
    if (b_paths.count(path) != 0)
      TEST_FAILED("Both products accepted '" + path + "'");

  // And the files must be the ones the names promise
  const auto ends_with = [](const std::string& str, const std::string& tail)
  {
    return str.size() >= tail.size() &&
           str.compare(str.size() - tail.size(), tail.size(), tail) == 0;
  };

  for (const auto& path : a_paths)
    if (!ends_with(path, "_EPSG3035_ir108.tif"))
      TEST_FAILED("'" + name_of(shared_dir_a) + "' accepted '" + path + "'");

  for (const auto& path : b_paths)
    if (!ends_with(path, "_EPSG3035_vis06_with_ir108.tif"))
      TEST_FAILED("'" + name_of(shared_dir_b) + "' accepted '" + path + "'");

  if (a_paths == b_paths)
    TEST_FAILED("The two products of the same directory have identical contents");

  TEST_PASSED();
}

// ----------------------------------------------------------------------
/*!
 * \brief Composites of one directory are noticed independently
 *
 * The images of the composites of one instrument land in the same
 * directory but not at the same moment: a polar orbiter composite may be
 * hours late while a geostationary one arrives every fifteen minutes.
 * Each product is watched separately, so one composite standing still
 * must not delay another, and an arriving image must be credited to the
 * product whose pattern it matches and to no other.
 */
// ----------------------------------------------------------------------

void staggered_updates()
{
  const char* dir = getenv("SATELLITE_SCAN_DIR");
  if (dir == nullptr)
    TEST_FAILED("SATELLITE_SCAN_DIR is not set");

  auto source = find_image(series_product, {}, Fmi::TimeDuration(0, 0, 0));
  if (!source)
    TEST_FAILED("No image available for '" + name_of(series_product) + "'");

  const std::filesystem::path base(dir);
  const auto first = base / "20230929_2100_Meteosat-10_fog_rgb_ir.tif";
  const auto second = base / "20230929_2200_Meteosat-10_other.tif";
  const auto third = base / "20230929_2300_Meteosat-10_fog_rgb_ir.tif";

  const auto cleanup = [&]()
  {
    for (const auto& p : {first, second, third})
      std::filesystem::remove(p);
  };

  const auto count = [](const ProductKey& key)
  { return satellite->imageCount(key.first, key.second); };

  // The scan interval of both products is one second
  const auto wait_for = [](const std::function<bool()>& condition)
  {
    for (int i = 0; i < 100; i++)
    {
      if (condition())
        return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return false;
  };

  const auto fail = [&](const std::string& message)
  {
    cleanup();
    TEST_FAILED(message);
  };

  if (count(livescan_product) != 0 || count(livescan_other_product) != 0)
    TEST_FAILED("The live scan directory was not empty at the start");

  // One composite arrives
  std::filesystem::copy_file(
      source->path, first, std::filesystem::copy_options::overwrite_existing);

  if (!wait_for([&]() { return count(livescan_product) == 1; }))
    fail("The first composite was not noticed");

  if (count(livescan_other_product) != 0)
    fail("The other composite of the same directory saw a file which is not its own");

  // The other composite arrives an hour later
  std::filesystem::copy_file(
      source->path, second, std::filesystem::copy_options::overwrite_existing);

  if (!wait_for([&]() { return count(livescan_other_product) == 1; }))
    fail("The second composite was not noticed");

  if (count(livescan_product) != 1)
    fail("The first composite changed when the second one arrived");

  // The first composite gets another image while the second stands still
  std::filesystem::copy_file(
      source->path, third, std::filesystem::copy_options::overwrite_existing);

  if (!wait_for([&]() { return count(livescan_product) == 2; }))
    fail("The new image of the first composite was not noticed");

  if (count(livescan_other_product) != 1)
    fail("The second composite changed when the first one was updated");

  // Each product must have got exactly its own files
  auto own = find_image(livescan_other_product, {}, Fmi::TimeDuration(0, 0, 0));
  if (!own || own->path != second.string())
    fail("The second composite is serving the wrong file");

  // Removing one composite must not disturb the other
  std::filesystem::remove(second);

  if (!wait_for([&]() { return count(livescan_other_product) == 0; }))
    fail("The deleted image of the second composite was not forgotten");

  if (count(livescan_product) != 2)
    fail("The first composite lost images when the second one was emptied");

  cleanup();

  if (!wait_for([&]() { return count(livescan_product) == 0; }))
    TEST_FAILED("The deleted images of the first composite were not forgotten");

  TEST_PASSED();
}

// ----------------------------------------------------------------------
/*!
 * \brief A file rewritten in place must be noticed
 *
 * The modification time of a directory changes when a file is created,
 * deleted or renamed in it, but not when an existing file is rewritten.
 * The directory monitor can use the directory time to skip scanning, and
 * a scanner relying on that would serve the old pixels of a rewritten
 * file forever, since the image hash which the WMS ETag is built from
 * would never change either. Asking for MODIFY events disables that
 * shortcut, which is why the scanner does so.
 *
 * Note that the file times have a one second resolution, hence the test
 * has to wait before rewriting. In production this case does not arise
 * from a rewrite at all: the images are written under a temporary name
 * and renamed, which the monitor sees as a new file.
 */
// ----------------------------------------------------------------------

void modified_in_place()
{
  const char* dir = getenv("SATELLITE_SCAN_DIR");
  if (dir == nullptr)
    TEST_FAILED("SATELLITE_SCAN_DIR is not set");

  auto times = times_of(series_product);
  if (times.size() < 2)
    TEST_FAILED("Need two source images for '" + name_of(series_product) + "'");

  auto first_source = find_image(series_product, times.front(), Fmi::TimeDuration(0, 0, 0));
  auto second_source = find_image(series_product, times.back(), Fmi::TimeDuration(0, 0, 0));

  const std::filesystem::path target =
      std::filesystem::path(dir) / "20230929_2100_Meteosat-10_fog_rgb_ir.tif";

  const auto cleanup = [&]() { std::filesystem::remove(target); };

  const auto wait_for = [](const std::function<bool()>& condition)
  {
    for (int i = 0; i < 100; i++)
    {
      if (condition())
        return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return false;
  };

  const auto fail = [&](const std::string& message)
  {
    cleanup();
    TEST_FAILED(message);
  };

  // Copy the contents of a file over another, keeping the same inode so
  // that the directory itself is not touched
  const auto overwrite = [](const std::filesystem::path& from, const std::filesystem::path& to)
  {
    std::ifstream in(from, std::ios::binary);
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    out << in.rdbuf();
  };

  std::filesystem::copy_file(
      first_source->path, target, std::filesystem::copy_options::overwrite_existing);

  if (!wait_for(
          [&]()
          { return satellite->imageCount(livescan_product.first, livescan_product.second) == 1; }))
    fail("The new file was not noticed");

  auto before = find_image(livescan_product, {}, Fmi::TimeDuration(0, 0, 0));
  if (!before)
    fail("The new file was counted but cannot be found");
  const auto hash_before = before->hash;

  const auto dirtime_before = std::filesystem::last_write_time(dir);

  // The file times have a one second resolution
  std::this_thread::sleep_for(std::chrono::milliseconds(2000));

  overwrite(second_source->path, target);

  // The point of the test: the directory did not change
  if (std::filesystem::last_write_time(dir) != dirtime_before)
    fail("The test rewrote the file in a way which changed the directory");

  if (!wait_for(
          [&]()
          {
            auto now = find_image(livescan_product, {}, Fmi::TimeDuration(0, 0, 0));
            return now && now->hash != hash_before;
          }))
    fail("The rewritten file was not noticed, so a stale image would be served");

  // And there must still be exactly one image, not two
  if (satellite->imageCount(livescan_product.first, livescan_product.second) != 1)
    fail("The rewritten file was added instead of replaced");

  cleanup();

  if (!wait_for(
          [&]()
          { return satellite->imageCount(livescan_product.first, livescan_product.second) == 0; }))
    TEST_FAILED("The deleted file was not forgotten");

  TEST_PASSED();
}

// ----------------------------------------------------------------------

class tests : public tframe::tests
{
  virtual const char* error_message_prefix() const { return "\n\t"; }

  void test()
  {
    TEST(minimal_config);
    TEST(producers);
    TEST(menu_parameters);
    TEST(shared_directory);
    TEST(times);
    TEST(parse_time);
    TEST(find);
    TEST(time_series);
    TEST(metadata);
    TEST(warp_same_projection);
    TEST(warp_web_mercator);
    TEST(warp_outside_data);
    TEST(warp_gray_alpha);
    TEST(warp_geostationary);
    TEST(warp_uses_overviews);
    TEST(warp_values);
    TEST(warp_values_outside_data);
    TEST(warp_wrong_kind_fails);
    TEST(live_scan);
    TEST(staggered_updates);
    TEST(modified_in_place);
    TEST(warp_speed);
  }

};  // class tests

}  // namespace Tests

int main()
{
  SmartMet::Spine::Options opts;
  opts.configfile = "cnf/reactor.conf";
  opts.parseConfig();

  SmartMet::Spine::Reactor reactor(opts);
  reactor.init();

  satellite = reactor.getEngine<SmartMet::Engine::Satellite::Engine>("Satellite", nullptr);

  cout << endl << "Satellite engine tester" << endl << "=======================" << endl;
  Tests::tests t;
  auto result = t.run();

  satellite.reset();
  reactor.shutdown();

  return result;
}
