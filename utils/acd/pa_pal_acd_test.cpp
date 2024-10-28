/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <inttypes.h>
#include <iostream>
#include <signal.h>

#include "pa_pal_acd.h"

#define OK 0
#define MAX_SOUND_TRIGGER_SESSIONS 8
#define MIN_REQ_PARAMS_PER_SESSION 9
#define MAX_ACD_NUMBER_OF_CONTEXT 10

#define PALACD_TEST_USAGE \
    "pa_pal_acd_test [OPTIONS]\n" \
    "Example: pa_pal_acd_test -acd_stream 1 -sr 16000 -ch 1 -dsr 16000 -dch 1 -num_contexts 1 -context_id AUDIO_CONTEXT_EVENT_BABYCRYING -conf_level 95 -step_size 1 -vendor_uuid 4e93281b-296e-4d73-9833-2710c3c7c1db\n" \
    "OPTIONS:\n" \
    "-sr stream sampling rate\n" \
    "-ch stream number of channel\n" \
    "-dsr device sampling rate\n" \
    "-dch device number of channel\n" \
    "-vendor_uuid vendor uuid for the session\n" \
    "-acd_stream enable acd stream\n" \
    "-context_id ACD context detection ID in strings\n" \
    "-conf_level confidence level" \
    "-step_size step size  " \
    "-vendor_uuid vendor uuid"

using namespace std;

typedef enum {
    AUDIO_CONTEXT_ENV_HOME = 0x08001324,
    AUDIO_CONTEXT_ENV_OFFICE,
    AUDIO_CONTEXT_ENV_RESTAURANT,
    AUDIO_CONTEXT_ENV_INDOOR,
    AUDIO_CONTEXT_ENV_INSTREET,
    AUDIO_CONTEXT_ENV_OUTDOOR,
    AUDIO_CONTEXT_ENV_INCAR,
    AUDIO_CONTEXT_ENV_INTRAIN,
    AUDIO_CONTEXT_ENV_UNKNOWN,
    AUDIO_CONTEXT_EVENT_ALARM,
    AUDIO_CONTEXT_EVENT_BABYCRYING,
    AUDIO_CONTEXT_EVENT_DOGBARKING,
    AUDIO_CONTEXT_EVENT_DOORBELL,
    AUDIO_CONTEXT_EVENT_DOOROPENCLOSE,
    AUDIO_CONTEXT_EVENT_CRASH,
    AUDIO_CONTEXT_EVENT_GLASSBREAKING,
    AUDIO_CONTEXT_EVENT_SIREN,
    AUDIO_CONTEXT_AMBIENCE_SPEECH,
    AUDIO_CONTEXT_AMBIENCE_MUSIC,
    AUDIO_CONTEXT_AMBIENCE_NOISY_SPL,
    AUDIO_CONTEXT_AMBIENCE_SILENT_SPL,
    AUDIO_CONTEXT_AMBIENCE_NOISY_SFLUX,
    AUDIO_CONTEXT_AMBIENCE_SILENT_SFLUX,
    AUDIO_CONTEXT_MAX,
} AUDIO_CONTEXT_ENUM;

struct sm_session_data {
    int session_id;
    pa_qst_ses_handle_t ses_handle;
    bool loaded;
    bool started;
    unsigned int num_phrases;
    unsigned int sampling_rate;
    unsigned int channel;
    unsigned int device_sampling_rate;
    unsigned int device_channel;
    struct st_uuid vendor_uuid;
    struct pal_st_recognition_config *rc_config;
    struct pa_pal_phrase_recognition_event *pa_qst_event;
    struct pa_pal_generic_recognition_event *pa_acd_event;
    string context_id[MAX_ACD_NUMBER_OF_CONTEXT];
    unsigned int confidence_level[MAX_ACD_NUMBER_OF_CONTEXT];
    unsigned int step_size[MAX_ACD_NUMBER_OF_CONTEXT];
    unsigned int num_context;
};

const pa_qst_handle_t *pa_qst_handle;
static struct sm_session_data sound_trigger_info;
bool event_received = false;
std::map<std::string,AUDIO_CONTEXT_ENUM> audio_context_map;

