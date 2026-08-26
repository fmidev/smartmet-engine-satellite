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

The images come from `smartmet-test-data`
(`/usr/share/smartmet/test/data/satellite`), so `make test` needs no
arguments. `SATELLITE_TEST_DATA` overrides the location.

`test/EngineTest.cpp` drives the engine through a real `Spine::Reactor`
using `test/cnf/reactor.conf`, which loads the locally built
`../../satellite.so`. The products in `test/cnf/satellite.conf` are chosen so that every data
flavour is covered once: RGBA in a projected CRS, gray plus alpha, a
cloud optimized GeoTIFF in Eckert IV, the native geostationary
projection without overviews, uncoloured Float32 data, and one product
with several timesteps. Several share the producer `meteosat`, which is
what the menu tests need, and `ir108` exists for two producers.

Do not put products whose colouring is made for a particular customer
into the tests or the test data package: those are proprietary.

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

**Never drop MODIFY from the directory monitor mask.** It is not about
modified files. Without it the monitor skips listing a directory whose
own modification time has not advanced, and that time is shared by every
composite in the directory and has a one second resolution. With MODIFY
the directory is listed on every tick and each product diffs the file
times of its own files only, so composites arriving at different moments
are noticed independently. Removing it makes `live_scan` and
`staggered_updates` fail, which is the intended guard.

**File name patterns must match the whole name, and must be anchored at
the end.** `DirectoryMonitor` uses `boost::regex_match`, hence patterns
need a leading `.*`. One directory holds every composite of an
instrument, and the names overlap: the AVHRR EARS directory has `ir108`,
`ir108_ilmavoimat`, `vis06_with_ir108` and `vis08_with_ir108`, so
`.*_EPSG3035_ir108\.tif$` is right and `.*ir108.*\.tif$` would swallow
all four. The `shared_directory` test in `test/EngineTest.cpp` guards
this; it fails if the pattern is loosened.

**No-data values differ between products.** NaN in the NWC SAF products,
-444 in the COBRA ones, and `warpValues` normalizes both to NaN so that
callers have one rule. Do not assume NaN when reading a file.

**Image identity, not pixels, is the ETag basis.** `ImageInfo::hash`
combines the path, size and modification time. The production system
writes each file once and never modifies it. Never hash pixels: the WMS
plugin asks for this hash on every request, including the conditional
ones that must not render anything.

**A broken image is indistinguishable from an empty one.** The production
system leaves files whose tile directory is all zeros: they open, they
read, and every pixel comes back as zero, so the layer serves a
transparent tile and the ETag is perfectly stable, which means the blank
gets cached. See `~/hub/satellite/WASTED_SPACE_REPORT.md`. Detecting this
would mean looking at the TIFF tile offsets at scan time; nothing does
that yet, so a bad production cycle shows up as missing imagery rather
than as an error.

## Not implemented yet

- Colour maps for uncoloured data are applied by the WMS layer, not here:
  `warpValues()` returns floats with NaN for missing values and the layer
  turns them into pixels with `Dali::ColorMap`. Keep it that way, the
  colour policy belongs where the rest of the styling is.

- Level 4 NetCDF products: the IASI files are a CF grid with 138 levels,
  the AVHRR swath files carry a latitude and longitude for every pixel
  and would need resampling.
- Admin queries for cache statistics: `getCacheStats()` returns nothing
  because the engine keeps no pixel cache of its own yet.
