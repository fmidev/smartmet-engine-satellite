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

# Location of the sample data. Override on the command line or in the
# environment once the data is packaged for the test machines.
SATELLITE_TEST_DATA ?= $(HOME)/hub/satellite/weather

all: $(PROG)

clean:
	rm -f $(PROG) *~

test: $(PROG)
	@echo Running tests:
	@rc=0; for prog in $(PROG); do \
	  SATELLITE_TEST_DATA=$(SATELLITE_TEST_DATA) ./$$prog || rc=1; \
	done; exit $$rc

$(PROG) : % : %.cpp ../satellite.so
	$(CXX) $(CFLAGS) -o $@ $@.cpp $(INCLUDES) $(LIBS)
