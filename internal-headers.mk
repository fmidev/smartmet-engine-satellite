# Headers which are internal to the engine and are not installed.
#
# Everything the installed headers declare is defined inline, so that a
# plugin using the API links and loads even when the engine library is
# not loaded. test/ApiTest.cpp guards that: it compiles against the
# installed set only and links without satellite.so.
#
# Included by both the top level Makefile and test/Makefile, which set
# SATELLITE_SRCDIR to the directory holding the sources.

INTERNAL_HDRS = \
	$(SATELLITE_SRCDIR)/Config.h \
	$(SATELLITE_SRCDIR)/EngineImpl.h \
	$(SATELLITE_SRCDIR)/Gdal.h \
	$(SATELLITE_SRCDIR)/Product.h \
	$(SATELLITE_SRCDIR)/Repository.h \
	$(SATELLITE_SRCDIR)/Scanner.h

API_HDRS = $(filter-out $(INTERNAL_HDRS),$(wildcard $(SATELLITE_SRCDIR)/*.h))
