// ======================================================================
/*!
 * \brief Tests of the engine API alone
 *
 * This program is compiled against the installed headers only and is
 * linked without satellite.so. That is the guard for two promises of the
 * API:
 *
 *  - the installed headers declare nothing which is defined in the
 *    engine library, so a plugin using them loads even when the engine
 *    is not loaded. A broken promise shows up as an unresolved symbol
 *    when this program is linked.
 *
 *  - the base class is a working engine object which merely has no
 *    imagery: every method of it throws instead of returning something
 *    which looks like an answer. That is what makes a disabled engine
 *    turn into an error response rather than into an empty map.
 */
// ======================================================================

#include <engines/satellite/Engine.h>
#include <regression/tframe.h>
#include <functional>
#include <iostream>
#include <string>

using namespace SmartMet::Engine::Satellite;

namespace Tests
{
// The engine of a server which has the satellite engine disabled

Engine engine;

// Check that the call throws, and that it says why

bool throws_unavailable(const std::function<void()>& call, std::string& message)
{
  try
  {
    call();
  }
  catch (const Fmi::Exception& e)
  {
    message = e.what();
    return message.find("Satellite engine not available") != std::string::npos;
  }
  catch (...)
  {
    message = "an exception which is not a Fmi::Exception";
    return false;
  }

  message = "no exception at all";
  return false;
}

#define CHECK_UNAVAILABLE(call)                                                                 \
  {                                                                                             \
    std::string message;                                                                        \
    if (!throws_unavailable([&]() { (void)engine.call; }, message))                             \
      TEST_FAILED(std::string(#call) + " did not report the engine as unavailable, but gave " + \
                  message);                                                                     \
  }

// ----------------------------------------------------------------------
/*!
 * \brief The menu queries of a disabled engine must fail
 */
// ----------------------------------------------------------------------

void menu_queries()
{
  CHECK_UNAVAILABLE(producers());
  CHECK_UNAVAILABLE(parameters("meteosat"));
  CHECK_UNAVAILABLE(hasProducer("meteosat"));
  CHECK_UNAVAILABLE(hasProduct("meteosat", "natural"));
  CHECK_UNAVAILABLE(productInfo("meteosat", "natural"));
  TEST_PASSED();
}

// ----------------------------------------------------------------------
/*!
 * \brief The time queries of a disabled engine must fail
 */
// ----------------------------------------------------------------------

void time_queries()
{
  CHECK_UNAVAILABLE(times("meteosat", "natural"));
  CHECK_UNAVAILABLE(latestTime("meteosat", "natural"));
  CHECK_UNAVAILABLE(imageCount("meteosat", "natural"));
  CHECK_UNAVAILABLE(find("meteosat", "natural", {}, Fmi::TimeDuration(0, 0, 0)));
  TEST_PASSED();
}

// ----------------------------------------------------------------------
/*!
 * \brief Rendering with a disabled engine must fail
 */
// ----------------------------------------------------------------------

void warping()
{
  const ImageInfo image;
  const WarpOptions options;

  CHECK_UNAVAILABLE(warp(image, options));
  CHECK_UNAVAILABLE(warpValues(image, options));
  TEST_PASSED();
}

// ----------------------------------------------------------------------
/*!
 * \brief What the API headers do provide must work without the library
 *
 * to_string is the only free function of the installed headers. It is
 * defined inline for exactly this reason, hence the test is really about
 * the program having linked at all.
 */
// ----------------------------------------------------------------------

void band_model_names()
{
  if (to_string(BandModel::RGBA) != "RGBA")
    TEST_FAILED("BandModel::RGBA should be named RGBA");
  if (to_string(BandModel::GrayAlpha) != "GrayAlpha")
    TEST_FAILED("BandModel::GrayAlpha should be named GrayAlpha");
  if (to_string(BandModel::Float) != "Float")
    TEST_FAILED("BandModel::Float should be named Float");
  TEST_PASSED();
}

// ----------------------------------------------------------------------

class tests : public tframe::tests
{
  virtual const char* error_message_prefix() const { return "\n\t"; }

  void test()
  {
    TEST(menu_queries);
    TEST(time_queries);
    TEST(warping);
    TEST(band_model_names);
  }

};  // class tests

}  // namespace Tests

int main()
{
  std::cout << std::endl
            << "Satellite engine API tester" << std::endl
            << "===========================" << std::endl;
  Tests::tests t;
  return t.run();
}
