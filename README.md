# smartmet-engine-satellite

Part of [SmartMet Server](https://github.com/fmidev/smartmet-server). See the [SmartMet Server documentation](https://github.com/fmidev/smartmet-server) for a full overview of the ecosystem.

## Overview

The satellite engine serves precoloured satellite imagery to the WMS
plugin. Unlike the other data sources of the server the images are ready
for display: the pixels are RGBA and no styling is applied, hence the
projection transformations use nearest neighbour interpolation. The
images are produced as GeoTIFF files, the larger ones as cloud optimized
GeoTIFF files with an overview pyramid. The engine watches the configured
directories, reads the metadata of the files it finds, and serves the
images warped to the projection the client asked for.

Only the `satellite` layer type of the WMS plugin uses this engine.

## Features

- Serves any projection GDAL understands, including the native
  geostationary projection of the satellites and Eckert IV, which has no
  EPSG code and is described by the embedded WKT only
- Chooses the overview level which suits the request, so a zoomed out
  view of a large image does not read full resolution data, and reports
  in the result which level the pixels came from
- Precoloured RGBA, RGB, gray plus alpha and gray images
- Uncoloured values, for example a cloud top temperature in Kelvin, for
  the caller to colour with a colour map
- Notices new and deleted images without a restart
- Gives the WMS plugin an ETag basis which costs no pixel reading, so
  conditional requests are answered without rendering anything

## Concepts

A **product** is identified by a **producer** and a **parameter**: the
producer says which satellite or data stream the images come from, and
the parameter says which composite of it. In the WMS plugin this is a
satellite layer:

```json
{ "layer_type": "satellite", "producer": "meteosat", "parameter": "natural" }
```

Splitting the identity in two is what makes menus easy to build: a client
lists the producers, and then the parameters of the one the user picked.

```cpp
for (const auto& producer : engine.producers())
  for (const auto& parameter : engine.parameters(producer))
    ...engine.productInfo(producer, parameter).title...
```

Each product is a directory plus a file name pattern, because one
directory holds many composites of many times. The valid time is parsed
from the beginning of the file name, which the production system writes
in the form `YYYYMMDD_HHMM_Platform_area_composite.tif`. Note that the
platform is not part of the identity: the EARS products alternate between
Metop-B and Metop-C from one pass to the next.

## Configuration

```
rootdir = "/smartmet/satellite/weather";

products:
{
  meteosat_natural:                                 # any unique name
  {
    producer  = "meteosat";                         # the satellite
    parameter = "natural";                          # the composite
    directory = "seviri/0deg/level3/EPSG3035/img";   # relative to rootdir
    pattern   = ".*_natural_with_colorized_ir_clouds\\.tif$";
    title     = "Natural colours";
    abstract  = "SEVIRI natural colour composite";
    keywords  = ["satellite", "seviri", "meteosat"];
    refresh_interval_secs = 60;   # directory scan interval, default 60
    max_files = 200;              # keep this many newest images, 0 = all
    # bbox = [-45.0, 27.6, 63.3, 68.1];   # WGS84 override of the estimate
  };

  meteosat_ir108:                                   # same satellite, another composite
  {
    producer  = "meteosat";
    parameter = "ir108";
    directory = "seviri/0deg/level3/geos/img";
    pattern   = ".*_geos_ir108\\.tif$";
    title     = "Infrared 10.8 um";
  };
};
```

An empty configuration file is legal: the engine starts, warns that
there will be no satellite data, and serves empty listings. `rootdir` is
needed only when a product uses a relative `directory`.

The name of the configuration group is free and is used only in error
messages; the producer and parameter pair is the identity, and the same
pair must not appear twice. The same parameter name may of course be used
by several producers, which is the point: `meteosat/ir108` and
`metop/ir108` are different products.

The `pattern` is what separates the composites, because one directory
holds all of them. It must match the **whole** file name, hence the
leading `.*`, and it should be anchored at the end, because the names are
not always distinct enough to be careless about. The AVHRR EARS directory
for example holds

```
20260826_1122_Metop-C_EPSG3035_ir108.tif
20260826_1122_Metop-C_EPSG3035_ir108_ilmavoimat.tif
20260826_1122_Metop-C_EPSG3035_vis06_with_ir108.tif
20260826_1122_Metop-C_EPSG3035_vis08_with_ir108.tif
```

so `.*_EPSG3035_ir108\.tif$` picks exactly one of the four, while a
pattern like `.*ir108.*\.tif$` would pick all of them. Anchoring also
keeps the partially written files out: the production system writes
`name.tif.tmp.tif` and `name.tif.ovr.tmp` while it works.

The `title`, `abstract` and `keywords` end up in the WMS GetCapabilities
response. The bounding box is estimated from the newest image and can be
overridden when the estimate is not wanted.

## Supported data

| Bands | Interpretation | Status |
| --- | --- | --- |
| 4 x Byte | red, green, blue, alpha | supported |
| 3 x Byte | red, green, blue | supported, fully opaque |
| 2 x Byte | gray, alpha | supported, gray is expanded to RGB |
| 1 x Byte | gray | supported, fully opaque |
| 1 x Float32 | uncoloured values | supported, needs a colour map |

Uncoloured data such as the NWC SAF cloud top temperature products holds
values rather than colours, so the engine hands out the warped values and
the caller colours them. `warpValues()` returns a float buffer in which
missing values are NaN, whatever the image itself uses to mark them: the
NWC SAF products use NaN and the COBRA products use -444, so the value is
read from the file. Areas the image does not cover are NaN as well, since
zero is a perfectly good temperature and would draw a cold region which
is not there.

Resampling is nearest neighbour for values too. Interpolating across the
edge of the data, or between the two sides of a cloud edge, would invent
values which were never measured.

In the WMS plugin the satellite layer colours these with the same colour
maps the raster layer uses:

```json
{ "layer_type": "satellite", "producer": "meteosat",
  "parameter": "ctth_tempe", "colormap": "cloud_top_temperature" }
```

A colour map on a precoloured product, or a missing colour map on an
uncoloured one, is reported when the layer is created rather than
silently ignored. Some products also ship their colour scale beside the
images as an SLD file with a `ColorMap type="ramp"`, which is the same
thing as a colour map file with smooth colours, so converting one is a
matter of transcribing the entries.

Any projection GDAL understands works, including the native geostationary
projection of the satellites and the Eckert IV projection, which has no
EPSG code and is described by the embedded WKT only.

## Performance notes

Two things dominate the cost of serving a zoomed out view:

1. **Overviews.** A cloud optimized GeoTIFF contains a pyramid of
   downscaled images, but the GDAL warper does not choose the overview by
   itself. The engine estimates how many source pixels one target pixel
   covers and reopens the file at the matching overview level. Without
   this a whole Earth tile of a 7633x8313 image would be read at full
   resolution. Products stored without overviews, such as the current
   geostationary projection files, cannot benefit from this.

2. **Coordinate transformation.** Transforming every pixel exactly means
   calling PROJ for every pixel, which costs several times more than the
   resampling itself. The engine approximates the transformation to
   within 0.125 pixels, which is what the `gdalwarp` utility does by
   default. This is worth a factor of eight at 1024x1024.

See `docs/poc-benchmarks.md` for measurements.

## Scanning

Each product is watched separately, with its own file name pattern and
its own scan interval, and the monitor compares the modification times of
the files matching that pattern only. Composites therefore do not
interfere with each other even though they share a directory: one
composite standing still cannot delay another, and an arriving image is
credited to the product whose pattern matches it and to no other.

The one subtlety is that the monitor is asked for MODIFY events even
though the production system never rewrites an image. Without that
request the monitor skips listing a directory whose own modification time
has not advanced, and that time is shared by all the composites of the
directory and has a one second resolution, so a change could be missed
until something else happened in the same directory. The cost of asking
for MODIFY is that a directory is listed once per product per interval
rather than once per interval.

## Thread safety

`GDALDataset` is not thread safe and is never shared between threads: every
call opens its own dataset and closes it before returning. Coordinate
reference systems are likewise always parsed locally and never shared,
since sharing them has been observed to corrupt the heap. Parsing WKT
reads the PROJ database, and doing that from several threads at once has
caused both deadlocks and heap corruption, hence the parsing is
serialized with a mutex. The warping itself, which is where the time
goes, is fully parallel.

## Building and testing

```bash
make
sudo make install          # /usr/share/smartmet/engines/satellite.so + headers
make test
```

The images come from the `smartmet-test-data` package, in
`/usr/share/smartmet/test/data/satellite`. Point `SATELLITE_TEST_DATA`
elsewhere to test against other data, for example a copy of the
production tree:

```bash
make test SATELLITE_TEST_DATA=/smartmet/satellite/weather
```

## Documentation

- `docs/poc-benchmarks.md` — measurements: warping, the rendering
  pipeline, the caching paths, and what the client request pattern means
  for a cache of decompressed data
- `docs/wms-poc/` — the WMS plugin test wiring for trying the layer out
- `CLAUDE.md` — notes for working on the code
