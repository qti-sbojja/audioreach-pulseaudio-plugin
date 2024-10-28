/**
*=============================================================================
* \file pa_bt_audio_client_wrapper.c
*
* \brief
*     Defines interface APIs, structs and enums for communication between pulseaudio and BT app/daemon
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
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include <gio/gio.h>

#include "pa_bt_audio_client_wrapper.h"

#define PA_PAL_DBUS_OBJECT_PATH_SIZE                    256
#define PA_PAL_JACK_PARAM_VALUE_SIZE                    10
#define PA_PAL_JACK_PARAM_SIZE                          35
#define USECASE_TYPE_BTSINK                             "btsink"
#define USECASE_TYPE_BTSRC                              "btsource"

/* Dbus interface handles for loopback methods */
#define PA_PAL_LOOPBACK_DBUS_OBJECT_PATH                "/org/pulseaudio/ext/pal/loopback"
#define PA_PAL_LOOPBACK_DBUS_MODULE_IFACE               "org.PulseAudio.Ext.Loopback"
#define PA_PAL_LOOPBACK_DBUS_SESSION_IFACE              "org.PulseAudio.Ext.Loopback.Session"

/* Dbus interface handles for external jack connections */
#define PA_PAL_A2DP_IN_PORT_DBUS_OBJECT_PATH_PREFIX     "/org/pulseaudio/ext/pal/port/bta2dp_in"
#define PA_PAL_A2DP_OUT_PORT_DBUS_OBJECT_PATH_PREFIX    "/org/pulseaudio/ext/pal/port/bta2dp_out"
#define PA_PAL_SCO_OUT_PORT_DBUS_OBJECT_PATH_PREFIX     "/org/pulseaudio/ext/pal/port/btsco_out"
#define PA_PAL_SCO_IN_PORT_DBUS_OBJECT_PATH_PREFIX      "/org/pulseaudio/ext/pal/port/btsco_in"
#define PA_PAL_EXTERNAL_JACK_DBUS_IFACE                 "org.PulseAudio.Ext.Pal.Module"
#define MAX_USECASE_LENGTH 60

/* dbus call response timeout */
#define PA_BT_DBUS_ASYNC_METHOD_TIMEOUT_MS              3000

/******* Global variables ********/
static struct pa_bt_client_module_data {
    GDBusConnection *conn;
    GHashTable *ses_hash_table;
    char obj_path[PA_PAL_DBUS_OBJECT_PATH_SIZE];
} *g_mod_data;

static struct pa_bt_async_method_data {
    GThread *thread_loop;
    GMainLoop *loop;
    guint sub_id_sb_event;
    GMutex mutex;
    GCond cond;
    int success;
} *pa_bt_async_data;

typedef struct {
    char *obj_path;
    char usecase[MAX_USECASE_LENGTH];
} pa_pal_loopback_session_data_t;

typedef struct {
    audio_param_key_t key;
    char *value;
} audio_prm_kvpair_t;

/* Helper functions */
struct pa_bt_async_method_data* allocate_async_data(void)
{
    pa_bt_async_data = calloc(1, sizeof(struct pa_bt_async_method_data));
    if (!pa_bt_async_data)
        return NULL;

    g_mutex_init(&pa_bt_async_data->mutex);
    g_cond_init(&pa_bt_async_data->cond);

    g_debug("%s\n", __func__);
    return pa_bt_async_data;
}

void free_async_data(void)
{
    g_cond_clear(&pa_bt_async_data->cond);
    g_mutex_clear(&pa_bt_async_data->mutex);

    if (pa_bt_async_data)
        free(pa_bt_async_data);

    g_debug("%s\n", __func__);
    pa_bt_async_data = NULL;
}

static void on_jack_setparam_done_event(GDBusConnection *conn, const gchar *sender_name,
        const gchar *object_path, const gchar *interface_name, const gchar *signal_name,
        GVariant *parameters, gpointer data)
{
    struct pa_bt_async_method_data *async_data = (struct pa_bt_async_method_data *)data;
    GError *error = NULL;
    GVariantIter arg_i;
    gint status = 0;

    g_debug("Set param done event received\n");
    g_variant_iter_init(&arg_i, parameters);
    g_variant_iter_next(&arg_i, "i", &status);

    g_mutex_lock(&async_data->mutex);
    async_data->success = status;
    g_debug("Jack set_param status=%d. Waking up method calling thread\n", status);
    g_cond_signal(&async_data->cond);
    g_mutex_unlock(&async_data->mutex);
}

static void *signal_threadloop(void *cookie) {
    struct pa_bt_async_method_data *ses_data = (struct pa_bt_async_method_data *)cookie;

    if (!ses_data) {
        g_printerr("Invalid thread params");
        goto exit;
    }

    ses_data->loop = g_main_loop_new(NULL, FALSE);
    g_debug("Initiate main loop run for subscription id %d\n", ses_data->sub_id_sb_event);
    g_main_loop_run(ses_data->loop);

    g_main_loop_unref(ses_data->loop);

exit:
    return NULL;
}

