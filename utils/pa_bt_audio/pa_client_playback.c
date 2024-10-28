/**
*=============================================================================
* \file pa_client_playback.c
*
* \brief
*     Defines APIs for ringtone playback on pulseaudio sink
*
* \copyright
*  Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
*  SPDX-License-Identifier: BSD-3-Clause-Clear
*
*=============================================================================
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <pulse/pulseaudio.h>
#include <pulsecore/core-util.h>
#include <pulsecore/sample-util.h>
#include <gio/gio.h>
#include <errno.h>
#include "pa_bt_audio_client_wrapper.h"

#define E_SUCCESS                   0
#define E_FAILURE                   -1
#define PLAY_BUFFER_ATTR_PREBUF     30
#define PLAY_BUFFER_ATTR_TLENGTH    1024
#define PLAY_BUFFER_ATTR_MINREQ     100
#define PLAY_BUFFER_ATTR_MAXLENGTH  ((uint32_t) -1)

/* Global variables */
static char *g_bufptr = NULL;
static void *partialframe_buffer = NULL;
static size_t g_bufsize = 0;
static size_t partialframe_length = 0;
static bool write_done = false;

static pa_context *context = NULL;
static pa_stream *stream = NULL;
static pa_mainloop_api *mainloop_api = NULL;
static pa_mainloop* ml = NULL;

static pa_sample_spec sample_spec;
static pa_channel_map chmap;
static char *playback_sink = NULL;

/* Callbacks and helper functions */
static void stream_underflow_cb(pa_stream *s, void *userdata)
{
    g_debug("Stream underrun detected\n");
}

static void stream_overflow_cb(pa_stream *s, void *userdata)
{
    g_debug("Stream overrun detected\n");
}

static void stream_ctx_disconnect(pa_stream *s, int is_success, void *userdata)
{
    pa_assert(s);

    if (!is_success) {
        g_printerr("Failed to drain stream: %s", pa_strerror(pa_context_errno(context)));
        mainloop_api->quit(mainloop_api, 1);
    }

    pa_stream_disconnect(s);
    pa_context_disconnect(context);
}

static void stream_drain(void)
{
    if (stream) {
        pa_operation *op;

        pa_stream_set_write_callback(stream, NULL, NULL);

        if (!(op = pa_stream_drain(stream, stream_ctx_disconnect, NULL))) {
            g_printerr("pa_stream_drain(): %s", pa_strerror(pa_context_errno(context)));
            mainloop_api->quit(mainloop_api, 1);
            return;
        }

        pa_stream_unref(stream);
        stream = NULL;
        pa_operation_unref(op);
    } else
        mainloop_api->quit(mainloop_api, 0);
}

static void stream_write_cb(pa_stream *s, size_t length, void *userdata)
{
    uint8_t *buf = NULL;
    size_t writable_size, write_bytes, r;
    int avail_for_write;

    /* Query for writable size if stream is ready */
    if (!stream || pa_stream_get_state(stream) != PA_STREAM_READY ||
        !(writable_size = pa_stream_writable_size(stream))) {
        g_debug("%s: Stream not ready !!\n", __func__);
        return;
    }

    /* Get the buffer allocated */
    if (pa_stream_begin_write(stream, (void **)&buf, &writable_size) < 0) {
        g_printerr("pa_stream_begin_write() failed: %s",
                pa_strerror(pa_context_errno(context)));
        mainloop_api->quit(mainloop_api, 1);
        return;
    }

    /* Append the partial frame cached during previous write */
    if (partialframe_length) {
        pa_assert(partialframe_length < pa_frame_size(&sample_spec));
        memcpy(buf, partialframe_buffer, partialframe_length);
    }

    if (g_bufsize <= (writable_size - partialframe_length)) {
        avail_for_write = g_bufsize;
    }
    else {
        avail_for_write = (writable_size - partialframe_length);
    }

    if (avail_for_write > 0) {
        memcpy(buf + partialframe_length, g_bufptr, avail_for_write);
    }
    else {
        g_debug("End of data stream reached..");
        stream_drain();
        write_done = true;
        return;
    }
    r = avail_for_write;
    g_bufptr += r;
    r += partialframe_length;
    g_bufsize -= avail_for_write;

    /* Cache the resudual frames for next write */
    write_bytes = pa_frame_align(r, &sample_spec);
    partialframe_length = r - write_bytes;

    if (partialframe_length)
        memcpy(partialframe_buffer, buf + write_bytes, partialframe_length);

    if (write_bytes) {
        g_debug("%s: Writing %ld\n", __func__, write_bytes);
        if (pa_stream_write(stream, buf, write_bytes, NULL, 0, PA_SEEK_RELATIVE) < 0) {
            g_printerr("pa_stream_write() failed: %s",
                    pa_strerror(pa_context_errno(context)));
            mainloop_api->quit(mainloop_api, 1);
            return;
        }
    } else
        pa_stream_cancel_write(stream);

    if (g_bufsize <= 0)
        write_done = true;
}

