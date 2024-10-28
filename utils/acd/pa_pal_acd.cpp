/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <gio/gio.h>
#include <glib/gprintf.h>

#include "pa_pal_acd.h"

#define PA_QST_DBUS_OBJECT_PATH_PREFIX "/org/pulseaudio/ext/qsthw"
#define PA_QST_DBUS_MODULE_IFACE "org.PulseAudio.Ext.Qsthw"
#define PA_QST_DBUS_SESSION_IFACE "org.PulseAudio.Ext.Qsthw.Session"
#define PA_QST_DBUS_MODULE_OBJ_PATH_SIZE 256
#define PA_QST_DBUS_MODULE_IFACE_VERSION_DEFAULT 0x100
#define PA_QST_DBUS_ASYNC_CALL_TIMEOUT_MS 1000

#define PA_QST_DBUS_MODULE_IFACE_VERSION_101 0x101

#ifndef memscpy
#define memscpy(dst, dst_size, src, bytes_to_copy) \
        (void) memcpy(dst, src, MIN(dst_size, bytes_to_copy))
#endif

struct pa_qst_module_data {
    GDBusConnection *conn;
    char g_obj_path[PA_QST_DBUS_MODULE_OBJ_PATH_SIZE];
    GHashTable *ses_hash_table;
    guint interface_version;
};

struct pa_qst_session_data {
    char *obj_path;
    GThread *thread_loop;
    GMainLoop *loop;
    guint sub_id_det_event;

    pa_qst_recognition_callback_t callback;
    void *cookie;

    GMutex mutex;
    GCond cond;
};

static pa_qst_ses_handle_t parse_ses_handle(char *obj_path) {
    char **handle_string;
    pa_qst_ses_handle_t handle;

    handle_string = g_strsplit(obj_path, "_", -1);
    handle = g_ascii_strtoll(handle_string[1], NULL, 0);
    g_printf("session handle %d\n",handle);
    g_strfreev(handle_string);

    return handle;
}

