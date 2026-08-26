// ======================================================================
/*!
 * \brief Satellite engine configuration implementation
 */
// ======================================================================

#include "Config.h"
#include <fmt/format.h>
#include <macgyver/Exception.h>
#include <spine/ConfigTools.h>

namespace SmartMet
{
namespace Engine
{
namespace Satellite
{
// ----------------------------------------------------------------------
/*!
 * \brief Read and validate the configuration file
 */
// ----------------------------------------------------------------------

Config::Config(const std::string& theFileName)
{
  try
  {
    if (theFileName.empty())
      throw Fmi::Exception(BCP, "Satellite engine configuration file name is empty");

    // Enable @include of files relative to the configuration file
    std::filesystem::path p = theFileName;
    p.remove_filename();
    itsConfig.setIncludeDir(p.c_str());

    itsConfig.readFile(theFileName.c_str());
    Spine::expandVariables(itsConfig);

    std::string rootdir;
    if (!itsConfig.lookupValue("rootdir", rootdir))
      throw Fmi::Exception(BCP, "Setting 'rootdir' is missing from '" + theFileName + "'");
    itsRootDir = rootdir;

    if (!itsConfig.exists("products"))
      throw Fmi::Exception(BCP, "Setting 'products' is missing from '" + theFileName + "'");

    const auto& products = itsConfig.lookup("products");
    if (!products.isGroup())
      throw Fmi::Exception(BCP, "Setting 'products' must be a group in '" + theFileName + "'");

    for (int i = 0; i < products.getLength(); i++)
      parseProduct(products[i]);

    if (itsProducts.empty())
      throw Fmi::Exception(BCP, "No products defined in '" + theFileName + "'");
  }
  catch (const libconfig::ParseException& e)
  {
    throw Fmi::Exception(
        BCP,
        fmt::format("Failed to parse '{}' line {}: {}", theFileName, e.getLine(), e.getError()));
  }
  catch (const libconfig::ConfigException& e)
  {
    throw Fmi::Exception(BCP, fmt::format("Failed to read '{}': {}", theFileName, e.what()));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Satellite engine configuration failed")
        .addParameter("Configuration file", theFileName);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Parse the settings of a single product
 *
 * A product tells which satellite the images come from, which composite
 * of it, and where the files are.
 */
// ----------------------------------------------------------------------

void Config::parseProduct(const libconfig::Setting& theSetting)
{
  Product product;
  product.id = theSetting.getName();

  try
  {
    if (!theSetting.isGroup())
      throw Fmi::Exception(BCP, "Product setting must be a group");

    if (!theSetting.lookupValue("producer", product.producer) || product.producer.empty())
      throw Fmi::Exception(BCP, "Setting 'producer' is missing");

    if (!theSetting.lookupValue("parameter", product.parameter) || product.parameter.empty())
      throw Fmi::Exception(BCP, "Setting 'parameter' is missing");

    std::string directory;
    if (!theSetting.lookupValue("directory", directory))
      throw Fmi::Exception(BCP, "Setting 'directory' is missing");

    // Relative directories are relative to the configured root
    std::filesystem::path dir = directory;
    product.directory = dir.is_absolute() ? dir : itsRootDir / dir;

    if (!theSetting.lookupValue("pattern", product.pattern))
      throw Fmi::Exception(BCP, "Setting 'pattern' is missing");
    product.regex = boost::regex(product.pattern);

    theSetting.lookupValue("title", product.title);
    theSetting.lookupValue("abstract", product.abstract);
    theSetting.lookupValue("refresh_interval_secs", product.refresh_interval_secs);

    if (product.title.empty())
      product.title = product.id;

    // libconfig has no unsigned type, hence the detour
    int max_files = static_cast<int>(product.max_files);
    theSetting.lookupValue("max_files", max_files);
    if (max_files < 0)
      throw Fmi::Exception(BCP, "Setting 'max_files' must not be negative");
    product.max_files = static_cast<std::size_t>(max_files);

    if (product.refresh_interval_secs <= 0)
      throw Fmi::Exception(BCP, "Setting 'refresh_interval_secs' must be positive");

    if (theSetting.exists("keywords"))
    {
      const auto& keywords = theSetting.lookup("keywords");
      if (!keywords.isArray())
        throw Fmi::Exception(BCP, "Setting 'keywords' must be an array");
      for (int i = 0; i < keywords.getLength(); i++)
        product.keywords.emplace_back(static_cast<const char*>(keywords[i]));
    }

    if (theSetting.exists("bbox"))
    {
      const auto& bbox = theSetting.lookup("bbox");
      if (!bbox.isArray() || bbox.getLength() != 4)
        throw Fmi::Exception(BCP, "Setting 'bbox' must be an array of four numbers");

      std::array<double, 4> values{};
      for (int i = 0; i < 4; i++)
        values[i] = static_cast<double>(bbox[i]);

      if (values[0] >= values[2] || values[1] >= values[3])
        throw Fmi::Exception(BCP, "Setting 'bbox' must be [minx, miny, maxx, maxy]");

      product.bbox = values;
    }

    const auto key = make_key(product.producer, product.parameter);

    if (itsProducts.find(key) != itsProducts.end())
      throw Fmi::Exception(BCP, "Producer and parameter pair is already in use")
          .addParameter("Producer", product.producer)
          .addParameter("Parameter", product.parameter);

    itsProducts.insert({key, product});
  }
  catch (const boost::regex_error& e)
  {
    throw Fmi::Exception(BCP, fmt::format("Invalid pattern regex: {}", e.what()))
        .addParameter("Product", product.id)
        .addParameter("Pattern", product.pattern);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Failed to parse satellite product settings")
        .addParameter("Product", product.id);
  }
}

}  // namespace Satellite
}  // namespace Engine
}  // namespace SmartMet