static void stream_state_cb(pa_stream *s, void *userdata)
{
    pa_assert(s);

    switch (pa_stream_get_state(s)) {
        case PA_STREAM_CREATING:
        case PA_STREAM_TERMINATED:
            break;

        case PA_STREAM_READY:
            {
                const pa_buffer_attr *a;
                char cmt[PA_CHANNEL_MAP_SNPRINT_MAX], sst[PA_SAMPLE_SPEC_SNPRINT_MAX],
                fst[PA_FORMAT_INFO_SNPRINT_MAX];

                g_debug("Stream successfully created.");

                if (!(a = pa_stream_get_buffer_attr(s)))
                    g_printerr("pa_stream_get_buffer_attr() failed: %s", \
                            pa_strerror(pa_context_errno(pa_stream_get_context(s))));
                else {
                    g_debug("Buffer metrics: maxlength=%u, tlength=%u, prebuf=%u, \
                            minreq=%u", a->maxlength, a->tlength, a->prebuf, a->minreq);
                }

                g_debug("Using sample spec '%s', channel map '%s'.",
                        pa_sample_spec_snprint(sst, sizeof(sst),
                            pa_stream_get_sample_spec(s)),
                        pa_channel_map_snprint(cmt, sizeof(cmt),
                            pa_stream_get_channel_map(s)));

                g_debug("Connected to playback_sink %s (index: %u, suspended: %s).",
                        pa_stream_get_device_name(s),
                        pa_stream_get_device_index(s),
                        pa_yes_no(pa_stream_is_suspended(s)));

                break;
            }

        case PA_STREAM_FAILED:
        default:
            g_printerr("Stream error: %s",
                    pa_strerror(pa_context_errno(pa_stream_get_context(s))));
            mainloop_api->quit(mainloop_api, 1);
    }
}

static void pa_context_state_cb(pa_context *ctx, void *userdata)
{
    pa_assert(ctx);

    switch (pa_context_get_state(ctx)) {
        case PA_CONTEXT_READY: {
            pa_buffer_attr buffer_attr;
            pa_assert(!stream);

            g_debug("Connection established with server\n");

            stream = pa_stream_new(ctx, "btaudio", &sample_spec, &chmap);
            if (!stream) {
                g_printerr("pa_stream_new() failed: %s", pa_strerror(pa_context_errno(ctx)));
                goto fail;
            }

            pa_stream_set_state_callback(stream, stream_state_cb, NULL);
            pa_stream_set_write_callback(stream, stream_write_cb, NULL);
            pa_stream_set_underflow_callback(stream, stream_underflow_cb, NULL);
            pa_stream_set_overflow_callback(stream, stream_overflow_cb, NULL);

            pa_zero(buffer_attr);
            buffer_attr.maxlength = PLAY_BUFFER_ATTR_MAXLENGTH;
            buffer_attr.prebuf = PLAY_BUFFER_ATTR_PREBUF;
            buffer_attr.fragsize = buffer_attr.tlength = PLAY_BUFFER_ATTR_TLENGTH;
            buffer_attr.minreq = PLAY_BUFFER_ATTR_MINREQ;

            if (pa_stream_connect_playback(stream, playback_sink, &buffer_attr, 0, NULL,
                        NULL) < 0) {
                g_printerr("pa_stream_connect_playback() failed: %s",
                        pa_strerror(pa_context_errno(ctx)));
                goto fail;
            }
            break;
        }

        case PA_CONTEXT_TERMINATED:
            mainloop_api->quit(mainloop_api, 0);
            break;

        case PA_CONTEXT_FAILED:
            g_printerr("Connection failure: %s", pa_strerror(pa_context_errno(ctx)));
            goto fail;
        default:
            break;
    }

    return;

fail:
    mainloop_api->quit(mainloop_api, 1);
}