static int subscribe_set_param_done_event(const char *obj_path, bool subscribe)
{
    GVariant *result;
    GVariant *argument_sig_listener = NULL;
    GError *error = NULL;
    guint id;
    char signal_name[128];
    gint ret = 0;

    if (!g_mod_data || !pa_bt_async_data)
        return -1;

    g_snprintf(g_mod_data->obj_path, PA_PAL_DBUS_OBJECT_PATH_SIZE, "%s", obj_path);

    g_snprintf(signal_name, sizeof(signal_name),
            "%s.%s", PA_PAL_EXTERNAL_JACK_DBUS_IFACE, "JackSetParamDone");

    if (subscribe) {
        pa_bt_async_data->thread_loop = g_thread_try_new("signallistener", signal_threadloop,
                pa_bt_async_data, &error);

        if (!pa_bt_async_data->thread_loop) {
            g_error("Could not create thread %s, error %s\n", "signallistener", error->message);
            g_error_free(error);
        }

        /* Add listener for signal to PulseAudio core */
        const gchar *obj_str[] = {};
        argument_sig_listener = g_variant_new("(@s@ao)",
                g_variant_new_string(signal_name),
                g_variant_new_objv(obj_str, 0));

        result = g_dbus_connection_call_sync(g_mod_data->conn,
                NULL,
                "/org/pulseaudio/core1",
                "org.PulseAudio.Core1",
                "ListenForSignal",
                argument_sig_listener,
                NULL,
                G_DBUS_CALL_FLAGS_NONE,
                -1,
                NULL,
                &error);
        if (result == NULL) {
            g_printerr ("Error invoking ListenForSignal(): %s\n", error->message);
            g_error_free(error);
            ret = -EINVAL;
            goto exit;
        }

        g_variant_unref(result);

        /* subscribe for detection event signal */
        g_debug("Subscribe for the signal on Obj path- %s\n", g_mod_data->obj_path);
        pa_bt_async_data->sub_id_sb_event = g_dbus_connection_signal_subscribe(g_mod_data->conn,
                NULL,
                PA_PAL_EXTERNAL_JACK_DBUS_IFACE,
                "JackSetParamDone",
                g_mod_data->obj_path,
                NULL,
                G_DBUS_SIGNAL_FLAGS_NONE,
                on_jack_setparam_done_event,
                pa_bt_async_data,
                NULL);
    } else {
        /* Remove signal listener to PulseAudio core */
        argument_sig_listener = g_variant_new("(@s)", g_variant_new_string(signal_name));
        result = g_dbus_connection_call_sync(g_mod_data->conn,
                NULL,
                "/org/pulseaudio/core1",
                "org.PulseAudio.Core1",
                "StopListeningForSignal",
                argument_sig_listener,
                NULL,
                G_DBUS_CALL_FLAGS_NONE,
                -1,
                NULL,
                &error);
        if (result == NULL) {
            g_printerr ("Error invoking ListenForSignal(): %s\n", error->message);
            g_error_free(error);
            ret = -EINVAL;
            goto exit;
        }

        g_variant_unref(result);

        if (pa_bt_async_data->sub_id_sb_event) {
            g_debug("UnSubscribe for the signal on Obj path- %s\n", g_mod_data->obj_path);
            g_dbus_connection_signal_unsubscribe(g_mod_data->conn, pa_bt_async_data->sub_id_sb_event);
        }

        if (pa_bt_async_data->thread_loop) {
            g_main_loop_quit(pa_bt_async_data->loop);
            g_thread_join(pa_bt_async_data->thread_loop);
            pa_bt_async_data->thread_loop = NULL;
        }
    }

exit:
    return ret;
}

static int get_mod_data(bool is_bt_src_usecase)
{
    int ret = 0;
    const gchar *s_address = NULL;
    GError *error = NULL;

    g_debug("%s: Entry\n", __func__);
    g_mod_data = g_malloc0(sizeof(struct pa_bt_client_module_data));
    if (g_mod_data == NULL) {
        g_printerr("Could not allocate memory for module data\n");
        ret = -ENOMEM;
        goto exit;
    }

    s_address = getenv("PULSE_DBUS_SERVER");
    if (!s_address) {
        g_info("Unable to obtain server address, using default address\n");
        g_mod_data->conn = g_dbus_connection_new_for_address_sync("unix:path=/var/run/pulse/dbus-socket",
                G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT, NULL, NULL, &error);
    } else {
        g_info("server address %s\n", s_address);
        g_mod_data->conn = g_dbus_connection_new_for_address_sync(s_address,
                G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT, NULL, NULL, &error);
    }

    if (g_mod_data->conn == NULL) {
        g_printerr("Error connecting to D-Bus address %s: %s\n", s_address,
                error->message);
        ret = E_FAILURE;
        goto exit;
    }

    /* Allocate async thread data for BT source usecases */
    if (is_bt_src_usecase && !pa_bt_async_data) {
        if (!allocate_async_data()) {
            return E_FAILURE;
        }
    } else {
        /* Allocate hash table for BT sink use cases */
        g_mod_data->ses_hash_table = g_hash_table_new(g_direct_hash, g_direct_equal);
    }
    g_debug("%s: Exit\n", __func__);

exit:
    return ret;
}

static void free_mod_data(void)
{
    GError *error = NULL;
    g_debug("%s: Entry\n", __func__);
    if (g_mod_data && g_mod_data->conn) {
        if (!g_dbus_connection_close_sync(g_mod_data->conn, NULL, &error)) {
            g_printerr("Error in connection close(): %s\n", error->message);
        }
    }
    if (g_mod_data && g_mod_data->ses_hash_table) {
        g_hash_table_destroy(g_mod_data->ses_hash_table);
    }
    g_free(g_mod_data);
    g_mod_data = NULL;

    if (pa_bt_async_data) {
        free_async_data();
    }
    g_debug("%s: Exit\n", __func__);
}

static int parse_keyidx(const char *keystr)
{
    int key_idx = 0;
    for (key_idx = 1; key_idx < AUDIO_PARAMETER_KEY_MAX; key_idx++) {
        if (!strcmp(keystr, audio_prmkey_names[key_idx])) {
            break;
        }
    }

    if ((key_idx > 0) && (key_idx < AUDIO_PARAMETER_KEY_MAX))
        return key_idx;
    else
        return -1;
}

