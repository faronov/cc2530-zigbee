/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "zcl_basic.h"

#include <stddef.h>
#include <string.h>

/* Protocol constants in CODE, without board/MMIO dependencies. */
#if defined(__SDCC)
#define BASIC_CODE __code
#else
#define BASIC_CODE
#endif

static const BASIC_CODE uint16_t ids[ZCL_BASIC_ATTRIBUTE_COUNT] = {
    0x0000, 0x0004, 0x0005, 0x0007, 0x4000, 0xfffd
};
static const BASIC_CODE uint8_t types[ZCL_BASIC_ATTRIBUTE_COUNT] = {
    ZCL_TYPE_UINT8, ZCL_TYPE_CHARACTER_STRING, ZCL_TYPE_CHARACTER_STRING,
    ZCL_TYPE_ENUM8, ZCL_TYPE_CHARACTER_STRING, ZCL_TYPE_UINT16
};

zcl_codec_result_t zcl_basic_init(zcl_basic_t * volatile model,
                                  const zcl_basic_config_t * volatile config)
{
    uint8_t i;
    if (model == NULL || config == NULL)
        return ZCL_CODEC_INVALID_ARGUMENT;
    if ((config->manufacturer.data == NULL && config->manufacturer.length != 0u)
            || (config->model.data == NULL && config->model.length != 0u)
            || (config->software.data == NULL && config->software.length != 0u))
        return ZCL_CODEC_INVALID_ARGUMENT;
    if (config->manufacturer.length > ZCL_BASIC_NAME_MAX
            || config->model.length > ZCL_BASIC_NAME_MAX
            || config->software.length > ZCL_BASIC_BUILD_MAX
            || (config->power_source & 0x7fu) > 6u)
        return ZCL_CODEC_INVALID_VALUE;

    /* No fallible operation after admission. Zero native padding as well. */
    memset(model, 0, sizeof(*model));
    model->set.attributes = model->attributes;
    model->set.count = ZCL_BASIC_ATTRIBUTE_COUNT;
    model->set.side = ZCL_ATTRIBUTE_SERVER;
    for (i = 0; i < ZCL_BASIC_ATTRIBUTE_COUNT; i++) {
        model->attributes[i].id = ids[i];
        model->attributes[i].readable = 1;
        model->attributes[i].value.type = types[i];
    }
    model->scalars[0] = 8;
    model->scalars[1] = config->power_source;
    model->scalars[2] = 3; /* LE16 ClusterRevision, not the library revision. */
    model->attributes[0].value.data = model->scalars;
    model->attributes[0].value.data_length = 1;
    model->attributes[3].value.data = model->scalars + 1;
    model->attributes[3].value.data_length = 1;
    model->attributes[5].value.data = model->scalars + 2;
    model->attributes[5].value.data_length = 2;

    if (config->manufacturer.length != 0u)
        memcpy(model->strings, config->manufacturer.data, config->manufacturer.length);
    if (config->model.length != 0u)
        memcpy(model->strings + 32, config->model.data, config->model.length);
    if (config->software.length != 0u)
        memcpy(model->strings + 64, config->software.data, config->software.length);
    model->attributes[1].value.data = model->strings;
    model->attributes[1].value.data_length = config->manufacturer.length;
    model->attributes[2].value.data = model->strings + 32;
    model->attributes[2].value.data_length = config->model.length;
    model->attributes[4].value.data = model->strings + 64;
    model->attributes[4].value.data_length = config->software.length;
    return ZCL_CODEC_OK;
}