static void on_det_event_callback(GDBusConnection *conn,
                                  const gchar *sender_name,
                                  const gchar *object_path,
                                  const gchar *interface_name,
                                  const gchar *signal_name,
                                  GVariant *parameters,
                                  gpointer data) {
    struct pa_qst_session_data *ses_data = (struct pa_qst_session_data *)data;
    GError *error = NULL;
    GVariant *struct_v = NULL, *struct_v2 = NULL, *array_v = NULL, *array_v2 = NULL;
    GVariantIter arg_i, struct_i, struct_ii, array_i, array_ii;
    struct pa_pal_phrase_recognition_event *pa_qst_event;
    struct pal_st_phrase_recognition_event phrase_event = {0, };
    gint i = 0, j = 0;
    gsize n_elements = 0;
    gsize element_size = sizeof(guchar);
    gconstpointer value;
    guint64 timestamp;

    if (!parameters) {
        g_printf("Invalid params received\n");
        return;
    }
    g_printf("signal handler: Ondetection event signal received\n");
    uint32_t frame_count = 0;
    uint32_t *sess_id = (uint32_t *)ses_data->cookie;

    /* TODO: parse the complete message and fill phrase_event struct */
    g_variant_iter_init(&arg_i, parameters);
    struct_v = g_variant_iter_next_value(&arg_i);
    if(!struct_v) {
        g_printf("Invalid struct_v pointer\n");
        return;
    }
    g_variant_iter_init(&struct_i, struct_v);
    g_variant_iter_next(&struct_i, "i", &phrase_event.common.status);
    g_variant_iter_next(&struct_i, "i", &phrase_event.common.type);
    g_variant_iter_next(&struct_i, "i", sess_id);

    g_variant_iter_next(&struct_i, "b", &phrase_event.common.capture_available);
    g_variant_iter_next(&struct_i, "i", &phrase_event.common.capture_session);
    g_variant_iter_next(&struct_i, "i", &phrase_event.common.capture_delay_ms);
    g_variant_iter_next(&struct_i, "i", &phrase_event.common.capture_preamble_ms);
    g_variant_iter_next(&struct_i, "b", &phrase_event.common.trigger_in_data);
    struct_v2 = g_variant_iter_next_value(&struct_i);
    g_variant_iter_init(&struct_ii, struct_v2);
    g_variant_iter_next(&struct_ii, "u", &phrase_event.common.media_config.sample_rate);
    g_variant_iter_next(&struct_ii, "u", &phrase_event.common.media_config.ch_info.channels);
    g_variant_iter_next(&struct_ii, "u", &phrase_event.common.media_config.aud_fmt_id);
    g_variant_iter_next(&struct_ii, "u", &phrase_event.num_phrases);

    array_v = g_variant_iter_next_value(&arg_i);
    g_variant_iter_init(&array_i, array_v);
    i = 0;
    while ((struct_v = g_variant_iter_next_value(&array_i)) &&
           (i < phrase_event.num_phrases)) {
        g_variant_iter_init(&struct_i, struct_v);
        g_variant_iter_next(&struct_i, "u", &phrase_event.phrase_extras[i].id);
        g_variant_iter_next(&struct_i, "u", &phrase_event.phrase_extras[i].recognition_modes);
        g_variant_iter_next(&struct_i, "u", &phrase_event.phrase_extras[i].confidence_level);

        array_v2 = g_variant_iter_next_value(&struct_i);
        g_variant_iter_init(&array_ii, array_v2);
        j = 0;
        int usernum = 1;
        while ((struct_v2 = g_variant_iter_next_value(&array_ii)) &&
              (j < usernum)) {
            g_variant_iter_init(&struct_ii, struct_v2);
            g_variant_iter_next(&struct_ii, "u", &phrase_event.phrase_extras[i].levels[j].user_id);
            g_variant_iter_next(&struct_ii, "u", &phrase_event.phrase_extras[i].levels[j].level);
            phrase_event.phrase_extras[i].num_levels++;
            j++;
        }
        phrase_event.num_phrases++;
        i++;
        g_variant_unref(struct_v);
    }
    g_variant_iter_next(&arg_i, "t", &timestamp);

    phrase_event.common.data_offset = sizeof(struct pa_pal_phrase_recognition_event);
    array_v = g_variant_iter_next_value(&arg_i);
    value = g_variant_get_fixed_array(array_v, &n_elements, element_size);
    phrase_event.common.data_size = n_elements;

    pa_qst_event = (struct pa_pal_phrase_recognition_event* ) g_malloc0(sizeof(struct pa_pal_phrase_recognition_event) +
                   phrase_event.common.data_size);

    memcpy(&pa_qst_event->phrase_event, &phrase_event, sizeof(phrase_event));

    pa_qst_event->timestamp = timestamp;

    memscpy((char*)pa_qst_event + pa_qst_event->phrase_event.common.data_offset,
            pa_qst_event->phrase_event.common.data_size,
            value, n_elements);
    ses_data->callback(&pa_qst_event->phrase_event.common, ses_data->cookie);
    g_free(pa_qst_event);
}