/* ACD vendor uuid */
static const struct st_uuid qc_acd_uuid =
    { 0x4e93281b, 0x296e, 0x4d73, 0x9833, { 0x27, 0x10, 0xc3, 0xc7, 0xc1, 0xdb } };

static void *event_handler_thread(void *context);
static int startIndex = 0;
static int endIndex = 0;
bool exit_loop = false;

static void sigint_handler(int sig __unused) {
    event_received = true;
    exit_loop = true;
}

static void init_sm_session_data(void) {
    sound_trigger_info.session_id = 1;
    sound_trigger_info.vendor_uuid = qc_acd_uuid;
    sound_trigger_info.ses_handle = -1;
    sound_trigger_info.num_phrases = 0;
    sound_trigger_info.loaded = false;
    sound_trigger_info.started = false;
    sound_trigger_info.rc_config = NULL;
    sound_trigger_info.pa_qst_event = NULL;
    sound_trigger_info.pa_acd_event = NULL;
}

static void *event_handler_thread(void *context) {
    struct sm_session_data *ses_data = (struct sm_session_data *) context;
    if (!ses_data) {
        printf("Error: context is null\n");
        return NULL;
    }
    pa_pal_phrase_recognition_event *event = ses_data->pa_qst_event;
    pa_qst_ses_handle_t ses_handle = ses_data->ses_handle;
    int rc = event->phrase_event.common.status;
    if (rc == 0) {
        printf("Context ID  detected successfully !!! \n");
        int num_contexts = event->phrase_event.num_phrases;
        printf("Number of contexts detected =%d\n",num_contexts);
        for(int i = 0;i < num_contexts;i++)
        {
            int id = event->phrase_event.phrase_extras[i].id;
            if((id >= AUDIO_CONTEXT_ENV_HOME) && (id <= AUDIO_CONTEXT_MAX)) {
                printf("detected contexted id = %x,event_info type=%d,confidence score=%d\n",
                    event->phrase_event.phrase_extras[i].id,event->phrase_event.phrase_extras[i].recognition_modes,
                    event->phrase_event.phrase_extras[i].confidence_level);
            }
        }
    } else {
        printf("Second stage failed !!!\n");
    }
    free(ses_data->pa_qst_event);
    event_received = true;
    return NULL;
}

static void eventCallback(struct pal_st_recognition_event *event, void *sessionHndl __unused) {

    int rc = 0;
    pthread_attr_t attr;
    pthread_t callback_thread;
    struct sm_session_data *ses_data = &sound_trigger_info;
    struct pa_pal_phrase_recognition_event *pa_qst_event;
    unsigned int event_size;
    uint64_t event_timestamp;
    pa_qst_event = (struct pa_pal_phrase_recognition_event *) event;
    event_timestamp = pa_qst_event->timestamp;

    rc = pthread_attr_init(&attr);
    if (rc != 0) {
        printf("pthread attr init failed %d\n",rc);
        return;
    }

    event_size = sizeof(struct pa_pal_phrase_recognition_event) +
                        pa_qst_event->phrase_event.common.data_size;
    ses_data->pa_qst_event = (struct pa_pal_phrase_recognition_event *)calloc(1, event_size);
    if(ses_data->pa_qst_event == NULL) {
        printf("Could not allocate memory for sm data recognition event");
        return;
    }
    memcpy(ses_data->pa_qst_event, pa_qst_event,
           sizeof(struct pa_pal_phrase_recognition_event));
    memcpy((char *)ses_data->pa_qst_event + ses_data->pa_qst_event->phrase_event.common.data_offset,
           (char *)pa_qst_event + pa_qst_event->phrase_event.common.data_offset,
           pa_qst_event->phrase_event.common.data_size);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    rc = pthread_create(&callback_thread, &attr,
                        event_handler_thread, (void *) ses_data);
    if (rc != 0)
        printf("event_handler_thread create failed %d\n",rc);
    pthread_attr_destroy(&attr);
}

