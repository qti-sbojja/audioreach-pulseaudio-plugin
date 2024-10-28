/*
 * Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef fooqsthwutilfoo
#define fooqsthwutilfoo

typedef struct pa_pal_voiceui_hooks pa_pal_voiceui_hooks;

typedef enum pa_pal_voiceui_hook {
    PA_HOOK_PAL_VOICEUI_START_DETECTION,
    PA_HOOK_PAL_VOICEUI_STOP_DETECTION,
    PA_HOOK_PAL_VOICEUI_MAX,
} pa_pal_voiceui_hook_t;

typedef struct {
    struct pal_st_phrase_recognition_event phrase_event;
    uint64_t timestamp;
} pa_pal_st_phrase_recognition_event;

struct pa_pal_voiceui_hooks {
    pa_hook hooks[PA_HOOK_PAL_VOICEUI_MAX];
};

#endif //fooqsthwutilfoo