static int subscribe_detection_event(struct pa_qst_module_data *m_data,
                                     struct pa_qst_session_data *ses_data,
                                     bool subscribe) {
    GVariant *result;
    GVariant *argument_sig_listener = NULL;
    GError *error = NULL;
    guint id;
    char signal_name[128];
    gint ret = 0;

    g_snprintf(signal_name, sizeof(signal_name),
               "%s.%s", PA_QST_DBUS_SESSION_IFACE, "DetectionEvent");
    if (subscribe) {
       /* Add listener for signal to PulseAudio core.
        * this is done during load of first session i.e. empty hash table
        * Empty obj path array is sent to listen for signals from all objects on
        * this connection
        */
        if (g_hash_table_size(m_data->ses_hash_table) == 0) {
            const gchar *obj_str[] = {};
            argument_sig_listener = g_variant_new("(@s@ao)",
                            g_variant_new_string(signal_name),
                            g_variant_new_objv(obj_str, 0));

            result = g_dbus_connection_call_sync(m_data->conn,
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
        }

        /* subscribe for detection event signal */
        ses_data->sub_id_det_event = g_dbus_connection_signal_subscribe(m_data->conn,
                           NULL,
                           PA_QST_DBUS_SESSION_IFACE,
                           "DetectionEvent",
                           ses_data->obj_path,
                           NULL,
                           G_DBUS_SIGNAL_FLAGS_NONE,
                           on_det_event_callback,
                           ses_data,
                           NULL);
    } else {
       /* Remove signal listener to PulseAudio core.
        * this is done during unload of last session i.e. hash table size == 1.
        */
        if (g_hash_table_size(m_data->ses_hash_table) == 1) {
            argument_sig_listener = g_variant_new("(@s)",
                            g_variant_new_string(signal_name));
            result = g_dbus_connection_call_sync(m_data->conn,
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
        }

        if (ses_data->sub_id_det_event)
            g_dbus_connection_signal_unsubscribe(m_data->conn, ses_data->sub_id_det_event);
    }

exit:
    return ret;
}

static void *signal_threadloop(void *cookie) {
    struct pa_qst_session_data *ses_data = (struct pa_qst_session_data *)cookie;

    g_printf("Enter %s %p\n", __func__, ses_data);
    if (!ses_data) {
        g_printerr("Invalid thread params");
        goto exit;
    }

    ses_data->loop = g_main_loop_new(NULL, FALSE);
    g_printf("initiate main loop run for detections %d\n", ses_data->sub_id_det_event);
    g_main_loop_run(ses_data->loop);

    g_printf("out of main loop\n");
    g_main_loop_unref(ses_data->loop);

exit:
    return NULL;
}

static gint unload_sm(struct pa_qst_module_data *m_data,
                      struct pa_qst_session_data *ses_data) {
    GVariant *result;
    GError *error = NULL;
    gint ret = 0;

    if (subscribe_detection_event(m_data, ses_data, false /*unsubscribe */)) {
        g_printerr("Failed to unsubscribe for detection event");
    }

    g_cond_clear(&ses_data->cond);
    g_mutex_clear(&ses_data->mutex);

    /* Quit mainloop started to listen for detection signals */
    if (ses_data->thread_loop) {
        g_main_loop_quit(ses_data->loop);
        g_thread_join(ses_data->thread_loop);
        ses_data->thread_loop = NULL;
    }

    result = g_dbus_connection_call_sync(m_data->conn,
                            NULL,
                            ses_data->obj_path,
                            PA_QST_DBUS_SESSION_IFACE,
                            "UnloadSoundModel",
                            NULL,
                            NULL,
                            G_DBUS_CALL_FLAGS_NONE,
                            -1,
                            NULL,
                            &error);
    if (result == NULL) {
        g_printerr ("Error invoking UnloadSoundmodel(): %s\n", error->message);
        g_error_free(error);
        ret = -EINVAL;
    } else {
        g_variant_unref(result);
    }

    return ret;
}

gint pa_qst_load_sound_model(const pa_qst_handle_t *mod_handle,
                             pal_param_payload *prm_payload,
                             void *cookie,
                             pa_qst_ses_handle_t *handle,
                             pal_stream_attributes *stream_attr,
                             pal_device *pal_dev) {
    GVariant *result;
    GVariant *value_0, *value_1, *value_2, *value_3, *value_4, *value_arr;
    GVariant *argument_1, *argument_2, *argument_load;
    GError *error = NULL;
    GVariantBuilder builder_1;
    gint i = 0, j = 0;
    struct pal_st_sound_model *sound_model = NULL;
    struct pa_qst_session_data *ses_data = NULL;
    struct pa_qst_module_data *m_data = NULL;
    pa_qst_ses_handle_t ses_handle;
    gchar thread_name[16];
    char arr1[] = "acd";
    char arr2[] = "detection";
    if (!mod_handle || !prm_payload || !handle || (prm_payload->payload_size != sizeof(pal_st_sound_model))) {
        g_printerr("Invalid input params\n");
        return -EINVAL;
    }

    sound_model = (pal_st_sound_model*)prm_payload->payload;
    m_data = (struct pa_qst_module_data *)mod_handle;
    ses_data = (pa_qst_session_data *)g_malloc0(sizeof(struct pa_qst_session_data));

    g_variant_builder_init(&builder_1, G_VARIANT_TYPE("(uuuu)"));
    g_variant_builder_add(&builder_1, "u", (guint32)stream_attr->in_media_config.sample_rate);
    g_variant_builder_add(&builder_1, "u", (guint32)stream_attr->in_media_config.ch_info.channels);
    g_variant_builder_add(&builder_1, "u", (guint32)pal_dev->config.sample_rate);
    g_variant_builder_add(&builder_1, "u", (guint32)pal_dev->config.ch_info.channels);
    value_0 = g_variant_builder_end(&builder_1);

    /* build loadsoundmodel message */
    g_variant_builder_init(&builder_1, G_VARIANT_TYPE("(uqqqay)"));
    g_variant_builder_add(&builder_1, "u", (guint32)sound_model->uuid.timeLow);
    g_variant_builder_add(&builder_1, "q", (guint16)sound_model->uuid.timeMid);
    g_variant_builder_add(&builder_1, "q", (guint16)sound_model->uuid.timeHiAndVersion);
    g_variant_builder_add(&builder_1, "q", (guint16)sound_model->uuid.clockSeq);
    value_arr = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
        (gconstpointer)&sound_model->uuid.node[0], 6, sizeof(guchar));
    g_variant_builder_add_value(&builder_1, value_arr);
    value_1 = g_variant_builder_end(&builder_1);

    g_variant_builder_init(&builder_1, G_VARIANT_TYPE("(uqqqay)"));
    g_variant_builder_add(&builder_1, "u", (guint32)sound_model->vendor_uuid.timeLow);
    g_variant_builder_add(&builder_1, "q", (guint16)sound_model->vendor_uuid.timeMid);
    g_variant_builder_add(&builder_1, "q", (guint16)sound_model->vendor_uuid.timeHiAndVersion);
    g_variant_builder_add(&builder_1, "q", (guint16)sound_model->vendor_uuid.clockSeq);
    value_arr = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
        (gconstpointer)&sound_model->vendor_uuid.node[0], 6, sizeof(guchar));
    g_variant_builder_add_value(&builder_1, value_arr);
    value_2 = g_variant_builder_end(&builder_1);

    g_variant_builder_init(&builder_1, G_VARIANT_TYPE("a(uuauss)"));
    g_variant_builder_open(&builder_1, G_VARIANT_TYPE("(uuauss)"));
    g_variant_builder_add(&builder_1, "u", 0);
    g_variant_builder_add(&builder_1, "u", 0);
    g_variant_builder_open(&builder_1, G_VARIANT_TYPE("au"));
    g_variant_builder_add(&builder_1, "u", 0);
    g_variant_builder_close(&builder_1);
    g_variant_builder_add(&builder_1, "s",arr1 );
    g_variant_builder_add(&builder_1, "s", arr2);
    g_variant_builder_close(&builder_1);
    value_3 = g_variant_builder_end(&builder_1);
    value_4 = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
        (gconstpointer)((gchar *)sound_model + sound_model->data_offset),
        sound_model->data_size, sizeof(guchar));

    argument_1 = g_variant_new("(@i@(uuuu)@(uqqqay)@(uqqqay))",
                               g_variant_new_int32(sound_model->type),
                               value_0,
                               value_1,
                               value_2);

    argument_2 = g_variant_new("(@(i(uuuu)(uqqqay)(uqqqay))@a(uuauss))",
                               argument_1,
                               value_3);

    argument_load = g_variant_new("(@((i(uuuu)(uqqqay)(uqqqay))a(uuauss))@ay)",
                               argument_2,
                               value_4);

    /*
     * Use global obj and intf path to call LoadSoundModel which
     * will return per session obj path.
     */
    result = g_dbus_connection_call_sync(m_data->conn,
                            NULL, /* bus_name */
                            m_data->g_obj_path,
                            PA_QST_DBUS_MODULE_IFACE,
                            "LoadSoundModel",
                            argument_load,
                            G_VARIANT_TYPE("(o)"),
                            G_DBUS_CALL_FLAGS_NONE,
                            -1,
                            NULL,
                            &error);
    if (result == NULL) {
        g_printerr ("Error invoking LoadSoundModel(): %s\n", error->message);
        g_error_free(error);
        goto exit;
    }

    g_variant_get(result, "(o)", &ses_data->obj_path);
    g_printf("The server answered: obj path: '%s'\n", ses_data->obj_path);
    g_variant_unref(result);

    /* parse sm handle from object path */
    ses_handle = parse_ses_handle(ses_data->obj_path);

    /* Start threadloop to listen to signals from server */
    snprintf(thread_name, sizeof(thread_name), "pa_loop_%d", ses_handle);
    g_printf("create thread %s\n", thread_name);
    ses_data->thread_loop = g_thread_try_new(thread_name, signal_threadloop,
                              ses_data, &error);
    if (!ses_data->thread_loop) {
        g_printf("Could not create thread %s, error %s\n", thread_name, error->message);
        g_error_free(error);
        goto exit_1;
    }

    if (subscribe_detection_event(m_data, ses_data, true /* subscribe */)) {
        g_printerr("Failed to subscribe for detection event");
        goto exit_1;
    }

    g_mutex_init(&ses_data->mutex);
    g_cond_init(&ses_data->cond);
    /* add session to module hash table */
    g_hash_table_insert(m_data->ses_hash_table, GINT_TO_POINTER(ses_handle), ses_data);
    *handle = ses_handle;
    return 0;

