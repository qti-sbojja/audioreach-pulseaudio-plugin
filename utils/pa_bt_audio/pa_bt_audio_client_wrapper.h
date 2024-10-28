/**
*=============================================================================
* \file pa_bt_audio_client_wrapper.h
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

#ifndef __PA_BT_AUDIO_CLIENT_WRAPPER_H__
#define __PA_BT_AUDIO_CLIENT_WRAPPER_H__

#define PA_BT_CLIENT_WRAPPER_LIB "libpa_bt_audio_client_wrapper.so"
#define E_SUCCESS 0
#define E_FAILURE -1

typedef enum {
    AUDIO_PARAMETER_KEY_INVALID = 0,
    AUDIO_PARAMETER_KEY_BTSINK_ENABLE = 1,
    AUDIO_PARAMETER_KEY_BTSINK_SET_VOLUME,
    AUDIO_PARAMETER_KEY_BTSINK_SET_MUTE,
    AUDIO_PARAMETER_KEY_BTSINK_GET_VOLUME,
    AUDIO_PARAMETER_KEY_HFP_ENABLE,
    AUDIO_PARAMETER_KEY_HFP_SET_SAMPLING_RATE,
    AUDIO_PARAMETER_KEY_HFP_SET_SPK_VOLUME,
    AUDIO_PARAMETER_KEY_HFP_SET_MIC_VOLUME,
    AUDIO_PARAMETER_KEY_HFP_SET_SPK_MUTE,
    AUDIO_PARAMETER_KEY_HFP_SET_MIC_MUTE,
    AUDIO_PARAMETER_KEY_HFP_GET_SAMPLING_RATE,
    AUDIO_PARAMETER_KEY_HFP_GET_SPK_VOLUME,
    AUDIO_PARAMETER_KEY_HFP_GET_MIC_VOLUME,
    AUDIO_PARAMETER_KEY_BTSRC_A2DP_SUSPEND,
    AUDIO_PARAMETER_KEY_MAX
} audio_param_key_t;

static char const *audio_prmkey_names[AUDIO_PARAMETER_KEY_MAX] = {
    [AUDIO_PARAMETER_KEY_INVALID]                  = "",

    /** Start bta2dp sink use case. Values supported: true & false */
    [AUDIO_PARAMETER_KEY_BTSINK_ENABLE]            = "btsink_enable",

    /** Sets bta2dp volume. Values supported: 0.0 to 15.0 (float values) */
    [AUDIO_PARAMETER_KEY_BTSINK_SET_VOLUME]        = "btsink_volume",

    /** Sets bta2dp sink volume to mute. Values supported: true & false */
    [AUDIO_PARAMETER_KEY_BTSINK_SET_MUTE]          = "btsink_mute",

    /** Returns bta2dp sink volume. Values type: float. Value range: 0.0 to 15.0 */
    [AUDIO_PARAMETER_KEY_BTSINK_GET_VOLUME]        = "btsink_get_volume",

    /** Starts HFP client use case. Values supported: true & false */
    [AUDIO_PARAMETER_KEY_HFP_ENABLE]               = "hfp_enable",

    /** Sets sample rate for HFP client TX & RX. Value supported: 8000 & 16000 */
    [AUDIO_PARAMETER_KEY_HFP_SET_SAMPLING_RATE]    = "hfp_sample_rate",

    /** Sets HFP client RX volume. Value supported: 0.0 to 15.0 */
    [AUDIO_PARAMETER_KEY_HFP_SET_SPK_VOLUME]       = "hfp_volume",

    /** Sets HFP client TX volume. Value supported: 0.0 to 15.0 */
    [AUDIO_PARAMETER_KEY_HFP_SET_MIC_VOLUME]       = "hfp_mic_volume",

    /** Sets HFP client RX volume to mute. Value supported: true & false */
    [AUDIO_PARAMETER_KEY_HFP_SET_SPK_MUTE]         = "hfp_spk_mute",

    /** Sets HFP client TX volume to mute. Value supported: true & false */
    [AUDIO_PARAMETER_KEY_HFP_SET_MIC_MUTE]         = "hfp_mic_mute",

    /** Returns HFP client use case sample rate. Value supported: 8000 & 16000 */
    [AUDIO_PARAMETER_KEY_HFP_GET_SAMPLING_RATE]    = "hfp_get_sample_rate",

    /** Returns HFP client RX volume. Values type: float. Value range: 0.0 to 15.0 */
    [AUDIO_PARAMETER_KEY_HFP_GET_SPK_VOLUME]       = "hfp_get_volume",

    /** Returns HFP client TX volume. Values type: float. Value range: 0.0 to 15.0 */
    [AUDIO_PARAMETER_KEY_HFP_GET_MIC_VOLUME]       = "hfp_get_mic_volume",

    /** Set bta2dp steam to suspend state. Values supported: true & false */
    [AUDIO_PARAMETER_KEY_BTSRC_A2DP_SUSPEND]       = "bta2dp_suspend"
};

