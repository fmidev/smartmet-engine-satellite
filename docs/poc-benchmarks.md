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
| 256x256 EPSG:3857, zoomed in | SEVIRI 2 km, 2028x2510 | 26.5 ms | **12.3 ms** |
| 512x512 EPSG:3857, zoomed in | SEVIRI 2 km | 75.1 ms | **16.5 ms** |
| 1024x1024 EPSG:3857, zoomed in | SEVIRI 2 km | 269.2 ms | **31.8 ms** |
| 1024x1024 EPSG:3857, all of Europe | SEVIRI 2 km | 302.8 ms | **180.8 ms** |
| 256x256 EPSG:3857, whole Earth | GOES-East Eckert IV, 7633x8313, 63 MB **COG** | 23.5 ms | **12.4 ms** |
| 512x512 EPSG:3857, whole disc | SEVIRI geostationary, 3712x3712, **no overviews** | 149.6 ms | **151.2 ms** |

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

| Request | Engine warp | Whole request | Pipeline overhead |
| --- | --- | --- | --- |
| GetMap 256x256 EPSG:3857, SEVIRI | 12 ms | **70 ms** | ~58 ms |
| GetMap 512x512 EPSG:3067, SEVIRI | 17 ms | **193 ms** | ~176 ms |
| GetMap 1024x1024 EPSG:3067, SEVIRI | 32 ms | **431 ms** | ~400 ms |
| GetMap 1024x1024 EPSG:3857, Europe | 181 ms | **539 ms** | ~360 ms |
| GetMap 512x512, geostationary disc | 151 ms | **278 ms** | ~127 ms |
| GetMap 256x256, whole Earth from COG | 12 ms | **105 ms** | ~93 ms |
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
