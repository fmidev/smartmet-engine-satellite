# EngineTest and DisabledTest drive the engine through a real Reactor and
# link against the locally built library. ApiTest is different: it is the
# guard for the installed API, hence it is compiled against a staged copy
# of the installed headers only and linked without satellite.so.

ENGINE_PROG = EngineTest DisabledTest
API_PROG = ApiTest
PROG = $(ENGINE_PROG) $(API_PROG)

REQUIRES = gdal configpp fmt

include $(shell echo $${PREFIX-/usr})/share/smartmet/devel/makefile.inc

SATELLITE_SRCDIR = ../satellite
include ../internal-headers.mk

CFLAGS = -DUNIX -O0 -g $(FLAGS)

INCLUDES += \
	-I$(SATELLITE_SRCDIR) \
	-isystem /usr/include/smartmet

LIBS += \
	$(PREFIX_LDFLAGS) \
	$(REQUIRED_LIBS) \
	-lsmartmet-spine \
	-lsmartmet-macgyver \
	-lboost_thread \
	-lboost_regex \
	-lpthread

# The images come from the smartmet-test-data package. Override to test
# against other data, for example a copy of the production tree.
SATELLITE_TEST_DATA ?= /usr/share/smartmet/test/data/satellite

# Directory the live scan test fills at run time
SATELLITE_SCAN_DIR ?= $(shell pwd)/tmp-scan

# Where the installed headers are staged for ApiTest. The layout is the
# installed one, so that ApiTest includes them the way a plugin does.
API_INCDIR = tmp-api-include
API_HDRDIR = $(API_INCDIR)/engines/satellite

all: $(PROG)

clean:
	rm -f $(PROG) *~
	rm -rf $(SATELLITE_SCAN_DIR) $(API_INCDIR)

test: $(PROG)
	@echo Running tests:
	@rm -rf $(SATELLITE_SCAN_DIR) && mkdir -p $(SATELLITE_SCAN_DIR)
	@rc=0; for prog in $(PROG); do \
	  SATELLITE_TEST_DATA=$(SATELLITE_TEST_DATA) \
	  SATELLITE_SCAN_DIR=$(SATELLITE_SCAN_DIR) ./$$prog || rc=1; \
	done; rm -rf $(SATELLITE_SCAN_DIR); exit $$rc

$(ENGINE_PROG) : % : %.cpp ../satellite.so
	$(CXX) $(CFLAGS) -o $@ $@.cpp $(INCLUDES) ../satellite.so $(LIBS)

# Only the installed headers, and no satellite.so on the command line:
# anything an installed header declares but does not define is an
# unresolved symbol here, which is precisely what must not happen to a
# plugin when the engine is not loaded.

$(API_HDRDIR): $(API_HDRS)
	@rm -rf $(API_INCDIR)
	@mkdir -p $(API_HDRDIR)
	@cp $(API_HDRS) $(API_HDRDIR)/
	@touch $(API_HDRDIR)

$(API_PROG) : % : %.cpp $(API_HDRDIR)
	$(CXX) $(CFLAGS) -o $@ $@.cpp -I$(API_INCDIR) -isystem /usr/include/smartmet $(LIBS)