exit_1:
    /* unload sound model internally */
    unload_sm(m_data, ses_data);

exit:
    if (ses_data)
        g_free(ses_data);
    *handle = -1;
    return -EINVAL;
}

gint pa_qst_unload_sound_model(const pa_qst_handle_t *mod_handle,
                               pa_qst_ses_handle_t handle) {
    struct pa_qst_module_data *m_data = (struct pa_qst_module_data *)mod_handle;
    struct pa_qst_session_data *ses_data;
    gint ret = 0;

    if (!m_data) {
        g_printf("Invalid input params\n");
        ret = -EINVAL;
        goto exit;
    }

    if ((ses_data = (struct pa_qst_session_data *)g_hash_table_lookup(m_data->ses_hash_table,
                        GINT_TO_POINTER(handle))) == NULL) {
        g_printerr("No session exists for given handle %d\n", handle);
        ret = -EINVAL;
        goto exit;
    }

    ret = unload_sm(m_data, ses_data);
    g_hash_table_remove(m_data->ses_hash_table, GINT_TO_POINTER(handle));
    if (ses_data->obj_path)
        g_free(ses_data->obj_path);

    g_free(ses_data);

exit:
    return ret;
}

gint pa_qst_start_recognition_v2(const pa_qst_handle_t *mod_handle,
                              pa_qst_ses_handle_t handle,
                              const struct pal_st_recognition_config *rc_config,
                              pa_qst_recognition_callback_t callback,
                              void *cookie) {
    GVariant *value_1, *value_2;
    GVariantBuilder builder_1;
    GVariant *argument, *argument_start;
    GVariant *result;
    struct pa_qst_module_data *m_data = (struct pa_qst_module_data *)mod_handle;
    gint ret = 0, i = 0, j = 0;
    GError *error = NULL;
    struct pa_qst_session_data *ses_data;

    if (!m_data) {
        g_printerr("Invalid input params\n");
        ret = -EINVAL;
        goto exit;
    }

    if ((ses_data = (struct pa_qst_session_data *)g_hash_table_lookup(m_data->ses_hash_table,
                        GINT_TO_POINTER(handle))) == NULL) {
        g_printerr("No session exists for given handle %d\n", handle);
        ret = -EINVAL;
        goto exit;
    }

    g_variant_builder_init(&builder_1, G_VARIANT_TYPE("a(uuu)"));
    for (i = 0; i < rc_config->num_phrases; i++) {
        g_variant_builder_open(&builder_1, G_VARIANT_TYPE("(uuu)"));
        g_variant_builder_add(&builder_1, "u", (guint32)rc_config->phrases[i].id);
        g_variant_builder_add(&builder_1, "u", (guint32)rc_config->phrases[i].recognition_modes);
        g_variant_builder_add(&builder_1, "u", (guint32)rc_config->phrases[i].confidence_level);
        g_variant_builder_close(&builder_1);
    }
    value_1 = g_variant_builder_end(&builder_1);

    value_2 = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
        (gconstpointer)((gchar *)rc_config + rc_config->data_offset),
        rc_config->data_size, sizeof(guchar));

    argument = g_variant_new("(@i@a(uuu))",
                   g_variant_new_int32(rc_config->num_phrases),
                   value_1);
    argument_start = g_variant_new("(@(ia(uuu))@ay)",
                   argument,
                   value_2);

    result = g_dbus_connection_call_sync(m_data->conn,
                            NULL,
                            ses_data->obj_path,
                            PA_QST_DBUS_SESSION_IFACE,
                            "StartRecognition_v2",
                            argument_start,
                            NULL,
                            G_DBUS_CALL_FLAGS_NONE,
                            -1,
                            NULL,
                            &error);
    if (result == NULL) {
        g_printerr ("Error invoking StartRecognition(): %s\n", error->message);
        g_error_free(error);
        ret = -EINVAL;
        goto exit;
    }

    g_variant_unref(result);
    ses_data->callback = callback;
    ses_data->cookie = cookie;

