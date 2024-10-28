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
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Copyright 2011, The Android Open Source Project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    * Neither the name of The Android Open Source Project nor the names of
 *      its contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY The Android Open Source Project ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL The Android Open Source Project BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
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

#include "pa_pal_voiceui.h"

#define OK 0
#define MAX_SOUND_TRIGGER_SESSIONS 8
#define FILE_PATH_MAX_LENGTH 256
#define MIN_REQ_PARAMS_PER_SESSION 2
#define SM_MINOR_VERSION 1
#define SM_MINOR_VERSION_2 2
#define MAX_SET_PARAM_KEYS 4
#define MAX_GET_PARAM_KEYS 2
#define MAX_HEX_DATA_SIZE 6
#define ID_RIFF 0x46464952
#define ID_WAVE 0x45564157
#define ID_FMT  0x20746d66
#define ID_DATA 0x61746164
#define FORMAT_PCM 1
#define DEFAULT_ST_SAMPLE_RATE 16000
#define DEFAULT_ST_CHANNELS 1
#define DEFAULT_VENDOR_UIID "68ab2d40-e860-11e3-95ef-0002a5d5c51b"
#define DEFAULT_PHRASE_HEX_VALUE "010040"
#define DEFAULT_S2_CONFIG_VALUE "040040"
#define DEFAULT_OPAQUE_ENABLED 1
#define DEFAULT_NUM_PHRASES_VALUE 1

#define PALVOICEUI_TEST_USAGE \
    "pa_pal_voiceui_test [OPTIONS]\n" \
    "Example: pa_pal_voiceui_test -smz <size> -sm <filepath> -np 1 -pd <hexdata> -s2_conf <hexdata> -sr 48000 -dsr 48000 -ch 1 -dch 2 -ope 1 -vendor_uuid 68ab2d40-e860-11e3-95ef-0002a5d5c51b\n" \
    "OPTIONS:\n" \
    "-smz size of sound model\n" \
    "-sm soundmodel file\n" \
    "-np number of phrasess\n" \
    "-pd phrase data in hex\n" \
    "-s2_conf stage2 config in hex\n" \
    "-ud user data in hex\n" \
    "-sr stream sampling rate\n" \
    "-ch stream number of channel\n" \
    "-dsr device sampling rate\n" \
    "-dch device number of channel\n" \
    "-ope opaque enable(1)/disable(0)\n" \
    "-vendor_uuid vendor uuid for the session\n" \
    "-cmd_file <File name with list of commands to read from>\n"

using namespace std;
struct sm_session_data {
    int session_id;
    pa_qst_ses_handle_t ses_handle;
    bool loaded;
    bool started;
    bool opaque_enabled;
    unsigned int counter;
    unsigned int num_phrases;
    unsigned int sampling_rate;
    unsigned int channel;
    unsigned int device_sampling_rate;
    unsigned int device_channel;
    char sm_file_path[FILE_PATH_MAX_LENGTH];
    string phrase_hex_data;
    string user_hex_data;
    string s2_config;
    struct st_uuid vendor_uuid;
    struct pal_st_recognition_config *rc_config;
    struct pa_pal_phrase_recognition_event *pa_qst_event;
};

struct keyword_buffer_config {
    int version;
    uint32_t kb_duration;
}__attribute__((packed));

/* Definitions from STHAL opaque_header.h to send packed opaque data */
#define ST_MAX_SOUND_MODELS 10
#define ST_MAX_KEYWORDS 10
#define ST_MAX_USERS 10

enum param_key {
    PARAM_KEY_CONFIDENCE_LEVELS,
    PARAM_KEY_HISTORY_BUFFER_CONFIG,
    PARAM_KEY_KEYWORD_INDICES,
    PARAM_KEY_TIMESTAMP,
};

enum sound_model_id {
    SM_ID_NONE = 0x0000,
    SM_ID_SVA_GMM = 0x0001,
    SM_ID_SVA_CNN = 0x0002,
    SM_ID_SVA_VOP = 0x0004,
    SM_ID_SVA_END = 0x00F0,
    SM_ID_CUSTOM_START = 0x0100,
    SM_ID_CUSTOM_END = 0xF000,
};

struct opaque_param_header {
    enum param_key key_id;
    uint32_t payload_size;
}__attribute__((packed));

