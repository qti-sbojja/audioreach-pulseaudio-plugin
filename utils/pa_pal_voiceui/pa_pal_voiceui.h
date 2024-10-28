/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef PA_QTI_SOUNDTRIGGER_H
#define PA_QTI_SOUNDTRIGGER_H

#include <memory>
#include "PalDefs.h"
#include "SoundTriggerUtils.h"

/*
 * Below are the definitions that are used by PA QTI ST client and
 * client wrapper.
 */
#define PA_QST_MODULE_ID_PRIMARY "soundtrigger.primary"

struct pa_pal_phrase_recognition_event {
    struct pal_st_phrase_recognition_event phrase_event; /* key phrase recognition event */
    uint64_t timestamp; /* event time stamp */
};

typedef void pa_qst_handle_t;
typedef int pa_qst_ses_handle_t;

typedef void (*pa_qst_recognition_callback_t)(struct pal_st_recognition_event *event,
                                              void *cookie);
/*
 * Below are the API definitions that are used by PA QTI ST client
 */
int pa_qst_load_sound_model(const pa_qst_handle_t *mod_handle,
                            pal_param_payload *prm_payload,
                            void *cookie,
                            pa_qst_ses_handle_t *handle,
                            pal_stream_attributes *stream_attr,
                            pal_device *pal_dev);

int pa_qst_unload_sound_model(const pa_qst_handle_t *mod_handle,
                              pa_qst_ses_handle_t handle);

int pa_qst_start_recognition(const pa_qst_handle_t *mod_handle,
                             pa_qst_ses_handle_t sound_model_handle,
                             const struct pal_st_recognition_config *config,
                             pa_qst_recognition_callback_t callback,
                             void *cookie);

int pa_qst_stop_recognition(const pa_qst_handle_t *mod_handle,
                            pa_qst_ses_handle_t sound_model_handle);

int pa_qst_set_parameters(const pa_qst_handle_t *mod_handle,
                          pa_qst_ses_handle_t sound_model_handle,
                          const char *kv_pairs);

int pa_qst_get_param_data(const pa_qst_handle_t *mod_handle,
                          pa_qst_ses_handle_t sound_model_handle,
                          const char *param,
                          void *payload,
                          size_t payload_size,
                          size_t *param_data_size);

size_t pa_qst_get_buffer_size(const pa_qst_handle_t *mod_handle,
                              pa_qst_ses_handle_t sound_model_handle);

int pa_qst_read_buffer(const pa_qst_handle_t *mod_handle,
                       pa_qst_ses_handle_t sound_model_handle,
                       unsigned char *buf,
                       size_t bytes);

int pa_qst_stop_buffering(const pa_qst_handle_t *mod_handle,
                          pa_qst_ses_handle_t sound_model_handle);

int pa_qst_get_version(const pa_qst_handle_t *mod_handle);

pa_qst_handle_t *pa_qst_init(const char *module_name);

int pa_qst_deinit(const pa_qst_handle_t *mod_handle);

#endif  //PA_QTI_SOUNDTRIGGER_H