static int parse_kvpair(const char *kvpair, audio_prm_kvpair_t **kv)
{
    int ret = E_SUCCESS;
    int key_idx = 0;
    char *key_name, *value;
    char *kvstr = strdup(kvpair);
    char *tmpstr;

    key_name = strtok_r(kvstr, "=", &tmpstr);

    if (key_name == NULL) {
        ret = -EINVAL;
        goto exit;
    }
    key_idx = parse_keyidx(key_name);
    if (key_idx != -1) {
        value = strtok_r(NULL, "=", &tmpstr);
        if (value == NULL) {
            ret = -EINVAL;
            goto exit;
        }
        (*kv)->value = strdup(value);
        (*kv)->key = key_idx;
    }
    else {
        ret = -EINVAL;
    }

exit:
    free(kvstr);
    return ret;
}

static int setup_loopback(pa_pal_loopback_session_data_t *ses_data, char *value)
{
    int ret = E_SUCCESS;
    GVariant *result = NULL;
    GError *error = NULL;

    if (ses_data == NULL) {
        return -EINVAL;
    }

    if (ses_data == NULL) {
        return -EINVAL;
    }

    if (!strncmp(value, "true", strlen(value))) {
        result = g_dbus_connection_call_sync(g_mod_data->conn,
                NULL,
                ses_data->obj_path,
                PA_PAL_LOOPBACK_DBUS_SESSION_IFACE,
                "CreateLoopback",
                NULL,
                NULL,
                G_DBUS_CALL_FLAGS_NONE,
                -1,
                NULL,
                &error);

        if (result == NULL) {
            g_printerr("Unable to start %s: %s\n", ses_data->usecase, error->message);
            g_error_free(error);
            ret = E_FAILURE;
        }
    }
    else if (!strncmp(value, "false", strlen(value))) {
        result = g_dbus_connection_call_sync(g_mod_data->conn,
                NULL,
                ses_data->obj_path,
                PA_PAL_LOOPBACK_DBUS_SESSION_IFACE,
                "DestroyLoopback",
                NULL,
                NULL,
                G_DBUS_CALL_FLAGS_NONE,
                -1,
                NULL,
                &error);
        if (result == NULL) {
            g_printerr("Unable to stop %s: %s\n", ses_data->usecase, error->message);
            g_error_free(error);
            ret = E_FAILURE;
        }
    }
    else {
        g_printerr("%s is invalid param for %s\n", value, __func__);
        ret = E_FAILURE;
    }

    if (result) {
        g_variant_unref(result);
    }

    return ret;
}

static int set_volume(pa_pal_loopback_session_data_t *ses_data,
        char *loopback_profile, float vol)
{
    int ret = E_SUCCESS;
    GVariant *result = NULL;
    GError *error = NULL;
    GVariant *value_1 = NULL;
    GVariantBuilder builder_1;
    GVariant *argument = NULL;

    g_variant_builder_init(&builder_1, G_VARIANT_TYPE("(ds)"));
    g_variant_builder_add(&builder_1, "d", (gdouble)vol);
    g_variant_builder_add(&builder_1, "s", loopback_profile);
    value_1 = g_variant_builder_end(&builder_1);

    argument = g_variant_new("(@(ds))", value_1);

    if (ses_data == NULL) {
        return -EINVAL;
    }

    if (ses_data == NULL) {
        return -EINVAL;
    }

    g_debug("Calling SetVolume\n");
    result = g_dbus_connection_call_sync(g_mod_data->conn,
            NULL,
            ses_data->obj_path,
            PA_PAL_LOOPBACK_DBUS_SESSION_IFACE,
            "SetVolume",
            argument,
            NULL,
            G_DBUS_CALL_FLAGS_NONE,
            -1,
            NULL,
            &error);

    if (result == NULL) {
        g_printerr("Unable to set volume: %s\n", error->message);
        g_error_free(error);
        ret = E_FAILURE;
    }
    else {
        g_variant_unref(result);
    }

    return ret;
}

static int set_mute(pa_pal_loopback_session_data_t *ses_data,
        char *loopback_profile, char *mute_val)
{
    int ret = E_SUCCESS;
    bool is_mute = false;
    GVariant *result = NULL;
    GError *error = NULL;
    GVariant *value_1 = NULL;
    GVariantBuilder builder_1;
    GVariant *argument = NULL;

    is_mute = (!strncmp(mute_val, "true", strlen(mute_val))) ? true : false;
    g_variant_builder_init(&builder_1, G_VARIANT_TYPE("(bs)"));
    g_variant_builder_add(&builder_1, "b", (gboolean)is_mute);
    g_variant_builder_add(&builder_1, "s", loopback_profile);
    value_1 = g_variant_builder_end(&builder_1);

    if (ses_data == NULL) {
        return -EINVAL;
    }

    if (ses_data == NULL) {
        return -EINVAL;
    }

    argument = g_variant_new("(@(bs))", value_1);

    g_debug("Calling SetMute\n");
    result = g_dbus_connection_call_sync(g_mod_data->conn,
            NULL,
            ses_data->obj_path,
            PA_PAL_LOOPBACK_DBUS_SESSION_IFACE,
            "SetMute",
            argument,
            NULL,
            G_DBUS_CALL_FLAGS_NONE,
            -1,
            NULL,
            &error);

    if (result == NULL) {
        g_printerr("Unable to set mute: %s\n", error->message);
        g_error_free(error);
        ret = E_FAILURE;
    }
    else {
        g_variant_unref(result);
    }

    return ret;
}

