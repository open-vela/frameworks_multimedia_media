/****************************************************************************
 * frameworks/media/media_parcel.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "media_parcel.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int media_parcel_grow(media_parcel *parcel, size_t newsz)
{
    media_parcel_chunk *newchunk;

    newsz += parcel->chunk->len;
    if (newsz <= parcel->cap)
        return 0;

    if (newsz <= 2 * parcel->cap)
        newsz = 2 * parcel->cap;

    if (parcel->chunk == &parcel->prealloc) {
        newchunk = malloc(MEDIA_PARCEL_HEADER_LEN + newsz);
        if (newchunk) {
            memcpy(newchunk, parcel->chunk,
                   MEDIA_PARCEL_HEADER_LEN + parcel->chunk->len);
        }
    } else {
        newchunk = realloc(parcel->chunk, MEDIA_PARCEL_HEADER_LEN + newsz);
    }

    if (newchunk == NULL)
        return -ENOMEM;

    parcel->chunk = newchunk;
    parcel->cap = newsz;
    return 0;
}

static int media_parcel_copy(media_parcel *parcel, void *val, size_t size)
{
    if (parcel->next + size > parcel->chunk->len)
        return -ENOSPC;

    memcpy(val, &parcel->chunk->buf[parcel->next], size);
    parcel->next += size;

    return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void media_parcel_init(media_parcel *parcel)
{
    parcel->chunk = &parcel->prealloc;
    parcel->next = 0;
    parcel->cap = MEDIA_PARCEL_DATA_LEN;
    parcel->chunk->code = 0;
    parcel->chunk->len = 0;
}

void media_parcel_deinit(media_parcel *parcel)
{
    if (parcel->chunk != &parcel->prealloc)
        free(parcel->chunk);
}

void media_parcel_reinit(media_parcel *parcel)
{
    media_parcel_deinit(parcel);
    media_parcel_init(parcel);
}

int media_parcel_append(media_parcel *parcel, const void *data, size_t size)
{
    int rv;

    if (size == 0)
        return 0;

    if ((rv = media_parcel_grow(parcel, size)) < 0)
        return rv;

    memcpy(&parcel->chunk->buf[parcel->chunk->len], data, size);
    parcel->chunk->len += size;

    return 0;
}

int media_parcel_append_uint16(media_parcel *parcel, uint16_t val)
{
    return media_parcel_append(parcel, &val, sizeof(val));
}

int media_parcel_append_uint32(media_parcel *parcel, uint32_t val)
{
    return media_parcel_append(parcel, &val, sizeof(val));
}

int media_parcel_append_uint64(media_parcel *parcel, uint64_t val)
{
    return media_parcel_append(parcel, &val, sizeof(val));
}

int media_parcel_append_int16(media_parcel *parcel, int16_t val)
{
    return media_parcel_append(parcel, &val, sizeof(val));
}

int media_parcel_append_int32(media_parcel *parcel, int32_t val)
{
    return media_parcel_append(parcel, &val, sizeof(val));
}

int media_parcel_append_int64(media_parcel *parcel, int64_t val)
{
    return media_parcel_append(parcel, &val, sizeof(val));
}

int media_parcel_append_float(media_parcel *parcel, float val)
{
    return media_parcel_append(parcel, &val, sizeof(val));
}

int media_parcel_append_double(media_parcel *parcel, double val)
{
    return media_parcel_append(parcel, &val, sizeof(val));
}

int media_parcel_append_string(media_parcel *parcel, const char *str)
{
    return media_parcel_append(parcel, str, strlen(str) + 1);
}

int media_parcel_send(media_parcel *parcel, int fd, uint32_t code)
{
    const uint8_t *buf = (const uint8_t *)parcel->chunk;
    uint32_t len = MEDIA_PARCEL_HEADER_LEN + parcel->chunk->len;

    parcel->chunk->code = code;

    while (len) {
        ssize_t ret = send(fd, buf, len, 0);
        if (ret < 0)
            return -errno;
        buf += ret;
        len -= ret;
    }

    return 0;
}

int media_parcel_recv(media_parcel *parcel, int fd, uint32_t *offset)
{
    uint32_t len = MEDIA_PARCEL_HEADER_LEN + parcel->chunk->len;
    uint32_t tmp = 0;

    if (offset == NULL)
        offset = &tmp;

    while (*offset != len) {
        ssize_t ret = recv(fd,
                           (char *)parcel->chunk + *offset,
                           *offset < MEDIA_PARCEL_HEADER_LEN ?
                                MEDIA_PARCEL_HEADER_LEN - *offset :
                                len - *offset,
                           0);
        if (ret == 0)
            return -EPIPE;

        if (ret < 0)
            return -errno;

        *offset += ret;
        if (*offset == MEDIA_PARCEL_HEADER_LEN) {
            ret = media_parcel_grow(parcel, parcel->chunk->len);
            len = MEDIA_PARCEL_HEADER_LEN + parcel->chunk->len;
            if (ret < 0)
                return ret;
        }
    }

    return 0;
}

uint32_t media_parcel_get_code(media_parcel *parcel)
{
    return parcel->chunk->code;
}

const void *media_parcel_read(media_parcel *parcel, size_t size)
{
    const void *rv = NULL;

    if (parcel->next + size <= parcel->chunk->len) {
        rv = &parcel->chunk->buf[parcel->next];
        parcel->next += size;
    }

    return rv;
}

int media_parcel_read_uint16(media_parcel *parcel, uint16_t *val)
{
    return media_parcel_copy(parcel, val, sizeof(*val));
}

int media_parcel_read_uint32(media_parcel *parcel, uint32_t *val)
{
    return media_parcel_copy(parcel, val, sizeof(*val));
}

int media_parcel_read_uint64(media_parcel *parcel, uint64_t *val)
{
    return media_parcel_copy(parcel, val, sizeof(*val));
}

int media_parcel_read_int16(media_parcel *parcel, int16_t *val)
{
    return media_parcel_copy(parcel, val, sizeof(*val));
}

int media_parcel_read_int32(media_parcel *parcel, int32_t *val)
{
    return media_parcel_copy(parcel, val, sizeof(*val));
}

int media_parcel_read_int64(media_parcel *parcel, int64_t *val)
{
    return media_parcel_copy(parcel, val, sizeof(*val));
}

int media_parcel_read_float(media_parcel *parcel, float *val)
{
    return media_parcel_copy(parcel, val, sizeof(*val));
}

int media_parcel_read_double(media_parcel *parcel, double *val)
{
    return media_parcel_copy(parcel, val, sizeof(*val));
}

const char *media_parcel_read_string(media_parcel *parcel)
{
    const void *beg;
    const void *end;

    beg = &parcel->chunk->buf[parcel->next];
    end = memchr(beg, 0, parcel->chunk->len - parcel->next);
    if (end == NULL)
        return NULL;

    return media_parcel_read(parcel, end - beg + 1);
}