static int string_to_uuid(const char *str, struct st_uuid *uuid) {
    int tmp[10];

    if (str == NULL || uuid == NULL) {
        return -EINVAL;
    }

    if (sscanf(str, "%08x-%04x-%04x-%04x-%02x%02x%02x%02x%02x%02x",
            tmp, tmp + 1, tmp + 2, tmp + 3, tmp + 4, tmp + 5, tmp + 6,
            tmp + 7, tmp+ 8, tmp+ 9) < 10) {
        return -EINVAL;
    }
    uuid->timeLow = (uint32_t)tmp[0];
    uuid->timeMid = (uint16_t)tmp[1];
    uuid->timeHiAndVersion = (uint16_t)tmp[2];
    uuid->clockSeq = (uint16_t)tmp[3];
    uuid->node[0] = (uint8_t)tmp[4];
    uuid->node[1] = (uint8_t)tmp[5];
    uuid->node[2] = (uint8_t)tmp[6];
    uuid->node[3] = (uint8_t)tmp[7];
    uuid->node[4] = (uint8_t)tmp[8];
    uuid->node[5] = (uint8_t)tmp[9];

    return 0;
}

void init_audio_contextinfo()
{
    audio_context_map["AUDIO_CONTEXT_ENV_HOME"] = AUDIO_CONTEXT_ENV_HOME;
    audio_context_map["AUDIO_CONTEXT_ENV_OFFICE"] = AUDIO_CONTEXT_ENV_OFFICE;
    audio_context_map["AUDIO_CONTEXT_ENV_RESTAURANT"] = AUDIO_CONTEXT_ENV_RESTAURANT;
    audio_context_map["AUDIO_CONTEXT_ENV_INDOOR"] = AUDIO_CONTEXT_ENV_INDOOR;
    audio_context_map["AUDIO_CONTEXT_ENV_INSTREET"] = AUDIO_CONTEXT_ENV_INSTREET;
    audio_context_map["AUDIO_CONTEXT_ENV_OUTDOOR"] = AUDIO_CONTEXT_ENV_OUTDOOR;
    audio_context_map["AUDIO_CONTEXT_ENV_INCAR"] = AUDIO_CONTEXT_ENV_INCAR;
    audio_context_map["AUDIO_CONTEXT_ENV_INTRAIN"] = AUDIO_CONTEXT_ENV_INTRAIN;
    audio_context_map["AUDIO_CONTEXT_ENV_UNKNOWN"] = AUDIO_CONTEXT_ENV_UNKNOWN;
    audio_context_map["AUDIO_CONTEXT_EVENT_ALARM"] = AUDIO_CONTEXT_EVENT_ALARM;
    audio_context_map["AUDIO_CONTEXT_EVENT_BABYCRYING"] = AUDIO_CONTEXT_EVENT_BABYCRYING;
    audio_context_map["AUDIO_CONTEXT_EVENT_DOGBARKING"] = AUDIO_CONTEXT_EVENT_DOGBARKING;
    audio_context_map["AUDIO_CONTEXT_EVENT_DOORBELL"] = AUDIO_CONTEXT_EVENT_DOORBELL;
    audio_context_map["AUDIO_CONTEXT_EVENT_DOOROPENCLOSE"] = AUDIO_CONTEXT_EVENT_DOOROPENCLOSE;
    audio_context_map["AUDIO_CONTEXT_EVENT_CRASH"] = AUDIO_CONTEXT_EVENT_CRASH;
    audio_context_map["AUDIO_CONTEXT_EVENT_GLASSBREAKING"] = AUDIO_CONTEXT_EVENT_GLASSBREAKING;
    audio_context_map["AUDIO_CONTEXT_EVENT_SIREN"] = AUDIO_CONTEXT_EVENT_SIREN;
    audio_context_map["AUDIO_CONTEXT_AMBIENCE_SPEECH"] = AUDIO_CONTEXT_AMBIENCE_SPEECH;
    audio_context_map["AUDIO_CONTEXT_AMBIENCE_MUSIC"] = AUDIO_CONTEXT_AMBIENCE_MUSIC;
    audio_context_map["AUDIO_CONTEXT_AMBIENCE_NOISY_SPL"] = AUDIO_CONTEXT_AMBIENCE_NOISY_SPL;
    audio_context_map["AUDIO_CONTEXT_AMBIENCE_SILENT_SPL"] = AUDIO_CONTEXT_AMBIENCE_SILENT_SPL;
    audio_context_map["AUDIO_CONTEXT_AMBIENCE_NOISY_SFLUX"] = AUDIO_CONTEXT_AMBIENCE_NOISY_SFLUX;
    audio_context_map["AUDIO_CONTEXT_AMBIENCE_SILENT_SFLUX"] = AUDIO_CONTEXT_AMBIENCE_SILENT_SFLUX;
    audio_context_map["AUDIO_CONTEXT_MAX"] = AUDIO_CONTEXT_MAX;
}

