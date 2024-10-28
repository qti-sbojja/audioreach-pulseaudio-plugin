pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = pabtaudio.pc

EXTRA_DIST = $(pkgconfig_DATA)

AM_CFLAGS = -Wundef \
        -Werror \
        -Wstrict-prototypes \
        -Wno-trigraphs \
        -g -O0 \
        -fno-short-enums \
        -fpic \
        -I .

lib_includedir = $(includedir)/pabt_headers
lib_include_HEADERS = $(top_srcdir)/pa_bt_audio_client_wrapper.h

header_includes = -I $(top_srcdir)/inc

lib_LTLIBRARIES = libpa_bt_audio_client_wrapper.la
libpa_bt_audio_client_wrapper_la_CC = @CC@
libpa_bt_audio_client_wrapper_la_SOURCES = pa_bt_audio_client_wrapper.c
libpa_bt_audio_client_wrapper_la_SOURCES += pa_client_playback.c
libpa_bt_audio_client_wrapper_la_CFLAGS = $(AM_CFLAGS) $(header_includes) $(GIO_CFLAGS)
libpa_bt_audio_client_wrapper_la_LDFLAGS = -shared -module -avoid-version $(GIO_LIBS) @LIBPULSE_CFLAGS@
libpa_bt_audio_client_wrapper_la_LIBADD = @LIBPULSE_LIBS@