static int set_sample_rate_loopback(pa_pal_loopback_session_data_t *ses_data,
        uint32_t sample_rate)
{
    int ret = E_SUCCESS;
    GVariant *result = NULL;
    GError *error = NULL;
    GVariant *argument = NULL;

    g_debug("%s:%d\n", __func__, __LINE__);
    argument = g_variant_new("(@u)", g_variant_new_uint32(sample_rate));

    if (ses_data == NULL) {
        return -EINVAL;
    }

    if (ses_data == NULL) {
        return -EINVAL;
    }

    g_debug("Calling SetSampleRate\n");
    result = g_dbus_connection_call_sync(g_mod_data->conn,
            NULL,
            ses_data->obj_path,
            PA_PAL_LOOPBACK_DBUS_SESSION_IFACE,
            "SetSampleRate",
            argument,
            NULL,
            G_DBUS_CALL_FLAGS_NONE,
            -1,
            NULL,
            &error);

    if (result == NULL) {
        g_printerr("Unable to set SampleRate: %s\n", error->message);
        g_error_free(error);
        ret = E_FAILURE;
    }
    else {
        g_variant_unref(result);
    }

    return ret;
}

static int set_sample_rate_jack(const char *obj_path, char *samplerate)
{
    int ret = E_SUCCESS;
    GVariant *result = NULL;
    GError *error = NULL;
    GVariant *argument = NULL;
    gint64 start_time, end_time;
    char param[PA_PAL_JACK_PARAM_SIZE];

    g_snprintf(g_mod_data->obj_path, PA_PAL_DBUS_OBJECT_PATH_SIZE, "%s", obj_path);
    g_debug("Obj path- %s\n", g_mod_data->obj_path);

    g_snprintf(param, PA_PAL_JACK_PARAM_SIZE, "sample_rate=%s", samplerate);
    argument = g_variant_new("(@s)", g_variant_new_string(param));

    g_debug("Calling hfp_ag samplerate\n");
    result = g_dbus_connection_call_sync(g_mod_data->conn,
            NULL,
            g_mod_data->obj_path,
            PA_PAL_EXTERNAL_JACK_DBUS_IFACE,
            "SetParam",
            argument,
            NULL,
            G_DBUS_CALL_FLAGS_NONE,
            -1,
            NULL,
            &error);

    if (result == NULL) {
        g_printerr ("Error in setting samplerate %s: %s\n", samplerate, error->message);
        ret = E_FAILURE;
    }

    start_time = g_get_monotonic_time();
    end_time = start_time + (PA_BT_DBUS_ASYNC_METHOD_TIMEOUT_MS * G_TIME_SPAN_MILLISECOND);
    g_mutex_lock(&pa_bt_async_data->mutex);
    g_cond_wait_until(&pa_bt_async_data->cond, &pa_bt_async_data->mutex, end_time);
    if (g_get_monotonic_time() >= end_time) {
        g_printerr("Async method timeout %ld and %ld\n", g_get_monotonic_time(), end_time);
        ret = -ETIMEDOUT;
    }

    if (pa_bt_async_data->success) {
        g_printerr("Set param failed\n");
        ret = -1;
    }

    g_mutex_unlock(&pa_bt_async_data->mutex);
    g_variant_unref(result);

    return ret;
}

static int jack_bt_set_a2dp_stream_suspend(const char *obj_path, char *is_suspend)
{
    int ret = E_SUCCESS;
    GVariant *result = NULL;
    GError *error = NULL;
    GVariant *argument = NULL;
    gint64 start_time, end_time;
    char param[PA_PAL_JACK_PARAM_SIZE];

    g_snprintf(g_mod_data->obj_path, PA_PAL_DBUS_OBJECT_PATH_SIZE, "%s", obj_path);
    g_debug("Obj path- %s\n", g_mod_data->obj_path);

    g_snprintf(param, PA_PAL_JACK_PARAM_SIZE, "a2dp_suspend=%s", is_suspend);
    argument = g_variant_new("(@s)", g_variant_new_string(param));

    g_debug("Calling A2dpSuspend\n");
    result = g_dbus_connection_call_sync(g_mod_data->conn,
            NULL,
            g_mod_data->obj_path,
            PA_PAL_EXTERNAL_JACK_DBUS_IFACE,
            "SetParam",
            argument,
            NULL,
            G_DBUS_CALL_FLAGS_NONE,
            -1,
            NULL,
            &error);

    if (result == NULL) {
        g_printerr ("Error in setting a2dp suspend %s: %s\n", is_suspend, error->message);
        ret = E_FAILURE;
        goto exit;
    }

    start_time = g_get_monotonic_time();
    end_time = start_time + (PA_BT_DBUS_ASYNC_METHOD_TIMEOUT_MS * G_TIME_SPAN_MILLISECOND);
    g_mutex_lock(&pa_bt_async_data->mutex);
    g_cond_wait_until(&pa_bt_async_data->cond, &pa_bt_async_data->mutex, end_time);
    if (g_get_monotonic_time() >= end_time) {
        g_printerr("Async method timeout %ld and %ld\n", g_get_monotonic_time(), end_time);
        ret = -ETIMEDOUT;
    }

    if (pa_bt_async_data->success) {
        g_printerr("Set param failed\n");
        ret = -1;
    }

    g_mutex_unlock(&pa_bt_async_data->mutex);
    g_variant_unref(result);

exit:
    return ret;
}