int main(int argc, char *argv[]) {
    int  i;
    unsigned int j, k;
    uint32_t rc_config_size;
    struct pal_st_sound_model *common_sm = NULL;
    struct pal_st_recognition_config *rc_config = NULL;
    struct pal_device pal_dev;
    struct pal_stream_attributes pal_stream_attr;
    struct sm_session_data *ses_data = NULL;
    pa_qst_ses_handle_t ses_handle = 0;
    uint8_t *payload = NULL;
    pal_param_payload *rec_config_payload = NULL;
    pal_param_payload *sound_model_payload = NULL;
    bool capture_requested = false;
    char command[128];
    int status = 0;
    unsigned int params = 0;

    if (argc < 3) {
        printf(PALACD_TEST_USAGE);
        return 0;
    }

    signal(SIGINT, sigint_handler);
    init_sm_session_data();
    init_audio_contextinfo();
    i = 1;
    j = 0;
    while ((i < argc) && ((i+1) < argc))
    {
        if (strcmp(argv[i], "-sr") == 0) {
            sound_trigger_info.sampling_rate = atoi(argv[i+1]);
            printf("stream sampling_rate%d\n", sound_trigger_info.sampling_rate);
            params++;
        }
        else if (strcmp(argv[i], "-ch") == 0) {
            sound_trigger_info.channel = atoi(argv[i+1]);
            printf("stream channel %d\n", sound_trigger_info.channel);
            params++;
        }
        else if (strcmp(argv[i], "-dsr") == 0) {
            sound_trigger_info.device_sampling_rate = atoi(argv[i+1]);
            printf("device_sampling_rate %d\n", sound_trigger_info.device_sampling_rate);
            params++;
        }
        else if (strcmp(argv[i], "-dch") == 0) {
            sound_trigger_info.device_channel = atoi(argv[i+1]);
            printf("device_channel %d\n", sound_trigger_info.device_channel);
            params++;
        }
        else if (strcmp(argv[i], "-vendor_uuid") == 0) {
            string_to_uuid(argv[i+1], &sound_trigger_info.vendor_uuid);
            params++;
        }
        else if (strcmp(argv[i], "-num_contexts") == 0) {
            sound_trigger_info.num_context = atoi(argv[i+1]);
            params++;
        }
        i += 2;
    }
    for(j = 0;j < sound_trigger_info.num_context;j++)
    {
        cout << "enter context ID context " << j+1;
        cin >> sound_trigger_info.context_id[j];
        cout << "enter confidence_level  context " << j+1;
        cin >> sound_trigger_info.confidence_level[j];
        cout << "enter step_size for  context " << j+1;
        cin >> sound_trigger_info.step_size[j];
    }

    pa_qst_handle = pa_qst_init(PA_QST_MODULE_ID_PRIMARY);
    if (NULL == pa_qst_handle) {
        printf("pa_qst_init() failed\n");
        status = -EINVAL;
        return status;
    }

    unsigned int num_kws = sound_trigger_info.num_context;
    sound_model_payload = (pal_param_payload *)calloc(1, sizeof(pal_param_payload) + sizeof(struct pal_st_sound_model));
    if((!sound_model_payload) || (!sound_model_payload->payload)) {
        printf("Memory allocation for payload failed !\n");
        goto error;
    }

    sound_model_payload->payload_size = sizeof(struct pal_st_sound_model);
    common_sm  = (struct pal_st_sound_model*)sound_model_payload->payload;
    common_sm->type = PAL_SOUND_MODEL_TYPE_GENERIC;
    common_sm->data_size = 0;
    common_sm->data_offset = 0;
    payload = (uint8_t *)((uint8_t *)common_sm + common_sm->data_offset);

    /*vendor_uuid*/
    memcpy(&common_sm->vendor_uuid, &sound_trigger_info.vendor_uuid, sizeof(struct st_uuid));

    rc_config_size = sizeof(struct pal_st_recognition_config);

    rec_config_payload = (pal_param_payload *) calloc(1, sizeof(pal_param_payload) + rc_config_size);
    sound_trigger_info.rc_config = (struct pal_st_recognition_config *)rec_config_payload->payload;

    if (sound_trigger_info.rc_config == NULL) {
        printf("Could not allocate memory for sm data recognition config");
        goto error;
    }
    num_kws = sound_trigger_info.num_context;
    rc_config = sound_trigger_info.rc_config;
    rc_config->num_phrases = num_kws;
    rc_config->data_size = rc_config_size - sizeof(struct pal_st_recognition_config);;
    rc_config->data_offset = sizeof(struct pal_st_recognition_config);
    for (i = 0; i < num_kws; i++) {
        rc_config->phrases[i].recognition_modes = sound_trigger_info.step_size[i];
        rc_config->phrases[i].id = audio_context_map.at(sound_trigger_info.context_id[i]);
        rc_config->phrases[i].confidence_level = sound_trigger_info.confidence_level[i];
    }
    pal_stream_attr.in_media_config.sample_rate = sound_trigger_info.sampling_rate;
    pal_stream_attr.in_media_config.ch_info.channels = sound_trigger_info.channel;
    pal_dev.config.sample_rate = sound_trigger_info.device_sampling_rate;
    pal_dev.config.ch_info.channels = sound_trigger_info.device_channel;

    status = pa_qst_load_sound_model(pa_qst_handle, sound_model_payload, NULL, &ses_handle, &pal_stream_attr, &pal_dev);
    if (OK != status) {
        printf("load_sound_model failed\n");
        goto error;
    }

    sound_trigger_info.loaded = true;
    sound_trigger_info.ses_handle = ses_handle;
    printf("[%d]session params %p, %p, %d\n", k, &sound_trigger_info, rc_config, ses_handle);

    /* not supporting multiple sessions for now */
    ses_data = &sound_trigger_info;
    ses_handle = ses_data->ses_handle;

    do {
           status = pa_qst_start_recognition_v2(pa_qst_handle, ses_handle, rc_config, eventCallback, NULL);
           if (OK != status) {
               printf("start_recognition failed, retrying..\n");
               // Retry after first fail to handle corner SSR failure for now.
               sleep(1);
               status = pa_qst_start_recognition_v2(pa_qst_handle, ses_handle, rc_config, eventCallback, NULL);
               if(OK != status) {
                   printf("start_recognition retry failed!\n");
                   exit_loop = true;
                   goto error;
               }
           }
           printf("start_recognition is success\n");
           while (!event_received) {
               sleep(1);
           }
           status = pa_qst_stop_recognition(pa_qst_handle, ses_handle);
           if (OK != status) {
               printf("stop_recognition failed\n");
           }
           printf("stop_recognition is success\n");
           event_received = false;
       } while(!exit_loop);

error:
    ses_handle = sound_trigger_info.ses_handle;
    if (sound_trigger_info.started) {
        status = pa_qst_stop_recognition(pa_qst_handle, ses_handle);
        if (OK != status)
            printf("stop_recognition failed\n");
            sound_trigger_info.started = false;
    }
    if (sound_trigger_info.loaded) {
        status = pa_qst_unload_sound_model(pa_qst_handle, ses_handle);
        if (OK != status)
            printf("unload_sound_model failed\n");
        sound_trigger_info.loaded = false;
    }


    status = pa_qst_deinit(pa_qst_handle);
    if (OK != status) {
       printf("pa_qst_deinit failed, status %d\n", status);
    }

    if (sound_model_payload)
       free(sound_model_payload);

    if (rec_config_payload)
       free(rec_config_payload);

    return status;
}
