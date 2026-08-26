PROG = $(patsubst %.cpp,%,$(wildcard *Test.cpp))

REQUIRES = gdal configpp fmt

include $(shell echo $${PREFIX-/usr})/share/smartmet/devel/makefile.inc

CFLAGS = -DUNIX -O0 -g $(FLAGS)

INCLUDES += \
	-I../satellite \
	-isystem /usr/include/smartmet

LIBS += \
	../satellite.so \
	$(PREFIX_LDFLAGS) \
	$(REQUIRED_LIBS) \
	-lsmartmet-spine \
	-lsmartmet-macgyver \
	-lboost_thread \
	-lboost_regex \
	-lboost_system \
	-lpthread

# The images come from the smartmet-test-data package. Override to test
# against other data, for example a copy of the production tree.
SATELLITE_TEST_DATA ?= $(shell test -d /usr/share/smartmet/test/data/satellite \
	&& echo /usr/share/smartmet/test/data/satellite \
	|| echo $(HOME)/hub/smartmet-test-data/satellite)

# Directory the live scan test fills at run time
SATELLITE_SCAN_DIR ?= $(shell pwd)/tmp-scan

all: $(PROG)

clean:
	rm -f $(PROG) *~
	rm -rf $(SATELLITE_SCAN_DIR)

test: $(PROG)
	@echo Running tests:
	@rm -rf $(SATELLITE_SCAN_DIR) && mkdir -p $(SATELLITE_SCAN_DIR)
	@rc=0; for prog in $(PROG); do \
	  SATELLITE_TEST_DATA=$(SATELLITE_TEST_DATA) \
	  SATELLITE_SCAN_DIR=$(SATELLITE_SCAN_DIR) ./$$prog || rc=1; \
	done; rm -rf $(SATELLITE_SCAN_DIR); exit $$rc

$(PROG) : % : %.cpp ../satellite.so
	$(CXX) $(CFLAGS) -o $@ $@.cpp $(INCLUDES) $(LIBS)