static int get_sample_rate(pa_pal_loopback_session_data_t *ses_data,
        uint32_t *sample_rate)
{
    int ret = E_SUCCESS;
    GVariant *result = NULL;
    GError *error = NULL;
    GVariant *argument = NULL;

    if (ses_data == NULL) {
        return -EINVAL;
    }

    if (ses_data == NULL) {
        return -EINVAL;
    }

    g_debug("Calling GetSampleRate\n");
    result = g_dbus_connection_call_sync(g_mod_data->conn,
            NULL,
            ses_data->obj_path,
            PA_PAL_LOOPBACK_DBUS_SESSION_IFACE,
            "GetSampleRate",
            NULL,
            G_VARIANT_TYPE("(u)"),
            G_DBUS_CALL_FLAGS_NONE,
            -1,
            NULL,
            &error);

    if (result == NULL) {
        g_printerr("Unable to get sample rate: %s\n", error->message);
        g_error_free(error);
        ret = E_FAILURE;
        goto quit;
    }

    g_variant_get(result, "(u)", sample_rate);

quit:
    g_variant_unref(result);
    return ret;
}

static int get_volume(pa_pal_loopback_session_data_t *ses_data,
        char *lb_profile, double *vol)
{
    int ret = E_SUCCESS;
    GVariant *result = NULL;
    GError *error = NULL;
    GVariant *argument = NULL;

    if (ses_data == NULL) {
        return -EINVAL;
    }

    if (ses_data == NULL) {
        return -EINVAL;
    }

    argument = g_variant_new("(@s)", g_variant_new_string(lb_profile));

    g_debug("Calling GetVolume\n");
    result = g_dbus_connection_call_sync(g_mod_data->conn,
            NULL,
            ses_data->obj_path,
            PA_PAL_LOOPBACK_DBUS_SESSION_IFACE,
            "GetVolume",
            argument,
            G_VARIANT_TYPE("(d)"),
            G_DBUS_CALL_FLAGS_NONE,
            -1,
            NULL,
            &error);

    if (result == NULL) {
        g_printerr("Unable to get volume: %s\n", error->message);
        g_error_free(error);
        ret = E_FAILURE;
        goto quit;
    }

    g_variant_get(result, "(d)", vol);

quit:
    g_variant_unref(result);
    return ret;
}

static int jack_bt_connect(const char *obj_path, bool connect, bool is_btsrc)
{
    int ret = E_SUCCESS;
    GError *error = NULL;
    GVariant *argument = NULL;
    GVariant *result = NULL;

    g_snprintf(g_mod_data->obj_path, PA_PAL_DBUS_OBJECT_PATH_SIZE, "%s", obj_path);
    g_debug("Obj path- %s\n", g_mod_data->obj_path);

    argument = g_variant_new("(@b)", g_variant_new_boolean(connect));

    result = g_dbus_connection_call_sync(g_mod_data->conn,
            NULL,
            g_mod_data->obj_path,
            PA_PAL_EXTERNAL_JACK_DBUS_IFACE,
            "BtConnect",
            argument,
            NULL,
            G_DBUS_CALL_FLAGS_NONE,
            -1,
            NULL,
            &error);

    if (result == NULL) {
        g_printerr ("Error in establishing BT connection: %s\n", error->message);
        ret = E_FAILURE;
    }

    return ret;
}

static int jack_bt_set_connection_param(const char *obj_path, bool connect, bool is_btsrc)
{
    int ret = E_SUCCESS;
    GError *error = NULL;
    GVariant *argument = NULL;
    GVariant *result = NULL;
    gint64 start_time, end_time;
    char param_value[PA_PAL_JACK_PARAM_VALUE_SIZE];
    char param[PA_PAL_JACK_PARAM_SIZE];

    /* Set connection params using ext-jack intf for BTsource devices only */
    if (!is_btsrc)
        return E_SUCCESS;

    subscribe_set_param_done_event(obj_path, true);

    g_snprintf(g_mod_data->obj_path, PA_PAL_DBUS_OBJECT_PATH_SIZE, "%s", obj_path);
    g_debug("Obj path- %s\n", g_mod_data->obj_path);

    g_strlcpy(param_value, (connect ? "true" : "false"), PA_PAL_JACK_PARAM_VALUE_SIZE);
    g_snprintf(param, PA_PAL_JACK_PARAM_SIZE, "device_connection=%s", param_value);
    g_printerr("param is %s", param);
    argument = g_variant_new("(@s)", g_variant_new_string(param));

    result = g_dbus_connection_call_sync(g_mod_data->conn,
            NULL,
            g_mod_data->obj_path,
            PA_PAL_EXTERNAL_JACK_DBUS_IFACE,
            "SetParam",
            argument,
            NULL,
            G_DBUS_CALL_FLAGS_NONE,
            -1,
            NULL,
            &error);

    if (result == NULL) {
        g_printerr ("Error in setting device connection params: %s\n", error->message);
        ret = E_FAILURE;
        goto exit;
    }

    start_time = g_get_monotonic_time();
    end_time = start_time + (PA_BT_DBUS_ASYNC_METHOD_TIMEOUT_MS * G_TIME_SPAN_MILLISECOND);
    g_mutex_lock(&pa_bt_async_data->mutex);
    g_cond_wait_until(&pa_bt_async_data->cond, &pa_bt_async_data->mutex, end_time);
    if (g_get_monotonic_time() >= end_time) {
        g_printerr("Async method timeout %ld and %ld\n", g_get_monotonic_time(), end_time);
        ret = -ETIMEDOUT;
    }

    if (pa_bt_async_data->success) {
        g_printerr("Set param failed\n");
        ret = -1;
    }

    g_mutex_unlock(&pa_bt_async_data->mutex);
    subscribe_set_param_done_event(obj_path, false);
    g_variant_unref(result);

exit:
    return ret;
}