exit:
    return ret;
}

gint pa_qst_stop_recognition(const pa_qst_handle_t *mod_handle,
                             pa_qst_ses_handle_t handle) {
    GVariant *result;
    struct pa_qst_module_data *m_data = (struct pa_qst_module_data *)mod_handle;
    GError *error = NULL;
    struct pa_qst_session_data *ses_data;
    gint ret = 0;

    if (!m_data) {
        g_printf("Invalid input params\n");
        ret = -EINVAL;
        goto exit;
    }

    if ((ses_data = (struct pa_qst_session_data *)g_hash_table_lookup(m_data->ses_hash_table,
                        GINT_TO_POINTER(handle))) == NULL) {
        g_printerr("No session exists for given handle %d\n", handle);
        ret = -EINVAL;
        goto exit;
    }

    result = g_dbus_connection_call_sync(m_data->conn,
                            NULL,
                            ses_data->obj_path,
                            PA_QST_DBUS_SESSION_IFACE,
                            "StopRecognition",
                            NULL,
                            NULL,
                            G_DBUS_CALL_FLAGS_NONE,
                            -1,
                            NULL,
                            &error);
    if (result == NULL) {
        g_printerr ("Error invoking StopRecognition(): %s\n", error->message);
        g_error_free(error);
        ret = -EINVAL;
        goto exit;
    }

    g_variant_unref(result);

exit:
    return ret;
}