struct user_levels {
    uint32_t user_id;
    uint8_t level;
}__attribute__((packed));

struct keyword_levels {
    uint8_t kw_level;
    uint32_t num_user_levels;
    struct user_levels user_levels[ST_MAX_USERS];
}__attribute__((packed));

struct sound_model_conf_levels {
    enum sound_model_id sm_id;
    uint32_t num_kw_levels;
    struct keyword_levels kw_levels[ST_MAX_KEYWORDS];
}__attribute__((packed));

struct confidence_levels_info {
    uint32_t version;
    uint32_t num_sound_models;
    struct sound_model_conf_levels conf_levels[ST_MAX_SOUND_MODELS];
}__attribute__((packed));

struct hist_buffer_info {
    uint32_t version;
    uint32_t hist_buffer_duration_msec;
    uint32_t pre_roll_duration_msec;
}__attribute__((packed));

struct keyword_indices_info {
    uint32_t version;
    uint32_t start_index; /* in bytes */
    uint32_t end_index; /* in bytes */
}__attribute__((packed));

struct timestamp_info {
    uint32_t version;
    long first_stage_det_event_timestamp; /* in nanoseconds */
    long second_stage_det_event_timestamp; /* in nanoseconds */
}__attribute__((packed));

struct wav_header {
    uint32_t riff_id;
    uint32_t riff_sz;
    uint32_t riff_fmt;
    uint32_t fmt_id;
    uint32_t fmt_sz;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint32_t data_id;
    uint32_t data_sz;
};

const pa_qst_handle_t *pa_qst_handle;
static struct sm_session_data sound_trigger_info[MAX_SOUND_TRIGGER_SESSIONS];
static unsigned int num_sessions;
static int lab_duration = 5; //5sec is default duration
static int kb_duration_ms = 2000; //2000 msec is default duration
int total_duration_ms = 0;
static int generic_payload_size = 0;
static int pre_roll_duration_ms = 0;
bool event_received = false;

/* SVA vendor uuid */
static const struct st_uuid qc_uuid =
    { 0x68ab2d40, 0xe860, 0x11e3, 0x95ef, { 0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b } };

static void *event_handler_thread(void *context);
static int startIndex = 0;
static int endIndex = 0;
bool exit_loop = false;

static void sigint_handler(int sig __unused) {
    event_received = true;
    exit_loop = true;
}

static struct sm_session_data *get_sm_session_data(int session_id) {
    unsigned int i;

    for (i = 0; i < num_sessions; i++) {
        if (sound_trigger_info[i].session_id == session_id)
            return &sound_trigger_info[i];
    }
    return NULL;
}

static struct sm_session_data *get_sound_trigger_info(pa_qst_ses_handle_t ses_handle) {
    unsigned int i;

    for (i = 0; i < num_sessions; i++) {
        if (sound_trigger_info[i].ses_handle == ses_handle)
            return &sound_trigger_info[i];
    }
    return NULL;
}

static void init_sm_session_data(void) {
    int i;

    for (i = 0; i < MAX_SOUND_TRIGGER_SESSIONS; i++) {
        sound_trigger_info[i].session_id = 1;
        sound_trigger_info[i].vendor_uuid = qc_uuid;
        sound_trigger_info[i].sm_file_path[0] = '\0';
        sound_trigger_info[i].ses_handle = -1;
        sound_trigger_info[i].num_phrases = 0;
        sound_trigger_info[i].loaded = false;
        sound_trigger_info[i].started = false;
        sound_trigger_info[i].counter = 0;
        sound_trigger_info[i].rc_config = NULL;
        sound_trigger_info[i].pa_qst_event = NULL;
    }
}

static size_t get_sample_size_from_format(pal_audio_fmt_t format) {
    size_t size = 0;
    switch (format) {
        case PAL_AUDIO_FMT_PCM_S32_LE:
            size = sizeof(int32_t);
            break;
        case PAL_AUDIO_FMT_PCM_S16_LE:
            size = sizeof(int16_t);
            break;
        case PAL_AUDIO_FMT_PCM_S8:
            size = sizeof(int8_t);
            break;
        default:
            size = sizeof(int16_t);
            break;
    }
    return size;
}