static int send_external_jack_connection_request(const char *obj_path,
        bool is_btsrc, bool connect)
{
    int ret = E_SUCCESS;

    if (connect) {
        ret = jack_bt_connect(obj_path, connect, is_btsrc);
        if (ret) {
            ret = E_FAILURE;
            goto exit;
        }
        ret = jack_bt_set_connection_param(obj_path, connect, is_btsrc);
        if (ret) {
            ret = E_FAILURE;
            goto exit;
        }
    }
    else {
        jack_bt_set_connection_param(obj_path, connect, is_btsrc);
        if (ret) {
            ret = E_FAILURE;
            goto exit;
        }
        jack_bt_connect(obj_path, connect, is_btsrc);
        if (ret) {
            ret = E_FAILURE;
            goto exit;
        }
    }

exit:
    return ret;
}

/* Library functions */
int pa_bt_connect(pa_bt_usecase_type_t usecase_type, bool connect)
{
    int ret = E_SUCCESS;
    GError *error = NULL;
    GVariant *argument = NULL;
    pa_pal_loopback_session_data_t *ses_data = NULL;
    GVariant *result = NULL;
    bool is_bt_src_usecase = ((usecase_type == PA_BT_A2DP_SOURCE) || \
            (usecase_type == PA_BT_HFP_AG)) ? true : false;

    g_debug("%s enter\n", __func__);

    if (!g_mod_data && get_mod_data(is_bt_src_usecase))
        return E_FAILURE;

    /* Send event for BT source usecase connection */
    if (usecase_type == PA_BT_A2DP_SOURCE) {
        ret = send_external_jack_connection_request(
                PA_PAL_A2DP_OUT_PORT_DBUS_OBJECT_PATH_PREFIX,
                is_bt_src_usecase, connect);
        if(ret) {
            g_printerr("Error connecting to BT port object %s\n",
                    PA_PAL_A2DP_OUT_PORT_DBUS_OBJECT_PATH_PREFIX);
            ret = E_FAILURE;
            goto exit;
        }
    }
    else if (usecase_type == PA_BT_HFP_AG) {
        ret = send_external_jack_connection_request(
                PA_PAL_SCO_IN_PORT_DBUS_OBJECT_PATH_PREFIX,
                is_bt_src_usecase, connect);
        if(ret) {
            g_printerr("Error connecting to BT port object %s\n",
                    PA_PAL_SCO_IN_PORT_DBUS_OBJECT_PATH_PREFIX);
            ret = E_FAILURE;
            goto exit;
        }
        ret = send_external_jack_connection_request(PA_PAL_SCO_OUT_PORT_DBUS_OBJECT_PATH_PREFIX, is_bt_src_usecase, connect);
        if(ret) {
            g_printerr("Error connecting to BT port object %s\n",
                    PA_PAL_SCO_OUT_PORT_DBUS_OBJECT_PATH_PREFIX);
            ret = E_FAILURE;
            goto exit;
        }
    }

    if (is_bt_src_usecase) {
        free_mod_data();
        goto exit;
    }

    /* Send event for BT sink usecase connection */
    if (usecase_type == PA_BT_A2DP_SINK) {
        ret = send_external_jack_connection_request(
                PA_PAL_A2DP_IN_PORT_DBUS_OBJECT_PATH_PREFIX,
                is_bt_src_usecase, connect);
        if(ret) {
            g_printerr("Error connecting to BT port object %s\n",
                    PA_PAL_A2DP_IN_PORT_DBUS_OBJECT_PATH_PREFIX);
            ret = E_FAILURE;
            goto exit;
        }
    }
    else {
        ret = send_external_jack_connection_request(
                PA_PAL_SCO_IN_PORT_DBUS_OBJECT_PATH_PREFIX,
                is_bt_src_usecase, connect);
        if(ret) {
            g_printerr("Error connecting to BT port object %s\n",
                    PA_PAL_SCO_IN_PORT_DBUS_OBJECT_PATH_PREFIX);
            ret = E_FAILURE;
            goto exit;
        }
        ret = send_external_jack_connection_request(PA_PAL_SCO_OUT_PORT_DBUS_OBJECT_PATH_PREFIX, is_bt_src_usecase, connect);
        if(ret) {
            g_printerr("Error connecting to BT port object %s\n",
                    PA_PAL_SCO_OUT_PORT_DBUS_OBJECT_PATH_PREFIX);
            ret = E_FAILURE;
            goto exit;
        }
    }

    /* Set connection params using loopback intf for BTsink usecase */
    g_snprintf(g_mod_data->obj_path, PA_PAL_DBUS_OBJECT_PATH_SIZE,
            "%s", PA_PAL_LOOPBACK_DBUS_OBJECT_PATH);

    g_debug("Obj path- %s\n", g_mod_data->obj_path);

    argument = g_variant_new("(@s)", g_variant_new_string(usecase_name[usecase_type]));
    /* Retrieve session handle from hash table */
    if ((ses_data = (pa_pal_loopback_session_data_t *)g_hash_table_lookup(g_mod_data->ses_hash_table,
                    (gconstpointer)(&usecase_name[usecase_type]))) == NULL) {
        g_printerr("No session exists for the usecase \n");
    }

    if (!connect) {
        g_hash_table_remove(g_mod_data->ses_hash_table, (gpointer)(&usecase_name[usecase_type]));

        if (ses_data && ses_data->obj_path)
            g_free(ses_data->obj_path);
        if (ses_data)
            g_free(ses_data);

        g_debug("Tearing down BT connection\n");
        result = g_dbus_connection_call_sync(g_mod_data->conn,
                NULL,
                g_mod_data->obj_path,
                PA_PAL_LOOPBACK_DBUS_MODULE_IFACE,
                "BtDisconnect",
                argument,
                NULL,
                G_DBUS_CALL_FLAGS_NONE,
                -1,
                NULL,
                &error);

        if (result == NULL) {
            g_printerr ("Error while disconnecting BT: %s\n", error->message);
            ret = E_FAILURE;
            goto exit;
        }

        /* Free mod data once all sessions are closed */
        if (g_mod_data && (g_hash_table_size(g_mod_data->ses_hash_table) == 0)) {
            printf("No active sessions running. Closing the connection with server !!!\n");
            free_mod_data();
        }
    }
    else {
        g_debug("Establishing BT connection\n");
        result = g_dbus_connection_call_sync(g_mod_data->conn,
                NULL,
                g_mod_data->obj_path,
                PA_PAL_LOOPBACK_DBUS_MODULE_IFACE,
                "BtConnect",
                argument,
                G_VARIANT_TYPE("(o)"),
                G_DBUS_CALL_FLAGS_NONE,
                -1,
                NULL,
                &error);

        if (result == NULL) {
            g_printerr ("Error in establishing BT connection: %s\n", error->message);
            ret = E_FAILURE;
            goto exit;
        }

        if (!ses_data) {
            ses_data = (pa_pal_loopback_session_data_t *)g_malloc0(sizeof(pa_pal_loopback_session_data_t));
            g_variant_get(result, "(o)", &ses_data->obj_path);
            g_printerr("The server answered: obj path: '%s'\n", ses_data->obj_path);
            g_hash_table_insert(g_mod_data->ses_hash_table,
                    (gpointer)(&usecase_name[usecase_type]), ses_data);
        }
        else {
            g_printerr("Session not created. Insufficient memory !!!\n");
            ret = -ENOMEM;
        }
    }

exit:
    if (error)
        g_error_free(error);
    if(result)
        g_variant_unref(result);
    g_debug("%s exit\n", __func__);

    return ret;
}

