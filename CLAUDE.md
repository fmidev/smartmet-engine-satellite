# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

The satellite engine of the SmartMet Server. It serves precoloured
satellite imagery (GeoTIFF, mostly cloud optimized) to the WMS plugin.
See `README.md` for the concepts and configuration, and
`docs/poc-benchmarks.md` for performance measurements.

Only the `satellite` layer type of the WMS plugin uses this engine. No
other plugin or layer knows it exists.

A product is identified by a **producer** (the satellite) and a
**parameter** (the composite), so that clients can build menus by listing
producers and then the parameters of one. `ProductKey` is that pair.

## Build and test commands

```bash
make                    # Build satellite.so
sudo make install       # /usr/share/smartmet/engines/satellite.so + headers
make test               # Run the Boost/tframe test
make format             # clang-format
make rpm                # Build the RPM
```

The tests need sample data which is not packaged yet:

```bash
make test SATELLITE_TEST_DATA=$HOME/hub/satellite/weather
```

`test/EngineTest.cpp` drives the engine through a real `Spine::Reactor`
using `test/cnf/reactor.conf`, which loads the locally built
`../../satellite.so`. The products in `test/cnf/satellite.conf` are chosen so that every data
flavour is covered once: RGBA in a projected CRS, gray plus alpha, a
large cloud optimized GeoTIFF in Eckert IV, the native geostationary
projection, uncoloured Float32 data, and one product with several
timesteps. Several of them share the producer `meteosat`, which is what
the menu tests need, and `ir108` exists for two producers.

## Source layout

```
satellite/Engine.{h,cpp}      Engine class, the API the WMS layer uses
satellite/Config.{h,cpp}      libconfig parsing
satellite/Product.h           One product's configuration
satellite/ImageInfo.h         Metadata of one image file
satellite/Repository.{h,cpp}  The catalog: (producer,parameter) -> time -> image
satellite/Scanner.{h,cpp}     DirectoryMonitor keeping the catalog current
satellite/Gdal.{h,cpp}        All GDAL and PROJ usage
```

## Things to be careful about

**GDAL and PROJ threading.** `GDALDataset` must never be shared between
threads, so every call opens and closes its own. Coordinate reference
systems must never be shared either: doing so has corrupted the heap in
this codebase before. WKT parsing hits the PROJ database and is
serialized with a mutex in `Gdal.cpp`; the warping itself is parallel.
Do not "optimize" these away without measuring, and never by sharing an
`OGRSpatialReference`.

**Overview selection.** The GDAL warper does not pick an overview level
by itself. `select_overview()` estimates the scale and reopens the file
with `OVERVIEW_LEVEL`. Removing this makes zoomed out views of the large
products orders of magnitude slower.

**The approximating transformer.** `GDALCreateApproxTransformer` with a
0.125 pixel threshold, the same as `gdalwarp` uses, is worth a factor of
eight. It is invisible for nearest neighbour resampling of precoloured
data, but would need thought if interpolation were ever added.

**File name patterns must match the whole name.** `DirectoryMonitor` uses
`boost::regex_match`, hence patterns need a leading `.*`.

**Image identity, not pixels, is the ETag basis.** `ImageInfo::hash`
combines the path, size and modification time. The production system
writes each file once and never modifies it. Never hash pixels: the WMS
plugin asks for this hash on every request, including the conditional
ones that must not render anything.

## Not implemented yet

- Uncoloured Float32 data (NWC SAF products, COBRA) needs a colour map.
  Such files are recognized and rejected with a clear message.
- Level 4 NetCDF products: the IASI files are a CF grid with 138 levels,
  the AVHRR swath files carry a latitude and longitude for every pixel
  and would need resampling.
- Admin queries for cache statistics: `getCacheStats()` returns nothing
  because the engine keeps no pixel cache of its own yet.