void mainloop_setup(void)
{
    int ret = 1;

    if (!(ml = pa_mainloop_new())) {
        g_printerr("pa_mainloop_new() failed.");
        goto quit;
    }

    mainloop_api = pa_mainloop_get_api(ml);

    pa_assert_se(pa_signal_init(mainloop_api) == 0);
    pa_disable_sigpipe();

    /* Create context */
    if (!(context = pa_context_new(mainloop_api, "btapp"))) {
        g_printerr("pa_context_new() failed.");
        goto quit;
    }

    pa_context_set_state_callback(context, pa_context_state_cb, NULL);

    /* Connect the context */
    if (pa_context_connect(context, NULL, 0, NULL) < 0) {
        g_printerr("pa_context_connect() failed: %s",
                pa_strerror(pa_context_errno(context)));
        goto quit;
    }

    /* Run main loop */
    if (pa_mainloop_run(ml, &ret) < 0) {
        g_printerr("pa_mainloop_run() failed.");
        write_done = false;
    }

    g_bufsize = 0;
    g_bufptr = NULL;
    g_debug("Quitting from the mainloop\n");
    pa_mainloop_quit(ml, 0);
    pa_signal_done();
    pa_mainloop_free(ml);
    ml = NULL;

quit:
    return;
}

/* Library functions */
int pa_sink_init(char *sink_name, unsigned int bit_depth,
        unsigned int sampling_rate, unsigned int channel, pa_audio_format_t format)
{
    int ret = E_SUCCESS;

    g_debug("%s: Entry\n", __func__);

    if (format != PA_BT_PCM_FORMAT) {
        g_printerr("Format not supported");
        ret = -EINVAL;
        goto quit;
    }

    if (!sink_name)
        playback_sink = NULL;
    else
        playback_sink = pa_xstrdup(sink_name);

    sample_spec.rate = sampling_rate;
    sample_spec.channels = channel;
    sample_spec.format = (bit_depth == 16) ? PA_SAMPLE_S16LE : PA_SAMPLE_INVALID;

    if (!pa_sample_spec_valid(&sample_spec)) {
        g_printerr("Invalid sample specification");
        ret = -EINVAL;
        goto quit;
    }

    pa_channel_map_init_extend(&chmap, sample_spec.channels, PA_CHANNEL_MAP_DEFAULT);

    if (!pa_channel_map_compatible(&chmap, &sample_spec)) {
        g_printerr("Channel map doesn't match sample specification");
        ret = E_FAILURE;
        goto quit;
    }

    partialframe_buffer = pa_xmalloc(pa_frame_size(&sample_spec));
    if (!partialframe_buffer) {
        g_printerr("Allocation failed !!\n");
        ret = -ENOMEM;
        goto quit;
    }

    g_debug("%s: Exit\n", __func__);

quit:
    return ret;
}

bool pa_sink_play(char *buffer, size_t bytes)
{
    int ret = 1;
    pa_assert(buffer);

    g_debug("%s: Entry\n", __func__);

    g_bufsize = bytes;
    g_bufptr = buffer;
    write_done = false;
    g_debug("%s, %d, %ld, %p\n", __func__, __LINE__, bytes, buffer);

    mainloop_setup();

    partialframe_length = 0;
    g_bufsize = 0;
    g_bufptr = NULL;

    g_debug("%s: Exit\n", __func__);

    return write_done;
}

void pa_sink_deinit(void)
{
    g_debug("%s: Entry\n", __func__);

    if (stream) {
        if (!write_done)
            stream_drain();
        pa_stream_unref(stream);
    }

    if (context)
        pa_context_unref(context);

    if (partialframe_buffer)
        pa_xfree(partialframe_buffer);

    g_debug("%s: Exit\n", __func__);
}