int pa_qst_set_parameters(const pa_qst_handle_t *mod_handle,
                          pa_qst_ses_handle_t handle,
                          const char *kv_pairs) {
    GVariant *result;
    struct pa_qst_module_data *m_data = (struct pa_qst_module_data *)mod_handle;
    GError *error = NULL;
    struct pa_qst_session_data *ses_data;
    GVariant *argument = NULL;
    gint ret = 0;

    if (!m_data || !kv_pairs) {
        g_printf("Invalid input params\n");
        ret = -EINVAL;
        goto exit;
    }

    argument = g_variant_new("(@s)",
                    g_variant_new_string(kv_pairs));

    if (handle == 0) {
        /* handle global set param here */
        result = g_dbus_connection_call_sync(m_data->conn,
                                NULL,
                                m_data->g_obj_path,
                                PA_QST_DBUS_MODULE_IFACE,
                                "SetParameters",
                                argument,
                                NULL,
                                G_DBUS_CALL_FLAGS_NONE,
                                -1,
                                NULL,
                                &error);
    } else {
        /* handle per session set param here */
        if ((ses_data =
                  (struct pa_qst_session_data *)g_hash_table_lookup(m_data->ses_hash_table,
                            GINT_TO_POINTER(handle))) == NULL) {
            g_printerr("No session exists for given handle %d\n", handle);
            ret = -EINVAL;
            goto exit;
        }
        result = g_dbus_connection_call_sync(m_data->conn,
                                NULL,
                                ses_data->obj_path,
                                PA_QST_DBUS_SESSION_IFACE,
                                "SetParameters",
                                argument,
                                NULL,
                                G_DBUS_CALL_FLAGS_NONE,
                                -1,
                                NULL,
                                &error);
    }

    if (result == NULL) {
        g_printerr ("Error invoking SetParameters(): %s\n", error->message);
        g_error_free(error);
        ret = -EINVAL;
        goto exit;
    }

    g_variant_unref(result);

exit:
    return ret;
}