static void process_detection_event(struct sm_session_data *ses_data) {
    int i, j, k, user_id;
    void *payload;
    char *payload_8;
    uint32_t *payload_32;
    uint32_t version, key, key_version, key_size;
    struct st_param_header *param_hdr = NULL;
    struct st_keyword_indices_info *kw_indices = NULL;
    struct pa_pal_phrase_recognition_event *pa_qst_event = ses_data->pa_qst_event;
    struct pal_st_recognition_event *event = &pa_qst_event->phrase_event.common;
    uint32_t parsedSize = 0;
    uint8_t *ptr = (uint8_t *)event + event->data_offset;

    while(parsedSize < event->data_size) {
        param_hdr = (struct st_param_header *)ptr;
        ptr += sizeof(struct st_param_header);
        parsedSize += sizeof(struct st_param_header);
        if (param_hdr->key_id == ST_PARAM_KEY_KEYWORD_INDICES) {

            kw_indices = (struct st_keyword_indices_info *)ptr;
            startIndex = kw_indices->start_index;
            endIndex = kw_indices->end_index;
        }
        ptr += param_hdr->payload_size;
        parsedSize += sizeof(struct st_param_header) + param_hdr->payload_size;
    }
    printf("%s: start_index: %d end_index: %d\n", __func__, startIndex, endIndex);
}

static void capture_lab_data(pal_st_recognition_event *event) {
    void *buffer = NULL;
    size_t bytes, written;
    char lab_capture_file[128] = "";
    size_t total_bytes_to_read = 0, cur_bytes_read = 0, bytes_to_skip = 0, actual_bytes_read = 0;
    FILE *fp;
    bool stopBuffering = false;
    struct sm_session_data *ses_data = NULL;

    pa_qst_ses_handle_t ses_handle = 0;
    ses_data = &sound_trigger_info[0];
    ses_handle = ses_data->ses_handle;
    uint32_t sample_rate = event->media_config.sample_rate;
    uint32_t channels = event->media_config.ch_info.channels;
    pal_audio_fmt_t format = event->media_config.aud_fmt_id;
    size_t samp_sz = get_sample_size_from_format(format);
    struct stat st = {0};
    int bytes_read = 0;
    struct wav_header header;

    bytes = pa_qst_get_buffer_size(pa_qst_handle, ses_handle);
    if (bytes <= 0) {
        printf("Invalid buffer size returned!\n");
        return;
    }
    total_bytes_to_read = ((sample_rate * channels * samp_sz) * total_duration_ms)/1000;
    bytes_to_skip = endIndex - startIndex;
    printf("rate %d, channels %d, samp sz %zu, duration %d, total_bytes_to_read %zu, bytes_to_skip %zu\n",
        sample_rate, channels, samp_sz, total_duration_ms, total_bytes_to_read, bytes_to_skip);
    buffer = calloc(1, bytes);
    if (buffer == NULL) {
        printf("Could not allocate memory for capture buffer\n");
    return;
    }

    if (stat("/tmp/SVA", &st) == -1)
        mkdir("/tmp/SVA", S_IRWXU | S_IRWXG | S_IRWXO);
    snprintf(lab_capture_file, sizeof(lab_capture_file),
            "/tmp/SVA/lab%d.wav", ses_handle);
    fp = fopen(lab_capture_file, "wb");
    if (fp == NULL) {
        printf("Could not open lab capture file : %s\n", lab_capture_file);
        free(buffer);
        return;
    }
    printf("lab capture file : %s\n", lab_capture_file);
    /* Configure RIFF Header parameters */
    header.riff_id = ID_RIFF;
    header.riff_sz = 0;
    header.riff_fmt = ID_WAVE;
    header.fmt_id = ID_FMT;
    header.fmt_sz = 16;
    header.audio_format = FORMAT_PCM;
    header.num_channels = channels;
    header.sample_rate = sample_rate;
    header.bits_per_sample = samp_sz * 8; // samp_sz is no. of bytes/sample
    header.byte_rate = (header.bits_per_sample / 8) * header.num_channels * header.sample_rate;
    header.block_align = header.num_channels * (header.bits_per_sample / 8);
    header.data_id = ID_DATA;
    fseek(fp, sizeof(struct wav_header), 0);
    while (cur_bytes_read < total_bytes_to_read) {
        bytes_read = pa_qst_read_buffer(pa_qst_handle, ses_handle,
                     (unsigned char *) buffer, bytes);
        if (bytes_read > 0) {
            if(!(cur_bytes_read < bytes_to_skip)) {
                written = fwrite(buffer, 1, bytes_read, fp);
                if (written != bytes) {
                    printf("written %zu, bytes %zu\n", written, bytes);
                    if (ferror(fp)) {
                        printf("Error writing lab capture data into file %s\n",strerror(errno));
                        break;
                    }
                    memset(buffer, 0, bytes);
                }
                actual_bytes_read += bytes_read;
            }
            cur_bytes_read += bytes_read;

            /*Stop buffering when not required*/
            if (stopBuffering)
                break;
        }
    }
    printf("bytes to read %zu, actual bytes read %zu\n", total_bytes_to_read, actual_bytes_read);
    header.data_sz = (actual_bytes_read/(channels*(16>>3))) * header.block_align;
    header.riff_sz = header.data_sz + sizeof(header) - 8;
    fseek(fp, 0, 0);
    fwrite(&header, 1, sizeof(struct wav_header), fp);
    pa_qst_stop_buffering(pa_qst_handle, ses_handle);
    free(buffer);
    fclose(fp);
    return;
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
        printf("Wake word is recognized successfully !!! \n");
        if (ses_data->pa_qst_event) {
            process_detection_event(ses_data);
            if (ses_data->pa_qst_event->phrase_event.common.capture_available) {
                printf ("Capturing LAB buffer...\n");
                capture_lab_data(&ses_data->pa_qst_event->phrase_event.common);
            }
        }
    } else {
        printf("Second stage failed !!!\n");
    }
    event_received = true;
    return NULL;
}

