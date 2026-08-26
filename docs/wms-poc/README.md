# WMS plugin test wiring for the satellite proof of concept

These files add satellite layers to the test environment of the WMS
plugin. They are kept here rather than in the plugin repository for two
reasons: the sample data is not packaged for the test machines yet, and
the WMS test configuration uses `lazylinking = false`, which means that
declaring an engine which is not installed fails the whole test suite.

Install them into a WMS plugin working copy when you want to try the
satellite layers, and keep the changes out of the commits until the
sample data and the engine RPM are available in CI.

## Installing

```bash
SAT=~/hub/brainstorm/engines/satellite/docs/wms-poc
WMS=~/hub/brainstorm/plugins/wms

cp $SAT/cnf/*.conf              $WMS/test/cnf/
cp $SAT/wms-products/*.json     $WMS/test/wms/customers/test/products/
cp $SAT/dali-products/*.json    $WMS/test/dali/customers/test/products/
cp $SAT/input/*.get             $WMS/test/input/
```

Then declare the engine in `$WMS/test/cnf/reactor.conf`:

```
	satellite:
	{
		configfile	= "satellite.conf";
	}
```

and tell the test Makefile where the sample data is, by adding this near
the top of `$WMS/test/Makefile`:

```make
SATELLITE_TEST_DATA ?= $(HOME)/hub/satellite/weather
export SATELLITE_TEST_DATA
```

## Running

```bash
cd $WMS/test
make test-dali  DALI_TESTS="input/satellite.get"
make test-wms   WMS_TESTS="input/wms_satellite_getmap.get input/wms_satellite_geos.get \
                           input/wms_satellite_goes_world.get input/wms_satellite_fog_time.get"
make test-wmts  WMTS_TESTS="input/wmts_satellite_gettile.get"
```

The expected outputs are not included: generate them once with the
commands above and copy `failures/*.get` to `output/`. The sample files
are never modified, so the results are reproducible after that.

Note that adding products changes the GetCapabilities baselines
(`output/wms_getcapabilities*.get`, `output/dali_getcapabilities.get`),
so those tests fail until they are regenerated as well. That is the other
reason these files are not committed to the plugin.

## What each product covers

| Product | Covers |
| --- | --- |
| `satellite_meteosat` | RGBA in EPSG:3035, the primary case, with a border overlay to show compositing |
| `satellite_geos` | native geostationary projection, gray plus alpha, full disc |
| `satellite_goes` | large cloud optimized GeoTIFF in Eckert IV, which has no EPSG code |
| `satellite_fog` | several timesteps, which exercises the WMS time dimension |
| `satellite.json` (Dali) | a fixed EPSG:3067 projection, for rendering without a WMS request |

## Standalone server

`cnf/server-satellite.conf` runs `smartmetd` with the same engines and
plugin configuration as the test suite, which is how the benchmarks in
`../poc-benchmarks.md` were measured:

```bash
cd $WMS/test
SATELLITE_TEST_DATA=$HOME/hub/satellite/weather \
  smartmetd -c cnf/server-satellite.conf
```

It listens on port 8099 and writes its access logs under the directory
named in the configuration file. Adjust both if they collide with
something else on your machine.