int pa_qst_get_param_data(const pa_qst_handle_t *mod_handle,
                          pa_qst_ses_handle_t handle,
                          const char *param,
                          void *payload,
                          size_t payload_size,
                          size_t *param_data_size) {
    GVariant *result, *array_v;
    struct pa_qst_module_data *m_data = (struct pa_qst_module_data *)mod_handle;
    GError *error = NULL;
    struct pa_qst_session_data *ses_data;
    size_t buf_size = 0;
    gsize n_elements;
    gsize element_size = sizeof(guchar);
    gconstpointer value;
    GVariant *argument = NULL;
    gint ret = 0;

    if (!m_data || !payload || !param_data_size) {
        g_printerr("Invalid input params\n");
        ret = -EINVAL;
        goto exit;
    }

    /* Initialize payload to 0 and return null payload in case of error */
    memset(payload, 0, payload_size);

    if ((ses_data = (struct pa_qst_session_data *)g_hash_table_lookup(m_data->ses_hash_table,
                        GINT_TO_POINTER(handle))) == NULL) {
        g_printerr("No session exists for given handle %d\n", handle);
        ret = -EINVAL;
        goto exit;
    }

    argument = g_variant_new("(@s)",
                    g_variant_new_string(param));
    result = g_dbus_connection_call_sync(m_data->conn,
                            NULL,
                            ses_data->obj_path,
                            PA_QST_DBUS_SESSION_IFACE,
                            "GetParamData",
                            argument,
                            G_VARIANT_TYPE("(ay)"),
                            G_DBUS_CALL_FLAGS_NONE,
                            -1,
                            NULL,
                            &error);
    if (result == NULL) {
        g_printerr("Error invoking GetParamData(): %s\n", error->message);
        g_error_free(error);
        goto exit;
    }

    array_v = g_variant_get_child_value(result, 0);
    value = g_variant_get_fixed_array(array_v, &n_elements, element_size);
    if(!value) {
        g_printerr("Invalid payload received\n");
        goto exit;
    }
    if (n_elements <= payload_size) {
        memcpy(payload, value, n_elements);
        *param_data_size = n_elements;
    } else {
        g_printerr("Insufficient payload size to copy payload data\n");
        ret = -ENOMEM;
    }

    g_variant_unref(array_v);
    g_variant_unref(result);

exit:
    return ret;
}

int pa_qst_get_version(const pa_qst_handle_t *mod_handle) {
    gint version;
    GVariant *result;
    struct pa_qst_module_data *m_data = (struct pa_qst_module_data *)mod_handle;
    GError *error = NULL;

    if (!m_data) {
        g_printf("Invalid input params\n");
        version = -EINVAL;
        goto exit;
    }

    result = g_dbus_connection_call_sync(m_data->conn,
                            NULL,
                            m_data->g_obj_path,
                            PA_QST_DBUS_MODULE_IFACE,
                            "GetVersion",
                            NULL,
                            G_VARIANT_TYPE("(i)"),
                            G_DBUS_CALL_FLAGS_NONE,
                            -1,
                            NULL,
                            &error);
    if (result == NULL) {
        g_printerr ("Error invoking GetVersion(): %s\n", error->message);
        g_error_free(error);
        version = -EINVAL;
        goto exit;
    }

    g_variant_get(result, "(i)", &version);
    g_printf("The server answered: version: '%d'\n", version);
    g_variant_unref(result);

exit:
    return version;
}