static void eventCallback(struct pal_st_recognition_event *event, void *sessionHndl __unused) {

    int rc = 0;
    pthread_attr_t attr;
    pthread_t callback_thread;
    struct sm_session_data *ses_data = &sound_trigger_info[0];
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

static void fill_opaque_data(struct pal_st_recognition_config *rec_config, int index)
{
    uint8_t *payload = NULL;
    uint32_t parsedSize = 0;
    struct st_param_header *header = NULL;
    struct st_confidence_levels_info_v2 *conf_info = NULL;
    struct st_hist_buffer_info *hist_buffer_info = NULL;

    payload = (uint8_t *)rec_config + rec_config->data_offset;
    header = (struct st_param_header *)payload;
    header->key_id = ST_PARAM_KEY_CONFIDENCE_LEVELS;
    header->payload_size = sizeof(struct st_confidence_levels_info_v2);

    payload += sizeof(struct st_param_header);
    conf_info = (struct st_confidence_levels_info_v2 *)payload;
    conf_info->version = 0x2;
    conf_info->num_sound_models = 3;  // support for GMM, CNN, VOP model
    conf_info->conf_levels[0].sm_id = ST_SM_ID_SVA_F_STAGE_GMM;
    conf_info->conf_levels[0].num_kw_levels = rec_config->num_phrases;
    for (int i = 0; i < rec_config->num_phrases; i++) {
        conf_info->conf_levels[0].kw_levels[i].kw_level =
            rec_config->phrases[i].confidence_level;
        conf_info->conf_levels[0].kw_levels[i].num_user_levels =
            rec_config->phrases[i].num_levels;
        for (int j = 0; j < rec_config->phrases[i].num_levels; j++) {
            conf_info->conf_levels[0].kw_levels[i].user_levels[j].user_id =
                rec_config->phrases[i].levels[j].user_id;
            conf_info->conf_levels[0].kw_levels[i].user_levels[j].level =
                rec_config->phrases[i].levels[j].level;
        }
    }

    conf_info->conf_levels[1].num_kw_levels = rec_config->num_phrases;
    conf_info->conf_levels[2].num_kw_levels = rec_config->num_phrases;
    conf_info->conf_levels[1].sm_id = ST_SM_ID_SVA_S_STAGE_PDK;
    conf_info->conf_levels[2].sm_id = ST_SM_ID_SVA_S_STAGE_USER;
    for (int i = 0; i < rec_config->num_phrases; i++) {
        conf_info->conf_levels[1].kw_levels[i].kw_level =
            std::stoi(sound_trigger_info[0].s2_config.substr(parsedSize, 2), 0, 10);
        parsedSize += 2;

        if (rec_config->phrases[i].num_levels > 0) {
            for (int j = 0; j < rec_config->phrases[i].num_levels; j++) {
                conf_info->conf_levels[2].kw_levels[i].user_levels[j].user_id =
                    rec_config->phrases[i].levels[j].user_id;
                conf_info->conf_levels[2].kw_levels[i].user_levels[j].level =
                    std::stoi(sound_trigger_info[0].s2_config.substr(parsedSize + 2, 2), 0, 10);
                parsedSize += 2;
            }
        }
    }

    payload += sizeof(struct st_confidence_levels_info_v2);
    header = (struct st_param_header *)payload;
    header->key_id = ST_PARAM_KEY_HISTORY_BUFFER_CONFIG;
    header->payload_size = sizeof(struct st_hist_buffer_info);

    payload += sizeof(struct st_param_header);
    hist_buffer_info = (struct st_hist_buffer_info *)payload;
    hist_buffer_info->version = 0x2;
    hist_buffer_info->hist_buffer_duration_msec = 0;
    hist_buffer_info->pre_roll_duration_msec = 0;
    hist_buffer_info->hist_buffer_duration_msec = 1750; //TODO: must come from cmdline
    hist_buffer_info->pre_roll_duration_msec = 250; //TODO: must come from cmdline
}

int main(int argc, char *argv[]) {
    int sm_data_size  = 0, i;
    int sound_model_size = 0;
    unsigned int j, k;
    uint32_t rc_config_size;
    struct pal_st_sound_model *common_sm = NULL;
    struct pal_st_phrase_sound_model *phrase_sm = NULL;
    struct pal_st_recognition_config *rc_config = NULL;
    struct pal_device pal_dev;
    struct pal_stream_attributes pal_stream_attr;
    struct sm_session_data *ses_data = NULL;
    pa_qst_ses_handle_t ses_handle = NULL;
    uint8_t *payload = NULL;
    pal_param_payload *rec_config_payload = NULL;
    pal_param_payload *sound_model_payload = NULL;
    bool capture_requested = false;
    bool lookahead_buffer = false, keyword_buffer = false;
    bool usr_req_lookahead_buffer = false;
    unsigned int custom_payload = 0;
    unsigned int custom_payload_size = 0;
    int index = 0;
    char command[128];
    int status = 0;
    FILE *fp = NULL, *cmd_fp = NULL;
    char cmd_file_path[FILE_PATH_MAX_LENGTH];
    uint32_t parsedSize = 0;
    unsigned int count = 0;

    if (argc < 3) {
        printf(PALVOICEUI_TEST_USAGE);
        return 0;
    }

    signal(SIGINT, sigint_handler);
    init_sm_session_data();

    /*Supporting only 1 session for now*/
    num_sessions = 1;
    i = 1;
    /* Filling default values */
    sound_trigger_info[index].sampling_rate = DEFAULT_ST_SAMPLE_RATE ;
    sound_trigger_info[index].channel = DEFAULT_ST_CHANNELS ;
    sound_trigger_info[index].device_sampling_rate = DEFAULT_ST_SAMPLE_RATE;
    sound_trigger_info[index].device_channel = DEFAULT_ST_CHANNELS ;
    sound_trigger_info[index].opaque_enabled = DEFAULT_OPAQUE_ENABLED;
    string_to_uuid(DEFAULT_VENDOR_UIID, &sound_trigger_info[index].vendor_uuid);
    sound_trigger_info[index].phrase_hex_data.assign(DEFAULT_PHRASE_HEX_VALUE);
    sound_trigger_info[index].s2_config.assign(DEFAULT_S2_CONFIG_VALUE);
    sound_trigger_info[index].num_phrases = DEFAULT_NUM_PHRASES_VALUE;

    while ((i < argc) && ((i+1) < argc))
    {
        if (strcmp(argv[i], "-smz") == 0) {
            sound_model_size = atoi(argv[i+1]);
            printf("sound_model_size=%d\n", sound_model_size);
            count++;
        }
        else if (strcmp(argv[i], "-sm") == 0) {
            strlcpy(sound_trigger_info[index].sm_file_path, argv[i+1],
                       sizeof(sound_trigger_info[index].sm_file_path));
            printf("sm file path= %s\n", sound_trigger_info[index].sm_file_path);
            count++;
        }
        else if (strcmp(argv[i], "-np") == 0) {
            sound_trigger_info[index].num_phrases = atoi(argv[i+1]);
            if (sound_trigger_info[index].num_phrases > ST_MAX_KEYWORDS) {
                printf("Invalid number_phrases, max allowed is %d\n", ST_MAX_KEYWORDS);
                status = -EINVAL;
                goto exit;
            }
            printf("num_phrases %d\n", sound_trigger_info[index].num_phrases);
        }
        else if (strcmp(argv[i], "-pd") == 0) {
            sound_trigger_info[index].phrase_hex_data.assign(argv[i+1]);
            printf("phrase_hex_data %s\n", sound_trigger_info[index].phrase_hex_data.c_str());
        }
        else if (strcmp(argv[i], "-ud") == 0) {
            sound_trigger_info[index].user_hex_data.assign(argv[i+1]);
            printf("user_hex_data %s\n", sound_trigger_info[index].user_hex_data.c_str());
        }
        else if (strcmp(argv[i], "-s2_conf") == 0) {
            sound_trigger_info[index].s2_config.assign(argv[i+1]);
            printf("s2_config_hex %s\n", sound_trigger_info[index].s2_config.c_str());
        }
        else if (strcmp(argv[i], "-sr") == 0) {
            sound_trigger_info[index].sampling_rate = atoi(argv[i+1]);
            printf("stream sampling_rate%d\n", sound_trigger_info[index].sampling_rate);
        }
        else if (strcmp(argv[i], "-ch") == 0) {
            sound_trigger_info[index].channel = atoi(argv[i+1]);
            printf("stream channel %d\n", sound_trigger_info[index].channel);
        }
        else if (strcmp(argv[i], "-dsr") == 0) {
            sound_trigger_info[index].device_sampling_rate = atoi(argv[i+1]);
            printf("device_sampling_rate %d\n", sound_trigger_info[index].device_sampling_rate);
        }
        else if (strcmp(argv[i], "-dch") == 0) {
            sound_trigger_info[index].device_channel = atoi(argv[i+1]);
            printf("device_channel %d\n", sound_trigger_info[index].device_channel);
        }
        else if (strcmp(argv[i], "-ope") == 0) {
            sound_trigger_info[index].opaque_enabled = atoi(argv[i+1]);
            printf("opaque enabled %d\n", sound_trigger_info[index].opaque_enabled);
        }
        else if (strcmp(argv[i], "-vendor_uuid") == 0) {
            string_to_uuid(argv[i+1], &sound_trigger_info[index].vendor_uuid);
        }
        /*TODO: below options to be implemented*/
        else if ((strcmp(argv[i], "-lab") == 0) && ((i+1) < argc)) {
            lookahead_buffer =
                  (0 == strncasecmp(argv[i+1], "true", 4))? true:false;
            usr_req_lookahead_buffer = true;
        }
        else if ((strcmp(argv[i], "-lab_duration") == 0) && ((i+1) < argc)) {
            lab_duration = atoi(argv[i+1]);
        }
        else if ((strcmp(argv[i], "-kb") == 0) && ((i+1) < argc)) {
            keyword_buffer =
                  (0 == strncasecmp(argv[i+1], "true", 4))? true:false;
        }
        else if ((strcmp(argv[i], "-kb_duration") == 0) && ((i+1) < argc)) {
            kb_duration_ms = atoi(argv[i+1]);
        }
        else if ((strcmp(argv[i], "-pre_roll_duration") == 0) && ((i+1) < argc)) {
            pre_roll_duration_ms = atoi(argv[i+1]);
        }
        else if ((strcmp(argv[i], "-cmd_file") == 0) && ((i+1) < argc)) {
            strlcpy(cmd_file_path, argv[i+1], sizeof(cmd_file_path));
            cmd_fp = fopen(cmd_file_path, "rb");
            if (cmd_fp == NULL) {
                printf("Could not open command file path : %s\n", cmd_file_path);
                goto exit;
            }
        }
        else {
            printf("Invalid syntax\n");
            printf(PALVOICEUI_TEST_USAGE);
            goto exit;
        }
        i += 2;
    }

    if (count != MIN_REQ_PARAMS_PER_SESSION) {
        printf("Insufficient data entered\n");
        printf(PALVOICEUI_TEST_USAGE);
        goto exit;
    }
    if (usr_req_lookahead_buffer) {
        if ((lookahead_buffer == false) && (keyword_buffer == true)) {
            printf("Invalid usecase: lab can't be false when keyword buffer is true ");
            status = -EINVAL;
            goto exit;
        }
    }
    printf("keyword buffer %d\n",keyword_buffer);
    capture_requested = (lookahead_buffer || keyword_buffer) ? true : false;
    total_duration_ms = (lookahead_buffer ? lab_duration * 1000 : 0) +
                        (keyword_buffer ? kb_duration_ms : 0) + pre_roll_duration_ms;

    pa_qst_handle = pa_qst_init(PA_QST_MODULE_ID_PRIMARY);
    if (NULL == pa_qst_handle) {
        printf("pa_qst_init() failed\n");
        status = -EINVAL;
        goto exit;
    }

    for (k = 0; k < num_sessions; k++) {
        unsigned int num_kws = sound_trigger_info[k].num_phrases;
        pa_qst_ses_handle_t ses_handle = 0;

        if (fp)
            fclose(fp);
        fp = fopen(sound_trigger_info[k].sm_file_path, "rb");
        if (fp == NULL) {
            printf("Could not open sound model file : %s\n",
                                   sound_trigger_info[k].sm_file_path);
            goto error;
        }

        /* Get the sound mode size i.e. file size */
        fseek( fp, 0, SEEK_END);
        sm_data_size  = ftell(fp);
        fseek( fp, 0, SEEK_SET);

        if (sm_data_size != sound_model_size) {
            printf("sound model size does not match size of file\n");
            goto error;
        }

        sound_model_payload = (pal_param_payload *)calloc(1, sizeof(pal_param_payload) + sizeof(struct pal_st_phrase_sound_model) + sound_model_size);
        if((!sound_model_payload) || (!sound_model_payload->payload)) {
            printf("Memory allocation for payload failed !\n");
            goto error;
        }

        sound_model_payload->payload_size = sizeof(struct pal_st_phrase_sound_model) + sound_model_size;
        phrase_sm = (struct pal_st_phrase_sound_model *)sound_model_payload->payload;
        common_sm = (struct pal_st_sound_model *)phrase_sm;
        common_sm->type = PAL_SOUND_MODEL_TYPE_KEYPHRASE;
        common_sm->data_size = sm_data_size;
        common_sm->data_offset = sizeof(struct pal_st_phrase_sound_model);

        payload = (uint8_t *)((uint8_t *)common_sm + common_sm->data_offset);
        int bytes_read = (int)fread((char*)payload, 1, sm_data_size , fp);
        if (bytes_read != sm_data_size) {
            printf("bytes read %d not match with model size %d\n", bytes_read, sm_data_size);
            goto error;
        }

        /*vendor_uuid*/
        memcpy(&common_sm->vendor_uuid, &sound_trigger_info[k].vendor_uuid, sizeof(struct st_uuid));

        rc_config_size = sizeof(struct pal_st_recognition_config);

        if (sound_trigger_info[k].opaque_enabled)
            rc_config_size += sizeof(struct st_param_header) * 2 + sizeof(struct st_confidence_levels_info_v2) + sizeof(struct st_hist_buffer_info);
        else if (custom_payload)
            rc_config_size += custom_payload_size;

        rec_config_payload = (pal_param_payload *) calloc(1, sizeof(pal_param_payload) + rc_config_size);
        sound_trigger_info[k].rc_config = (struct pal_st_recognition_config *)rec_config_payload->payload;

        if (sound_trigger_info[k].rc_config == NULL) {
            printf("Could not allocate memory for sm data recognition config");
            goto error;
        }
        rc_config = sound_trigger_info[k].rc_config;
        rc_config->capture_handle = 0;
        rc_config->capture_device = PAL_DEVICE_IN_HANDSET_VA_MIC;
        rc_config->capture_requested = capture_requested;
        rc_config->num_phrases = num_kws;
        rc_config->data_size = rc_config_size - sizeof(struct pal_st_recognition_config);;
        rc_config->data_offset = sizeof(struct pal_st_recognition_config);

        phrase_sm->num_phrases = num_kws;
        for (i = 0; i < num_kws; i++) {
            int phraseId = std::stoi(sound_trigger_info[k].phrase_hex_data.substr(0, 2), 0, 16);
            int userNum = std::stoi(sound_trigger_info[k].phrase_hex_data.substr(2, 2), 0, 16);
            int confLevel = std::stoi(sound_trigger_info[k].phrase_hex_data.substr(4, 2), 0, 16);
            phrase_sm->phrases[i].recognition_mode = PAL_RECOGNITION_MODE_VOICE_TRIGGER;
            phrase_sm->phrases[i].id = phraseId;
            phrase_sm->phrases[i].num_users = userNum;
            rc_config->phrases[i].recognition_modes = PAL_RECOGNITION_MODE_VOICE_TRIGGER;
            rc_config->phrases[i].id = phraseId;
            rc_config->phrases[i].num_levels = userNum;
            rc_config->phrases[i].confidence_level = confLevel;
            for (j = 0; j < userNum; j++) {
                int userId = std::stoi(sound_trigger_info[k].user_hex_data.substr(parsedSize, 2), 0, 16);
                int userConf = std::stoi(sound_trigger_info[k].user_hex_data.substr(parsedSize + 2, 2), 0, 16);
                phrase_sm->phrases[i].users[j] = userId;
                rc_config->phrases[i].levels[j].level = userConf;
                rc_config->phrases[i].levels[j].user_id = userId;
                parsedSize += 4;
            }
        }

        if(sound_trigger_info[k].opaque_enabled)
            fill_opaque_data(rc_config, index);

        pal_stream_attr.in_media_config.sample_rate = sound_trigger_info[k].sampling_rate;
        pal_stream_attr.in_media_config.ch_info.channels = sound_trigger_info[k].channel;
        pal_dev.config.sample_rate = sound_trigger_info[k].device_sampling_rate;
        pal_dev.config.ch_info.channels = sound_trigger_info[k].device_channel;

        status = pa_qst_load_sound_model(pa_qst_handle, sound_model_payload, NULL, &ses_handle, &pal_stream_attr, &pal_dev);
        if (OK != status) {
            printf("load_sound_model failed\n");
            goto error;
        }

        sound_trigger_info[k].loaded = true;
        sound_trigger_info[k].ses_handle = ses_handle;
        printf("[%d]session params %p, %p, %d\n", k, &sound_trigger_info[k], rc_config, ses_handle);
    }

    /* not supporting multiple sessions for now */
    ses_data = &sound_trigger_info[0];
    ses_handle = ses_data->ses_handle;

    //TODO: Add support for custom payload parsing

    do {
           status = pa_qst_start_recognition(pa_qst_handle, ses_handle, rc_config, eventCallback, NULL);
           if (OK != status) {
               printf("start_recognition failed, retrying..\n");
               // Retry after first fail to handle corner SSR failure for now.
               sleep(1);
               status = pa_qst_start_recognition(pa_qst_handle, ses_handle, rc_config, eventCallback, NULL);
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
    for (i = 0; i < num_sessions; i++) {
        pa_qst_ses_handle_t ses_handle = sound_trigger_info[i].ses_handle;
        if (sound_trigger_info[i].started) {
            status = pa_qst_stop_recognition(pa_qst_handle, ses_handle);
            if (OK != status)
                printf("stop_recognition failed\n");
            sound_trigger_info[i].started = false;
        }
        if (sound_trigger_info[i].loaded) {
            status = pa_qst_unload_sound_model(pa_qst_handle, ses_handle);
            if (OK != status)
                printf("unload_sound_model failed\n");
            sound_trigger_info[i].loaded = false;
        }
    }

    status = pa_qst_deinit(pa_qst_handle);
    if (OK != status) {
       printf("pa_qst_deinit failed, status %d\n", status);
    }

    if (sound_model_payload)
       free(sound_model_payload);

    if (rec_config_payload)
       free(rec_config_payload);

    if (fp)
        fclose(fp);
exit:
    if (cmd_fp)
        fclose(cmd_fp);

    return status;
}
