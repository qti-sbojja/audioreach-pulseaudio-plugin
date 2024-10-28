/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef foopalconfparserfoo
#define foopalconfparserfoo

#include <pulsecore/conf-parser.h>

#include <PalApi.h>
#include <PalDefs.h>

#include "pal-card.h"

typedef struct {
    pa_hashmap *ports;
    pa_hashmap *profiles;
    pa_hashmap *sinks;
    pa_hashmap *sources;
    pa_hashmap *loopbacks;
    char *default_profile;
} pa_pal_config_data;

pa_pal_config_data* pa_pal_config_parse_new(char *dir, char *conf_file_name);
void pa_pal_config_parse_free(pa_pal_config_data *config_data);
#endif