int pa_bt_set_param(pa_bt_usecase_type_t usecase_type, const char *kvpairs)
{
    int ret = 0, result = E_SUCCESS;
    GError *error = NULL;
    double vol = -1.0;
    int sr = 0;
    pa_pal_loopback_session_data_t *ses_data = NULL;
    audio_prm_kvpair_t *prm_kvpair = NULL;
    bool is_bt_src_usecase = ((usecase_type == PA_BT_A2DP_SOURCE) || \
            (usecase_type == PA_BT_HFP_AG)) ? true : false;

    g_debug("%s Enter\n", __func__);

    if ((usecase_type == PA_BT_A2DP_SOURCE || usecase_type == PA_BT_HFP_AG) && get_mod_data(is_bt_src_usecase))
        return E_FAILURE;

    if (g_mod_data == NULL)
        return -EINVAL;

    if (usecase_type == PA_BT_A2DP_SINK  || usecase_type == PA_BT_HFP_CLIENT) {
        ses_data = (pa_pal_loopback_session_data_t *)g_hash_table_lookup(g_mod_data->ses_hash_table,
                (gconstpointer)(&usecase_name[usecase_type]));
        if((ses_data == NULL)) {
            g_printerr("No session exists for given handle\n");
            ret = -EINVAL;
            return ret;
        }
    }

    prm_kvpair = (audio_prm_kvpair_t*)calloc(1, sizeof(audio_prm_kvpair_t));
    if (prm_kvpair == NULL) {
        g_printerr("Could not allocate memory for prm_kvpair\n");
        return -ENOMEM;
    }
    if (parse_kvpair(kvpairs, &prm_kvpair)) {
        g_printerr("%s command not supported!!\n", kvpairs);
        ret = -EINVAL;
        return ret;
    }
    switch (prm_kvpair->key) {
        case AUDIO_PARAMETER_KEY_BTSINK_ENABLE:
            ret = setup_loopback(ses_data, prm_kvpair->value);
            if (ret) {
                g_printerr("Loopback setup failed for cmd: %s!!\n", kvpairs);
                result = E_FAILURE;
            }
            break;
        case AUDIO_PARAMETER_KEY_HFP_ENABLE:
            ret = setup_loopback(ses_data, prm_kvpair->value);
            if (ret) {
                g_printerr("Loopback setup failed for cmd: %s!!\n", kvpairs);
                result = E_FAILURE;
            }
            break;
        case AUDIO_PARAMETER_KEY_BTSINK_SET_VOLUME:
            vol = atof(prm_kvpair->value);
            g_debug("%s: vol: %f\n", __func__, vol);
            ret = set_volume(ses_data, "bta2dp", vol);
            if (ret) {
                g_printerr("Set volume failed for cmd: %s!!\n", kvpairs);
                result = E_FAILURE;
            }
            break;
        case AUDIO_PARAMETER_KEY_BTSINK_SET_MUTE:
            ret = set_mute(ses_data, "bta2dp", prm_kvpair->value);
            if (ret) {
                g_printerr("Set mute failed for cmd: %s!!\n", kvpairs);
                result = E_FAILURE;
            }
            break;
        case AUDIO_PARAMETER_KEY_HFP_SET_SPK_MUTE:
            ret = set_mute(ses_data, "hfp_rx", prm_kvpair->value);
            if (ret) {
                g_printerr("Set mute failed for cmd: %s!!\n", kvpairs);
                result = E_FAILURE;
            }
            break;
        case AUDIO_PARAMETER_KEY_HFP_SET_MIC_MUTE:
            ret = set_mute(ses_data, "hfp_tx", prm_kvpair->value);
            if (ret) {
                g_printerr("Set mute failed for cmd: %s!!\n", kvpairs);
                result = E_FAILURE;
            }
            break;
        case AUDIO_PARAMETER_KEY_HFP_SET_SPK_VOLUME:
            vol = atof(prm_kvpair->value);
            g_debug("%s: vol: %f\n", __func__, vol);
            ret = set_volume(ses_data, "hfp_rx", vol);
            if (ret) {
                g_printerr("Set volume failed for cmd: %s!!\n", kvpairs);
                result = E_FAILURE;
            }
            break;
        case AUDIO_PARAMETER_KEY_HFP_SET_MIC_VOLUME:
            vol = atof(prm_kvpair->value);
            g_debug("%s: vol: %f\n", __func__, vol);
            ret = set_volume(ses_data, "hfp_tx", vol);
            if (ret) {
                g_printerr("Set volume failed for cmd: %s!!\n", kvpairs);
                result = E_FAILURE;
            }
            break;
        case AUDIO_PARAMETER_KEY_HFP_SET_SAMPLING_RATE:
            sr = atoi(prm_kvpair->value);
            g_debug("%s: sampling rate: %d\n", __func__, sr);
            if (usecase_type == PA_BT_HFP_AG) {
                subscribe_set_param_done_event(PA_PAL_SCO_IN_PORT_DBUS_OBJECT_PATH_PREFIX, true);
                ret = set_sample_rate_jack(PA_PAL_SCO_IN_PORT_DBUS_OBJECT_PATH_PREFIX, prm_kvpair->value);
                subscribe_set_param_done_event(PA_PAL_SCO_IN_PORT_DBUS_OBJECT_PATH_PREFIX, false);
            }
            else
                ret = set_sample_rate_loopback(ses_data, sr);
            if (ret) {
                g_printerr("Set sampling rate failed for cmd: %s!!\n", kvpairs);
                result = E_FAILURE;
            }
            break;
        case AUDIO_PARAMETER_KEY_BTSRC_A2DP_SUSPEND:
            g_debug("%s: a2dp_suspend: %s\n", __func__, prm_kvpair->value);
            subscribe_set_param_done_event(PA_PAL_A2DP_OUT_PORT_DBUS_OBJECT_PATH_PREFIX, true);
            ret = jack_bt_set_a2dp_stream_suspend(PA_PAL_A2DP_OUT_PORT_DBUS_OBJECT_PATH_PREFIX,
                    prm_kvpair->value);
            if (ret) {
                g_printerr("Set a2dp suspend failed for cmd: %s!!\n", kvpairs);
                result = E_FAILURE;
            }
            subscribe_set_param_done_event(PA_PAL_A2DP_OUT_PORT_DBUS_OBJECT_PATH_PREFIX, false);
            break;
        default:
            g_printerr("Invalid key %d\n", prm_kvpair->key);
            break;
    }

    if (prm_kvpair->value)
        free(prm_kvpair->value);
    free(prm_kvpair);

    if (usecase_type == PA_BT_A2DP_SOURCE || usecase_type == PA_BT_HFP_AG)
        free_mod_data();

    g_debug("%s Exit\n", __func__);
    return result;
}