typedef enum {
    PA_BT_INVALID = 0,
    PA_BT_A2DP_SINK = 1,
    PA_BT_HFP_CLIENT,
    PA_BT_A2DP_SOURCE,
    PA_BT_HFP_AG
} pa_bt_usecase_type_t;

typedef enum {
    PA_BT_PCM_FORMAT = 1
} pa_audio_format_t;

static char const *usecase_name[] = {
    [PA_BT_INVALID]       = "",
    [PA_BT_A2DP_SINK]     = "bta2dp",
    [PA_BT_HFP_CLIENT]    = "btsco",
    [PA_BT_A2DP_SOURCE]   = "bta2dp_src",
    [PA_BT_HFP_AG]        = "bthfp_ag"
};

#define PA_SINK_LOW_LATENCY "low-latency0"

/* Lib function pointer typedefs */

/**
  * \brief- Connect to pulseaudio server for a use case.
  *
  * \param[in] usecase_type - Valid usecase type
  * \param[in] connect - true/false
  *
  * \return 0 on success, error code otherwise
  */
typedef int (*pa_bt_connect_fn_t)(pa_bt_usecase_type_t usecase_type, bool connect);

/**
  * \brief- Set use case parameters
  *
  * \param[in] usecase_type - Valid usecase type
  * \param[in] param - kv pair based on the param to be set
  *                    Ex: "hfp_enable=true", "btsink_volume=10"
  *
  * \return 0 on success, error code otherwise
  */
typedef int (*pa_bt_set_param_fn_t)(pa_bt_usecase_type_t usecase_type, const char *param);

/**
  * \brief- Get use case parameters
  *
  * \param[in] usecase_type - Valid usecase type
  * \param[in] query - param key based on the param to be fetched
  *                    Ex: "hfp_get_volume", "btsink_get_volume"
  * \param[in/out] reply - Pointer to the params returned. Memory
  *                        needs to be allocated by client.
  *                        Ex: sizeof(float) for volume.
  *                        sizeof(unsigned int) for sample rate.
  *
  * \return 0 on success, error code otherwise
  */
typedef int (*pa_bt_get_param_fn_t)(pa_bt_usecase_type_t usecase_type,
        const char *query, void *reply);

/**
  * \brief- Initialize the sink for playback
  *
  * \param[in] sink_name- Sink name for playback Ex: low-latency0
  * \param[in] bit_depth- Bit depth of the pcm data Ex: 16/32/24
  * \param[in] sampling_rate- Sampling rate of pcm data
  * \param[in] channels- Number of channels
  * \param[in] channels- Format of data Ex: PA_BT_PCM_FORMAT
  *
  * \return 0 on success, error code otherwise
  */
typedef int (*pa_sink_init_fn_t)(char *sink_name, unsigned int bit_depth,
        unsigned int sampling_rate, unsigned int channels, pa_audio_format_t format);

/**
  * \brief- Play pcm samples on the sink
  *
  * \param[in] buffer- Buffer containing pcm data
  * \param[in] bytes- Size of data on the buffer
  *
  * \return true on successful write, false otherwise
  */
typedef bool (*pa_sink_play_fn_t)(char *buffer, size_t bytes);

/**
  * \brief- Deinit the sink
  */
typedef void (*pa_sink_deinit_fn_t)(void);

#endif //__PA_BT_AUDIO_CLIENT_WRAPPER_H__
