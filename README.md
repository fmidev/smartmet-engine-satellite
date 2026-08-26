# SmartMet satellite engine

Serves precoloured satellite imagery to the WMS plugin of the SmartMet
Server. Unlike the other data sources of the server the images are ready
for display: the pixels are RGBA and no styling is applied, hence the
projection transformations use nearest neighbour interpolation.

The images are produced as GeoTIFF files by the satellite data
production system, the larger ones as cloud optimized GeoTIFF files with
an overview pyramid. The engine watches the configured directories, reads
the metadata of the files it finds, and serves the images warped to the
projection requested by the client.

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
| 1 x Float32 | uncoloured values | recognized, **not supported yet** |

Uncoloured data such as the NWC SAF cloud top temperature products needs
a colour map, which is a separate feature. The engine recognizes such
files so that a clear error message can be given instead of a crash.
Some of these products already ship their colour scale beside the images
as an SLD file with a `ColorMap type="ramp"`, which is the same thing as
the colour maps the WMS plugin already interpolates. Note that the
no-data value is NaN in the NWC SAF products but -444 in the COBRA ones,
so it has to be read from the file.

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

The tests need sample data, which is not packaged yet. Point
`SATELLITE_TEST_DATA` at a copy of the `/smartmet/satellite/weather`
directory tree:

```bash
make test SATELLITE_TEST_DATA=$HOME/hub/satellite/weather
```
