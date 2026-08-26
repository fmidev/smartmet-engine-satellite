# Satellite WMS proof of concept: measurements

Measured 2026-08-26 on a developer workstation: 12th Gen Intel Core
i7-12850HX, 24 threads, 62 GB RAM, GDAL 3.12.1, PROJ 9.7.1, data on a
local NVMe disk. The sample data is the ten newest files of each
`/smartmet/satellite` subdirectory, copied to `~/hub/satellite`.

These numbers are from the first working implementation. No pixel caching
of any kind has been added yet beyond the rendered image cache the WMS
plugin already has.

## Engine alone: warping one image

Nearest neighbour warp of one image into an RGBA buffer, five rounds
after a warm-up round, measured by `test/EngineTest.cpp` (`warp_speed`).
The "exact" column transformed every pixel with PROJ, the "approximate"
column transforms to within 0.125 pixels the way `gdalwarp` does by
default.

| Case | Source | Exact | Approximate |
| --- | --- | --- | --- |
| 256x256 EPSG:3857, zoomed in | SEVIRI 2 km, 2028x2510 | 26.5 ms | **9.7 ms** |
| 512x512 EPSG:3857, zoomed in | SEVIRI 2 km | 75.1 ms | **10.6 ms** |
| 1024x1024 EPSG:3857, zoomed in | SEVIRI 2 km | 269.2 ms | **18.3 ms** |
| 1024x1024 EPSG:3857, all of Europe | SEVIRI 2 km | 302.8 ms | **159.8 ms** |
| 256x256 EPSG:3857, whole Earth | GOES-East Eckert IV, 7633x8313 **COG** | 23.5 ms | **12.1 ms** |
| 512x512 EPSG:3857, whole disc | SEVIRI geostationary, 3712x3712, **no overviews** | 149.6 ms | **149.6 ms** |

Two observations decide the performance of this engine.

**Overviews are what make large images cheap.** The whole Earth view of
the 63 MB cloud optimized GeoTIFF costs the same as a zoomed in view of a
small image, because only the matching overview level is read. The GDAL
warper does not choose the overview by itself, so the engine estimates
how many source pixels one target pixel covers and reopens the file at
the right level. The geostationary products are stored **without** an
overview pyramid, which is why they are the slowest case in the table and
the only one the approximation does not help: the whole 3712x3712 image
has to be read and decompressed. Adding overviews to those products in
the production system would make them as fast as the others.

**Approximating the coordinate transformation is worth a factor of
eight.** Calling PROJ per pixel costs several times more than the
resampling itself. At 0.125 pixels the error is invisible for nearest
neighbour resampling of precoloured imagery.

## Through the server: WMS GetMap

`smartmetd` with the WMS test configuration, requested over HTTP with
`curl`. Each request shifts the bounding box slightly so that the
rendered image cache is bypassed; the median of five to ten requests is
given, since the first touch of a file also pays for reading it from disk.

| Request | Engine warp | Whole request | Pipeline share |
| --- | --- | --- | --- |
| GetMap 256x256 EPSG:3857, SEVIRI | 9.7 ms | **70 ms** | 86 % |
| GetMap 512x512 EPSG:3067, SEVIRI | 10.6 ms | **193 ms** | 95 % |
| GetMap 350x380 EPSG:3035, cloud top temperature with a colour map | ~10 ms | **78 ms** | 87 % |
| GetMap 256x256, whole Earth from a COG | 12.1 ms | **114 ms** | 89 % |
| GetMap 512x512, geostationary disc, no overviews | 149.6 ms | **278 ms** | 46 % |
| WMTS GetTile 1024x1024, zoom 5 | | **151 ms** | |

The difference between the warp and the whole request is the shared
rendering pipeline: the warped image is encoded as PNG, base64 encoded
into an SVG `<image>` element, parsed and decoded again by librsvg,
composited by Cairo together with the other layers, and finally encoded
as PNG once more. That is two PNG encodes, one PNG decode and a base64
round trip per request, and at 1024x1024 it costs more than everything
else put together.

