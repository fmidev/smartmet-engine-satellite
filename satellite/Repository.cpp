// ======================================================================
/*!
 * \brief Catalog of the available satellite images
 */
// ======================================================================

#include "Repository.h"
#include <fmt/format.h>
#include <macgyver/Exception.h>
#include <set>

namespace SmartMet
{
namespace Engine
{
namespace Satellite
{
namespace
{
std::string describe(const ProductKey& theKey)
{
  return fmt::format("{}/{}", theKey.first, theKey.second);
}
}  // namespace

// ----------------------------------------------------------------------

void Repository::add(const Product& theProduct)
{
  try
  {
    Spine::WriteLock lock(itsMutex);
    Contents contents;
    contents.product = theProduct;
    contents.bbox = theProduct.bbox;  // Configured override, if any
    itsProducts.insert({make_key(theProduct.producer, theProduct.parameter), contents});
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Failed to add a satellite product");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add a scanned image to the catalog
 */
// ----------------------------------------------------------------------

void Repository::insert(const ProductKey& theKey, const ImageInfoPtr& theImage)
{
  try
  {
    Spine::WriteLock lock(itsMutex);

    auto pos = itsProducts.find(theKey);
    if (pos == itsProducts.end())
      return;

    auto& contents = pos->second;
    contents.images[theImage->time] = theImage;

    // The newest image defines the estimated bounding box unless the
    // configuration overrides it
    if (!contents.product.bbox && theImage->time == contents.images.rbegin()->first)
      contents.bbox = theImage->bbox;

    limitSize(contents);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Failed to insert a satellite image")
        .addParameter("Product", describe(theKey));
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Remove a deleted image from the catalog
 */
// ----------------------------------------------------------------------

void Repository::remove(const ProductKey& theKey, const std::string& thePath)
{
  try
  {
    Spine::WriteLock lock(itsMutex);

    auto pos = itsProducts.find(theKey);
    if (pos == itsProducts.end())
      return;

    // Removals are rare, hence a linear search by path is acceptable
    auto& images = pos->second.images;
    for (auto it = images.begin(); it != images.end(); ++it)
    {
      if (it->second->path == thePath)
      {
        images.erase(it);
        return;
      }
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Failed to remove a satellite image")
        .addParameter("Product", describe(theKey));
  }
}

// ----------------------------------------------------------------------

void Repository::limitSize(Contents& theContents)
{
  const auto max_files = theContents.product.max_files;
  if (max_files == 0)
    return;

  auto& images = theContents.images;
  while (images.size() > max_files)
    images.erase(images.begin());
}

// ----------------------------------------------------------------------
/*!
 * \brief The satellites available, sorted and without duplicates
 */
// ----------------------------------------------------------------------

std::vector<std::string> Repository::producers() const
{
  Spine::ReadLock lock(itsMutex);

  std::set<std::string> names;
  for (const auto& product : itsProducts)
    names.insert(product.first.first);

  return {names.begin(), names.end()};
}

// ----------------------------------------------------------------------
/*!
 * \brief The parameters available for one satellite
 *
 * This is what a client needs to build a menu of the composites of one
 * satellite.
 */
// ----------------------------------------------------------------------

std::vector<std::string> Repository::parameters(const std::string& theProducer) const
{
  Spine::ReadLock lock(itsMutex);

  std::vector<std::string> ret;

  // The map is sorted by producer first, hence the parameters of one
  // producer are consecutive and already in order
  for (auto pos = itsProducts.lower_bound(make_key(theProducer, std::string()));
       pos != itsProducts.end() && pos->first.first == theProducer;
       ++pos)
  {
    ret.push_back(pos->first.second);
  }

  return ret;
}

// ----------------------------------------------------------------------

bool Repository::hasProducer(const std::string& theProducer) const
{
  Spine::ReadLock lock(itsMutex);

  auto pos = itsProducts.lower_bound(make_key(theProducer, std::string()));
  return pos != itsProducts.end() && pos->first.first == theProducer;
}

// ----------------------------------------------------------------------

bool Repository::hasProduct(const ProductKey& theKey) const
{
  Spine::ReadLock lock(itsMutex);
  return itsProducts.find(theKey) != itsProducts.end();
}

// ----------------------------------------------------------------------

ProductInfo Repository::productInfo(const ProductKey& theKey) const
{
  try
  {
    Spine::ReadLock lock(itsMutex);

    auto pos = itsProducts.find(theKey);
    if (pos == itsProducts.end())
      throw Fmi::Exception(BCP, "Unknown satellite product '" + describe(theKey) + "'");

    const auto& contents = pos->second;

    ProductInfo info;
    info.producer = contents.product.producer;
    info.parameter = contents.product.parameter;
    info.title = contents.product.title;
    info.abstract = contents.product.abstract;
    info.keywords = contents.product.keywords;
    info.bbox = contents.bbox;
    return info;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Failed to get satellite product information")
        .addParameter("Product", describe(theKey));
  }
}

// ----------------------------------------------------------------------

std::vector<Fmi::DateTime> Repository::times(const ProductKey& theKey) const
{
  Spine::ReadLock lock(itsMutex);

  std::vector<Fmi::DateTime> ret;

  auto pos = itsProducts.find(theKey);
  if (pos == itsProducts.end())
    return ret;

  ret.reserve(pos->second.images.size());
  for (const auto& image : pos->second.images)
    ret.push_back(image.first);

  return ret;
}

// ----------------------------------------------------------------------

Fmi::DateTime Repository::latestTime(const ProductKey& theKey) const
{
  Spine::ReadLock lock(itsMutex);

  auto pos = itsProducts.find(theKey);
  if (pos == itsProducts.end() || pos->second.images.empty())
    return {};  // NOT_A_DATE_TIME

  return pos->second.images.rbegin()->first;
}

// ----------------------------------------------------------------------

std::size_t Repository::size(const ProductKey& theKey) const
{
  Spine::ReadLock lock(itsMutex);

  auto pos = itsProducts.find(theKey);
  if (pos == itsProducts.end())
    return 0;

  return pos->second.images.size();
}

// ----------------------------------------------------------------------
/*!
 * \brief Find the image closest to the desired time
 */
// ----------------------------------------------------------------------

ImageInfoPtr Repository::find(const ProductKey& theKey,
                              const std::optional<Fmi::DateTime>& theTime,
                              const Fmi::TimeDuration& theTolerance) const
{
  try
  {
    Spine::ReadLock lock(itsMutex);

    auto pos = itsProducts.find(theKey);
    if (pos == itsProducts.end())
      return nullptr;

    const auto& images = pos->second.images;
    if (images.empty())
      return nullptr;

    // No requested time means the latest available image
    if (!theTime)
      return images.rbegin()->second;

    const auto& t = *theTime;

    // Exact match is the common case for WMS requests generated from
    // the advertised time dimension
    auto exact = images.find(t);
    if (exact != images.end())
      return exact->second;

    // Otherwise choose the closest one within the tolerance. The first
    // image at or after the requested time and its predecessor are the
    // only candidates.
    auto next = images.lower_bound(t);

    ImageInfoPtr best;
    Fmi::TimeDuration bestdiff;

    if (next != images.end())
    {
      best = next->second;
      bestdiff = next->first - t;
    }

    if (next != images.begin())
    {
      auto prev = std::prev(next);
      auto diff = t - prev->first;
      if (!best || diff < bestdiff)
      {
        best = prev->second;
        bestdiff = diff;
      }
    }

    if (best && bestdiff <= theTolerance)
      return best;

    return nullptr;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Failed to find a satellite image")
        .addParameter("Product", describe(theKey));
  }
}

}  // namespace Satellite
}  // namespace Engine
}  // namespace SmartMet
