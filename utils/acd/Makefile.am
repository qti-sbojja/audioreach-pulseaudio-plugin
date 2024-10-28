AM_CPPFLAGS = -Wundef \
        -Wno-error \
        -Wno-trigraphs \
        -g -O0 \
        -fno-short-enums \
        -fpic \
        -I .
lib_LTLIBRARIES = libpapalacd.la
libpapalacd_la_SOURCES = pa_pal_acd.cpp
libpapalacd_la_CPPFLAGS = @VUI_INTF_HEADERS_CFLAGS@ @AUDIOLISTEN_CFLAGS@
libpapalacd_la_CPPFLAGS += $(AM_CPPFLAGS) -DNDEBUG -D__unused=__attribute__\(\(__unused__\)\) @PAL_CFLAGS@ -std=c++11
libpapalacd_la_CPPFLAGS +=  $(GIO_CFLAGS)
libpapalacd_la_LDFLAGS = -shared -module -avoid-version
libpapalacd_la_LDFLAGS += $(GIO_LIBS)

###Generate test app ####
bin_PROGRAMS = pa_pal_acd_test
pa_pal_acd_test_SOURCES = pa_pal_acd_test.cpp
pa_pal_acd_test_CPPFLAGS = $(AM_CPPFLAGS) -D__unused=__attribute__\(\(__unused__\)\) @PAL_CFLAGS@ -std=c++11
pa_pal_acd_test_CPPFLAGS +=  $(GIO_CFLAGS)
pa_pal_acd_test_CPPFLAGS += -Dstrlcpy=g_strlcpy $(GLIB_CFLAGS) -include glib.h
pa_pal_acd_test_CPPFLAGS += @VUI_INTF_HEADERS_CFLAGS@ @AUDIOLISTEN_CFLAGS@
pa_pal_acd_test_LDADD = libpapalacd.la $(GLIB_LIBS)
