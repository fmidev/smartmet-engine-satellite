#include "Engine.h"
#include <macgyver/DateTime.h>
#include <macgyver/StringConversion.h>
#include <boost/algorithm/string/join.hpp>
#include <regression/tframe.h>
#include <spine/Options.h>
#include <spine/Reactor.h>
#include <fmt/format.h>
#include <iostream>
#include <chrono>
#include <filesystem>
#include <functional>
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
const ProductKey cog_product = {"goes-east", "truecolor"};
const ProductKey geos_product = {"meteosat", "ir108"};
const ProductKey float_product = {"meteosat", "ctth_tempe"};
const ProductKey series_product = {"meteosat", "fog_rgb"};
const ProductKey livescan_product = {"testsat", "livescan"};

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

void producers()
{
  auto names = satellite->producers();

  const std::vector<std::string> expected = {"goes-east", "meteosat", "metop", "testsat"};
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

  // Another satellite must not see them
  auto others = satellite->parameters("goes-east");
  if (others.size() != 1 || others[0] != "truecolor")
    TEST_FAILED("The parameters of goes-east are wrong");

  if (!satellite->parameters("no_such_producer").empty())
    TEST_FAILED("Found parameters for an unknown producer");

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
  } tests[] = {
      {"20260826_1415_Meteosat-10_EPSG3035_natural_with_colorized_ir_clouds.tif",
       "20260826T141500"},
      {"20260820_0840_GOES-19_geos_truecolor_with_ash.tif", "20260820T084000"},
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
    TEST_FAILED(fmt::format("The longitude range {}...{} of the 0 degree service is too wide",
                            bbox[0],
                            bbox[2]));

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
    TEST_FAILED(fmt::format("{} of {} pixels have data, expected less", opaque, image.pixels.size()));

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

  WarpOptions options;
  options.crs = "EPSG:3857";
  options.bbox = {-20037508, -20037508, 20037508, 20037508};
  options.width = 256;
  options.height = 256;

  const auto start = std::chrono::steady_clock::now();
  auto image = satellite->warp(*info, options);
  const auto duration = std::chrono::steady_clock::now() - start;

  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

  if (opaque_pixels(image) == 0)
    TEST_FAILED("The overview tile is fully transparent");

  // Reading the 7633x8313 image at full resolution takes seconds. Using
  // the overviews it takes tens of milliseconds.
  if (ms > 500)
    TEST_FAILED(fmt::format(
        "Warping a whole Earth tile took {} ms, the overviews are probably not used", ms));

  std::cout << fmt::format(" ({} ms)", ms) << std::flush;

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void warp_uncoloured_data_fails()
{
  auto info = find_image(float_product, {}, Fmi::TimeDuration(0, 0, 0));
  if (!info)
    TEST_FAILED("No image available for '" + name_of(float_product) + "'");

  WarpOptions options;
  options.crs = "EPSG:3035";
  options.bbox = {2244000, 800000, 6300000, 5820000};
  options.width = 100;
  options.height = 100;

  try
  {
    satellite->warp(*info, options);
    TEST_FAILED("Warping uncoloured data should have failed");
  }
  catch (const std::exception&)
  {
    // Expected: the error message tells the user the data is not supported
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
      {"256x256   whole Earth COG   ", cog_product, {-20037508, -20037508, 20037508, 20037508}, 256},
      {"512x512   geos full disc    ", geos_product, {-20037508, -20037508, 20037508, 20037508}, 512}};

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

  if (!wait_for([]() { return satellite->imageCount(livescan_product.first, livescan_product.second) == 1; }))
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

  if (!wait_for([]() { return satellite->imageCount(livescan_product.first, livescan_product.second) == 0; }))
    TEST_FAILED("The deleted file was not forgotten within 20 seconds");

  TEST_PASSED();
}

// ----------------------------------------------------------------------

class tests : public tframe::tests
{
  virtual const char* error_message_prefix() const { return "\n\t"; }

  void test()
  {
    TEST(producers);
    TEST(menu_parameters);
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
    TEST(warp_uncoloured_data_fails);
    TEST(live_scan);
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
