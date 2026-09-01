// ======================================================================
/*!
 * \brief Tests of a disabled satellite engine
 *
 * The engine is loaded by a real Reactor from a configuration which sets
 * 'disabled', which is how a server keeps the engine listed on a machine
 * with no image directories. What the Reactor then hands out is an
 * object of the API base class: it starts, it stops, and every request
 * for imagery fails with an exception the WMS plugin turns into an error
 * response.
 */
// ======================================================================

#include "Engine.h"
#include <regression/tframe.h>
#include <spine/Options.h>
#include <spine/Reactor.h>
#include <functional>
#include <iostream>
#include <string>

std::shared_ptr<SmartMet::Engine::Satellite::Engine> satellite;

namespace Tests
{
using namespace SmartMet::Engine::Satellite;

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
    if (!throws_unavailable([&]() { (void)satellite->call; }, message))                         \
      TEST_FAILED(std::string(#call) + " did not report the engine as unavailable, but gave " + \
                  message);                                                                     \
  }

// ----------------------------------------------------------------------
/*!
 * \brief A disabled engine must still start
 *
 * The point of disabling instead of unloading is that the server starts
 * normally, hence the engine must reach the ready state.
 */
// ----------------------------------------------------------------------

void engine_starts()
{
  if (!satellite)
    TEST_FAILED("The Reactor did not create a satellite engine at all");

  if (!satellite->ready())
    TEST_FAILED("A disabled engine should still become ready");

  TEST_PASSED();
}

// ----------------------------------------------------------------------
/*!
 * \brief A disabled engine must be the base class, not the implementation
 *
 * Checked through the behaviour rather than through the type, since the
 * implementation class is internal to the engine library.
 */
// ----------------------------------------------------------------------

void queries_fail()
{
  CHECK_UNAVAILABLE(producers());
  CHECK_UNAVAILABLE(parameters("meteosat"));
  CHECK_UNAVAILABLE(hasProducer("meteosat"));
  CHECK_UNAVAILABLE(hasProduct("meteosat", "natural"));
  CHECK_UNAVAILABLE(productInfo("meteosat", "natural"));
  CHECK_UNAVAILABLE(times("meteosat", "natural"));
  CHECK_UNAVAILABLE(latestTime("meteosat", "natural"));
  CHECK_UNAVAILABLE(imageCount("meteosat", "natural"));
  CHECK_UNAVAILABLE(find("meteosat", "natural", {}, Fmi::TimeDuration(0, 0, 0)));
  TEST_PASSED();
}

// ----------------------------------------------------------------------

void warping_fails()
{
  const ImageInfo image;
  const WarpOptions options;

  CHECK_UNAVAILABLE(warp(image, options));
  CHECK_UNAVAILABLE(warpValues(image, options));
  TEST_PASSED();
}

// ----------------------------------------------------------------------

class tests : public tframe::tests
{
  virtual const char* error_message_prefix() const { return "\n\t"; }

  void test()
  {
    TEST(engine_starts);
    TEST(queries_fail);
    TEST(warping_fails);
  }

};  // class tests

}  // namespace Tests

int main()
{
  SmartMet::Spine::Options opts;
  opts.configfile = "cnf/reactor_disabled.conf";
  opts.parseConfig();

  SmartMet::Spine::Reactor reactor(opts);
  reactor.init();

  satellite = reactor.getEngine<SmartMet::Engine::Satellite::Engine>("Satellite", nullptr);

  std::cout << std::endl
            << "Disabled satellite engine tester" << std::endl
            << "================================" << std::endl;
  Tests::tests t;
  auto result = t.run();

  satellite.reset();
  reactor.shutdown();

  return result;
}