void pa_qst_update_interface_version(struct pa_qst_module_data *m_data) {
    GVariant *result;
    GError *error = NULL;

    result = g_dbus_connection_call_sync(m_data->conn,
                            NULL,
                            m_data->g_obj_path,
                            PA_QST_DBUS_MODULE_IFACE,
                            "GetInterfaceVersion",
                            NULL,
                            G_VARIANT_TYPE("(i)"),
                            G_DBUS_CALL_FLAGS_NONE,
                            -1,
                            NULL,
                            &error);
    if (result == NULL) {
        g_printerr ("Error invoking GetInterfaceVersion(): %s\n", error->message);
        g_error_free(error);
        return;
    }

    g_variant_get(result, "(i)", &m_data->interface_version);
    g_printf("The server answered: interface version: '%d'\n", m_data->interface_version);
    g_variant_unref(result);
    g_printf("returning %s'\n", __func__);
}

pa_qst_handle_t *pa_qst_init(const char *module_name) {
    struct pa_qst_module_data *m_data = NULL;
    const gchar *s_address = NULL;
    GError *error = NULL;
    char module_string[128];

    if (!g_strcmp0(module_name, PA_QST_MODULE_ID_PRIMARY)) {
        g_strlcpy(module_string, "primary", sizeof(module_string));
    } else {
        g_printerr("Unsupported module %s", module_name);
        goto exit;
    }

    m_data = (pa_qst_module_data*)g_malloc0(sizeof(struct pa_qst_module_data));
    if(!m_data) {
        g_printerr("Error allocating the memory\n");
        goto exit;
    }
    s_address = getenv("PULSE_DBUS_SERVER");
    if (!s_address) {
        g_printf("Pulse DBus server address not set, use default address\n");
        m_data->conn = g_dbus_connection_new_for_address_sync("unix:path=/var/run/pulse/dbus-socket",
                  G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT, NULL, NULL, &error);
    } else {
        g_printf("server address %s\n", s_address);
        m_data->conn = g_dbus_connection_new_for_address_sync(s_address,
                  G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT, NULL, NULL, &error);
    }

    if (m_data->conn == NULL) {
        g_printerr("Error connecting to D-Bus address %s: %s\n", s_address, error->message);
        g_error_free(error);
        goto exit;
    }

    g_snprintf(m_data->g_obj_path, PA_QST_DBUS_MODULE_OBJ_PATH_SIZE,
               "%s/%s", PA_QST_DBUS_OBJECT_PATH_PREFIX, module_string);

    /* hash table to retrieve session information */
    m_data->ses_hash_table = g_hash_table_new(g_direct_hash, g_direct_equal);

    /* Initialize module interface version */
    m_data->interface_version = PA_QST_DBUS_MODULE_IFACE_VERSION_DEFAULT;
    pa_qst_update_interface_version(m_data);
    g_printf("returning %s\n", __func__);
    return (pa_qst_handle_t *)m_data;

exit:
    if (m_data)
        g_free(m_data);
    return NULL;
}

int pa_qst_deinit(const pa_qst_handle_t *mod_handle) {
    struct pa_qst_module_data *m_data = (struct pa_qst_module_data *)mod_handle;
    GError *error = NULL;

    if (m_data) {
        if (m_data->ses_hash_table) {
            g_hash_table_destroy(m_data->ses_hash_table);
        }
        if (m_data->conn) {
            if (!g_dbus_connection_close_sync(m_data->conn, NULL, &error)) {
                g_printerr("Error in connection close(): %s\n", error->message);
                g_error_free(error);
            }
            g_object_unref(m_data->conn);
        }
        g_free(m_data);
        return 0;
    } else {
        g_printerr("Invalid module handle\n");
        return -EINVAL;
    }
}
