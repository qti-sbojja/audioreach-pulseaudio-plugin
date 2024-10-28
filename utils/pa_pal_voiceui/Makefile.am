AM_CPPFLAGS = -Wundef \
        -Wno-error \
        -Wno-trigraphs \
        -g -O0 \
        -fno-short-enums \
        -fpic \
        -I .

lib_LTLIBRARIES = libpapalvoiceui.la
libpapalvoiceui_la_SOURCES = pa_pal_voiceui.cpp
libpapalvoiceui_la_CPPFLAGS = $(AM_CPPFLAGS) -DNDEBUG -D__unused=__attribute__\(\(__unused__\)\) @PAL_CFLAGS@ @VUI_INTERFACE_HEADERS_CFLAGS@ @AUDIOLISTEN_CFLAGS@ -std=c++11
libpapalvoiceui_la_CPPFLAGS +=  $(GIO_CFLAGS)
libpapalvoiceui_la_LDFLAGS = -shared -module -avoid-version
libpapalvoiceui_la_LDFLAGS += $(GIO_LIBS)

###Generate test app ####
bin_PROGRAMS = pa_pal_voiceui_test
pa_pal_voiceui_test_SOURCES = pa_pal_voiceui_test.cpp
pa_pal_voiceui_test_CPPFLAGS = $(AM_CPPFLAGS) -D__unused=__attribute__\(\(__unused__\)\) @PAL_CFLAGS@ @VUI_INTERFACE_HEADERS_CFLAGS@ @AUDIOLISTEN_CFLAGS@ -std=c++11
pa_pal_voiceui_test_CPPFLAGS += -Dstrlcpy=g_strlcpy $(GLIB_CFLAGS) -include glib.h
pa_pal_voiceui_test_LDADD = libpapalvoiceui.la $(GLIB_LIBS)