int pa_bt_get_param(pa_bt_usecase_type_t usecase_type, const char *query, void *reply)
{
    int ret = 0, result = E_SUCCESS;
    char value[32]={0};
    int sr = 0;
    double vol = -1.0;
    int key_idx = 0;
    pa_pal_loopback_session_data_t *ses_data = NULL;

    g_debug("%s Enter\n", __func__);

    if (query == NULL || reply == NULL) {
        g_printerr("Invalid arguments\n");
        return -EINVAL;
    }

    if (g_mod_data &&
            (ses_data = (pa_pal_loopback_session_data_t *)g_hash_table_lookup(g_mod_data->ses_hash_table,
                    (gconstpointer)(&usecase_name[usecase_type]))) == NULL) {
        g_printerr("No session exists for given handle\n");
        return -EINVAL;
    }

    key_idx = parse_keyidx(query);
    if (key_idx == -1) {
        g_printerr("%s command not supported!!\n", query);
        ret = -EINVAL;
        return ret;
    }
    switch (key_idx) {
        case AUDIO_PARAMETER_KEY_HFP_GET_SPK_VOLUME:
            ret = get_volume(ses_data, "hfp_rx", &vol);
            if (ret) {
                g_printerr("Get volume failed for cmd: %s!!\n", query);
                result = E_FAILURE;
            }
            g_debug("%s: vol: %f\n", __func__, vol);
            memcpy(reply, &vol, sizeof(vol));
            break;
        case AUDIO_PARAMETER_KEY_HFP_GET_MIC_VOLUME:
            ret = get_volume(ses_data, "hfp_tx", &vol);
            if (ret) {
                g_printerr("Get volume failed for cmd: %s!!\n", query);
                result = E_FAILURE;
            }
            g_debug("%s: vol: %f\n", __func__, vol);
            memcpy(reply, &vol, sizeof(vol));
            break;
        case AUDIO_PARAMETER_KEY_HFP_GET_SAMPLING_RATE:
            ret = get_sample_rate(ses_data, &sr);
            if (ret) {
                g_printerr("Get sampling rate failed for cmd: %s!!\n", query);
                result = E_FAILURE;
            }
            g_debug("%s: sampling rate: %u\n", __func__, sr);
            memcpy(reply, &sr, sizeof(sr));
            break;
        case AUDIO_PARAMETER_KEY_BTSINK_GET_VOLUME:
            ret = get_volume(ses_data, "bta2dp", &vol);
            if (ret) {
                g_printerr("Get volume failed failed for cmd: %s!!\n", query);
                result = E_FAILURE;
            }
            g_debug("%s: vol: %f\n", __func__, vol);
            memcpy(reply, &vol, sizeof(vol));
            break;
        default:
            g_printerr("Invalid key %d\n", key_idx);
            break;
    }

    g_debug("%s Exit\n", __func__);
    return result;
}