This is the obvious next optimization and it is not satellite specific:
the plugin already bypasses the SVG pipeline for GeoTIFF, MVT and
DataTile output. A satellite layer which is the only layer of a product,
which is the normal case for a tile service, could encode its RGBA buffer
straight into the response.

## Where a cache would help

Measured with a standalone GDAL program (`docs/` has no copy; the numbers
come from the same machine and data). The question is whether the engine
should keep decompressed pixels in memory.

| Operation | Cost |
| --- | --- |
| Opening and closing a GeoTIFF, metadata only | **0.043 ms** |
| Decompressing the whole base level of the 2 km RGBA product, 20.4 MB of pixels | **81 ms** |
| The same read with the dataset kept open, blocks already decompressed | **2.8 ms** |
| One 256x256 tile of that product, dataset opened per request | **13.3 ms** |
| The same tile with the dataset kept open | **2.8 ms**, GDAL holding 2.1 MB |

Opening a file costs nothing, so the win from keeping a dataset open is
entirely the decompressed blocks GDAL retains. Note the last row: serving
that tile needs only 2.1 MB of decompressed blocks, not the whole 20 MB
image, because the request touches a few 512x512 tiles of one overview
level. A cache of decompressed **blocks**, which is what GDAL's own block
cache is, therefore buys the whole saving at a fraction of the memory a
cache of decompressed **images** would need.

For comparison, whole decompressed images are 20 MB for the 2 km European
products, 55 MB for a geostationary full disc, 254 MB for GOES-East in
Eckert IV and 563 MB for Himawari. A cache holding "1000 images" is
therefore anywhere between 20 GB and 500 GB, which is why such a cache
has to be bounded in bytes rather than in images.

## Caching

The interesting numbers for a tile service are not the rendering times
but the cache paths, since a frontend serving a map repeats the same
requests constantly.

| Path | Time |
| --- | --- |
| Rendered image cache hit (identical request) | **1 ms** |
| `X-Request-ETag` probe, answered with 204 | **1.1 ms** |
| `If-None-Match` matching, answered with 304 | **0.9 ms** |

The ETag is the hash of the product definition combined with the identity
of the image file, which the engine derives from the file name, size and
modification time when it first sees the file. No pixels are hashed, and
nothing has to be rendered to answer a conditional request. The satellite
files are written once and never modified, so the ETag of a given image
is stable for as long as the file exists.

`Last-Modified` is the valid time of the image that was used, and
`Expires` comes from the plugin as usual.

## What this says about replacing GeoServer

The pieces the comparison depends on are in place: strong ETags without
reading pixel data, 304 and 204 answers in about a millisecond, overview
based reads so that zoomed out views do not touch full resolution data,
and one warp implementation shared by WMS, WMTS and OGC API Tiles.

The rendering path still carries the cost of the SVG pipeline, which is
the largest single item in every measurement above and is entirely
avoidable for single layer satellite products. A comparison against the
current GeoServer installation should be made with the same products and
the same tile sizes; those numbers have to be collected on the FMI side.

## Reproducing

```bash
# Engine only
cd ~/hub/brainstorm/engines/satellite
make && make test SATELLITE_TEST_DATA=$HOME/hub/satellite/weather

# Through the server, from the WMS plugin test directory
cd ~/hub/brainstorm/plugins/wms/test
SATELLITE_TEST_DATA=$HOME/hub/satellite/weather \
  smartmetd -c cnf/server-satellite.conf
curl -o tile.png 'http://localhost:8099/wms?service=WMS&version=1.3.0\
&request=GetMap&layers=test:satellite_meteosat&styles=&crs=EPSG:3857\
&bbox=2504688,8140237,3130860,8766409&width=256&height=256\
&format=image/png&transparent=true'
```
