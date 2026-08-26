// ======================================================================
/*!
 * \brief Catalog of the available satellite images
 */
// ======================================================================

#include "Repository.h"
#include <macgyver/Exception.h>

namespace SmartMet
{
namespace Engine
{
namespace Satellite
{
// ----------------------------------------------------------------------

void Repository::add(const Producer& theProducer)
{
  try
  {
    Spine::WriteLock lock(itsMutex);
    Contents contents;
    contents.producer = theProducer;
    contents.bbox = theProducer.bbox;  // Configured override, if any
    itsProducers.insert({theProducer.name, contents});
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Failed to add satellite producer");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add a scanned image to the catalog
 */
// ----------------------------------------------------------------------

void Repository::insert(const std::string& theProducer, const ImageInfoPtr& theImage)
{
  try
  {
    Spine::WriteLock lock(itsMutex);

    auto pos = itsProducers.find(theProducer);
    if (pos == itsProducers.end())
      return;

    auto& contents = pos->second;
    contents.images[theImage->time] = theImage;

    // The newest image defines the estimated bounding box unless the
    // configuration overrides it
    if (!contents.producer.bbox && theImage->time == contents.images.rbegin()->first)
      contents.bbox = theImage->bbox;

    limitSize(contents);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Failed to insert a satellite image")
        .addParameter("Producer", theProducer);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Remove a deleted image from the catalog
 */
// ----------------------------------------------------------------------

void Repository::remove(const std::string& theProducer, const std::string& thePath)
{
  try
  {
    Spine::WriteLock lock(itsMutex);

    auto pos = itsProducers.find(theProducer);
    if (pos == itsProducers.end())
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
        .addParameter("Producer", theProducer);
  }
}

// ----------------------------------------------------------------------

void Repository::limitSize(Contents& theContents)
{
  const auto max_files = theContents.producer.max_files;
  if (max_files == 0)
    return;

  auto& images = theContents.images;
  while (images.size() > max_files)
    images.erase(images.begin());
}

// ----------------------------------------------------------------------

std::vector<std::string> Repository::producers() const
{
  Spine::ReadLock lock(itsMutex);
  std::vector<std::string> ret;
  ret.reserve(itsProducers.size());
  for (const auto& producer : itsProducers)
    ret.push_back(producer.first);
  return ret;
}

// ----------------------------------------------------------------------

bool Repository::hasProducer(const std::string& theProducer) const
{
  Spine::ReadLock lock(itsMutex);
  return itsProducers.find(theProducer) != itsProducers.end();
}

// ----------------------------------------------------------------------

ProducerInfo Repository::producerInfo(const std::string& theProducer) const
{
  try
  {
    Spine::ReadLock lock(itsMutex);

    auto pos = itsProducers.find(theProducer);
    if (pos == itsProducers.end())
      throw Fmi::Exception(BCP, "Unknown satellite producer '" + theProducer + "'");

    const auto& contents = pos->second;

    ProducerInfo info;
    info.name = contents.producer.name;
    info.title = contents.producer.title;
    info.abstract = contents.producer.abstract;
    info.keywords = contents.producer.keywords;
    info.bbox = contents.bbox;
    return info;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Failed to get satellite producer information")
        .addParameter("Producer", theProducer);
  }
}

// ----------------------------------------------------------------------

std::vector<Fmi::DateTime> Repository::times(const std::string& theProducer) const
{
  Spine::ReadLock lock(itsMutex);

  std::vector<Fmi::DateTime> ret;

  auto pos = itsProducers.find(theProducer);
  if (pos == itsProducers.end())
    return ret;

  ret.reserve(pos->second.images.size());
  for (const auto& image : pos->second.images)
    ret.push_back(image.first);

  return ret;
}

// ----------------------------------------------------------------------

Fmi::DateTime Repository::latestTime(const std::string& theProducer) const
{
  Spine::ReadLock lock(itsMutex);

  auto pos = itsProducers.find(theProducer);
  if (pos == itsProducers.end() || pos->second.images.empty())
    return {};  // NOT_A_DATE_TIME

  return pos->second.images.rbegin()->first;
}

// ----------------------------------------------------------------------

std::size_t Repository::size(const std::string& theProducer) const
{
  Spine::ReadLock lock(itsMutex);

  auto pos = itsProducers.find(theProducer);
  if (pos == itsProducers.end())
    return 0;

  return pos->second.images.size();
}

// ----------------------------------------------------------------------
/*!
 * \brief Find the image closest to the desired time
 */
// ----------------------------------------------------------------------

ImageInfoPtr Repository::find(const std::string& theProducer,
                              const std::optional<Fmi::DateTime>& theTime,
                              const Fmi::TimeDuration& theTolerance) const
{
  try
  {
    Spine::ReadLock lock(itsMutex);

    auto pos = itsProducers.find(theProducer);
    if (pos == itsProducers.end())
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
        .addParameter("Producer", theProducer);
  }
}

}  // namespace Satellite
}  // namespace Engine
}  // namespace SmartMet
