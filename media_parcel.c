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

static int media_parcel_grow(media_parcel *parcel, size_t cursz, size_t newsz)
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
                   MEDIA_PARCEL_HEADER_LEN + cursz);
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

    if ((rv = media_parcel_grow(parcel, parcel->chunk->len, size)) < 0)
        return rv;

    memcpy(&parcel->chunk->buf[parcel->chunk->len], data, size);
    parcel->chunk->len += size;

    return 0;
}

int media_parcel_append_uint8(media_parcel *parcel, uint8_t val)
{
    return media_parcel_append(parcel, &val, sizeof(val));
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

int media_parcel_append_int8(media_parcel *parcel, int8_t val)
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
    if (!str)
        str = "";

    return media_parcel_append(parcel, str, strlen(str) + 1);
}

int media_parcel_append_vprintf(media_parcel *parcel, const char *fmt, va_list *ap)
{
    const char *val_s;
    int64_t val_i64;
    int32_t val_i32;
    int16_t val_i16;
    int8_t val_i8, c;
    double val_dbl;
    float val_flt;
    int ret = 0;

    while ((c = *fmt++)) {
        if (c == '%')
            continue;

        switch (c) {
            case 'l':
                val_i64 = va_arg(*ap, int64_t);
                ret = media_parcel_append_int64(parcel, val_i64);
                break;
            case 'i':
                val_i32 = va_arg(*ap, int32_t);
                ret = media_parcel_append_int32(parcel, val_i32);
                break;
            case 'h':
                val_i16 = va_arg(*ap, int32_t);
                ret = media_parcel_append_int16(parcel, val_i16);
                break;
            case 'c':
                val_i8 = va_arg(*ap, int32_t);
                ret = media_parcel_append_int8(parcel, val_i8);
                break;
            case 'd':
                val_dbl = va_arg(*ap, double);
                ret = media_parcel_append_double(parcel, val_dbl);
                break;
            case 'f':
                val_flt = va_arg(*ap, double);
                ret = media_parcel_append_float(parcel, val_flt);
                break;
            case 's':
                val_s = va_arg(*ap, const char *);
                ret = media_parcel_append_string(parcel, val_s);
                break;
            default:
                break;
        }

        if (ret < 0)
            break;
    }

    return ret;
}

int media_parcel_append_printf(media_parcel *parcel, const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = media_parcel_append_vprintf(parcel, fmt, &ap);
    va_end(ap);

    return ret;
}

int media_parcel_send(media_parcel *parcel, int fd, uint32_t code, int flags)
{
    const uint8_t *buf = (const uint8_t *)parcel->chunk;
    uint32_t len = MEDIA_PARCEL_HEADER_LEN + parcel->chunk->len;

    parcel->chunk->code = code;

    while (len) {
        ssize_t ret = send(fd, buf, len, flags);
        if (ret < 0)
            return -errno;
        buf += ret;
        len -= ret;
    }

    return 0;
}

int media_parcel_recv(media_parcel *parcel, int fd, uint32_t *offset, int flags)
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
                           flags);
        if (ret == 0)
            return -EPIPE;

        if (ret < 0)
            return -errno;

        *offset += ret;
        if (*offset == MEDIA_PARCEL_HEADER_LEN) {
            ret = media_parcel_grow(parcel, 0, parcel->chunk->len);
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

int media_parcel_read_uint8(media_parcel *parcel, uint8_t *val)
{
    return media_parcel_copy(parcel, val, sizeof(*val));
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

int media_parcel_read_int8(media_parcel *parcel, int8_t *val)
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
    const char *ret;

    beg = &parcel->chunk->buf[parcel->next];
    end = memchr(beg, 0, parcel->chunk->len - parcel->next);
    if (end == NULL)
        return NULL;

    ret = media_parcel_read(parcel, end - beg + 1);
    if (ret && ret[0] == '\0')
        return NULL;

    return ret;
}

int media_parcel_read_vscanf(media_parcel *parcel, const char *fmt, va_list *ap)
{
    const char **p_s;
    int64_t *p_i64;
    int32_t *p_i32;
    int16_t *p_i16;
    int8_t *p_i8;
    double *p_dbl;
    float *p_flt;
    int ret = 0;
    char c;

    while ((c = *fmt++)) {
        if (c == '%')
            continue;

        switch (c) {
            case 'l':
                p_i64 = va_arg(*ap, int64_t *);
                ret = media_parcel_read_int64(parcel, p_i64);
                break;
            case 'i':
                p_i32 = va_arg(*ap, int32_t *);
                ret = media_parcel_read_int32(parcel, p_i32);
                break;
            case 'h':
                p_i16 = va_arg(*ap, int16_t *);
                ret = media_parcel_read_int16(parcel, p_i16);
                break;
            case 'c':
                p_i8 = va_arg(*ap, int8_t *);
                ret = media_parcel_read_int8(parcel, p_i8);
                break;
            case 'd':
                p_dbl = va_arg(*ap, double *);
                ret = media_parcel_read_double(parcel, p_dbl);
                break;
            case 'f':
                p_flt = va_arg(*ap, float *);
                ret = media_parcel_read_float(parcel, p_flt);
                break;
            case 's':
                p_s = va_arg(*ap, const char **);
                *p_s = media_parcel_read_string(parcel);
                break;
            default:
                break;
        }

        if (ret < 0)
            break;
    }

    return ret;
}

int media_parcel_read_scanf(media_parcel *parcel, const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = media_parcel_read_vscanf(parcel, fmt, &ap);
    va_end(ap);

    return ret;
}

