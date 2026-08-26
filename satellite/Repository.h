// ======================================================================
/*!
 * \brief Catalog of the available satellite images
 *
 * The images of one product are kept sorted by valid time, hence
 * finding the latest one or the one closest to a requested time is a
 * simple map lookup. All public methods are thread safe.
 */
// ======================================================================

#pragma once

#include "ImageInfo.h"
#include "Product.h"
#include <spine/Thread.h>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace SmartMet
{
namespace Engine
{
namespace Satellite
{
// Metadata of one product for GetCapabilities responses and menus

struct ProductInfo
{
  std::string producer;
  std::string parameter;
  std::string title;
  std::string abstract;
  std::vector<std::string> keywords;

  // WGS84 bounding box: minx miny maxx maxy. Empty if not known yet.
  std::optional<std::array<double, 4>> bbox;
};

class Repository
{
 public:
  // Called once at startup for each configured product
  void add(const Product& theProduct);

  // Scanner interface

  void insert(const ProductKey& theKey, const ImageInfoPtr& theImage);
  void remove(const ProductKey& theKey, const std::string& thePath);

  // Query interface

  std::vector<std::string> producers() const;
  std::vector<std::string> parameters(const std::string& theProducer) const;
  bool hasProducer(const std::string& theProducer) const;
  bool hasProduct(const ProductKey& theKey) const;

  ProductInfo productInfo(const ProductKey& theKey) const;
  std::vector<Fmi::DateTime> times(const ProductKey& theKey) const;
  Fmi::DateTime latestTime(const ProductKey& theKey) const;
  std::size_t size(const ProductKey& theKey) const;

  // Image closest to the requested time, or the latest one if no time is
  // given. Returns nullptr if the product is unknown or no image is
  // within the given tolerance. A zero tolerance requires an exact match.
  ImageInfoPtr find(const ProductKey& theKey,
                    const std::optional<Fmi::DateTime>& theTime,
                    const Fmi::TimeDuration& theTolerance) const;

 private:
  struct Contents
  {
    Product product;
    std::map<Fmi::DateTime, ImageInfoPtr> images;
    std::optional<std::array<double, 4>> bbox;  // Estimated from the newest image
  };

  // Erase the oldest images if the product has more than max_files of
  // them. Must be called with the write lock held.
  static void limitSize(Contents& theContents);

  mutable Spine::MutexType itsMutex;
  std::map<ProductKey, Contents> itsProducts;
};

}  // namespace Satellite
}  // namespace Engine
}  // namespace SmartMet
