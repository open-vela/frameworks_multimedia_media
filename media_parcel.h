/****************************************************************************
 * frameworks/media/meida_parcel.h
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

#ifndef __FRAMEWORKS_MEDIA_MEDIA_PARCEL_H
#define __FRAMEWORKS_MEDIA_MEDIA_PARCEL_H

#include <stdarg.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MEDIA_PARCEL_HEADER_LEN sizeof(uint64_t)
#define MEDIA_PARCEL_DATA_LEN 256

#define MEDIA_PARCEL_SEND 1
#define MEDIA_PARCEL_SEND_ACK 2
#define MEDIA_PARCEL_REPLY 3
#define MEDIA_PARCEL_CREATE_NOTIFY 4
#define MEDIA_PARCEL_NOTIFY 5

typedef struct {
    uint32_t code;
    uint32_t len;
    uint8_t buf[MEDIA_PARCEL_DATA_LEN];
} media_parcel_chunk;

typedef struct media_parcel {
    media_parcel_chunk* chunk;
    media_parcel_chunk prealloc;
    uint32_t next;
    uint32_t cap;
} media_parcel;

void media_parcel_init(media_parcel* parcel);
void media_parcel_deinit(media_parcel* parcel);
void media_parcel_reinit(media_parcel* parcel);

int media_parcel_append(media_parcel* parcel, const void* data, size_t size);
int media_parcel_append_uint8(media_parcel* parcel, uint8_t val);
int media_parcel_append_uint16(media_parcel* parcel, uint16_t val);
int media_parcel_append_uint32(media_parcel* parcel, uint32_t val);
int media_parcel_append_uint64(media_parcel* parcel, uint64_t val);
int media_parcel_append_int8(media_parcel* parcel, int8_t val);
int media_parcel_append_int16(media_parcel* parcel, int16_t val);
int media_parcel_append_int32(media_parcel* parcel, int32_t val);
int media_parcel_append_int64(media_parcel* parcel, int64_t val);
int media_parcel_append_float(media_parcel* parcel, float val);
int media_parcel_append_double(media_parcel* parcel, double val);
int media_parcel_append_string(media_parcel* parcel, const char* str);
int media_parcel_append_printf(media_parcel* parcel, const char* fmt, ...);
int media_parcel_append_vprintf(media_parcel* parcel, const char* fmt, va_list* ap);

int media_parcel_send(media_parcel* parcel, int fd, uint32_t code, int flags);
int media_parcel_recv(media_parcel* parcel, int fd, uint32_t* offset, int flags);

uint32_t media_parcel_get_code(media_parcel* parcel);

const void* media_parcel_read(media_parcel* parcel, size_t size);
int media_parcel_read_uint8(media_parcel* parcel, uint8_t* val);
int media_parcel_read_uint16(media_parcel* parcel, uint16_t* val);
int media_parcel_read_uint32(media_parcel* parcel, uint32_t* val);
int media_parcel_read_uint64(media_parcel* parcel, uint64_t* val);
int media_parcel_read_int8(media_parcel* parcel, int8_t* val);
int media_parcel_read_int16(media_parcel* parcel, int16_t* val);
int media_parcel_read_int32(media_parcel* parcel, int32_t* val);
int media_parcel_read_int64(media_parcel* parcel, int64_t* val);
int media_parcel_read_float(media_parcel* parcel, float* val);
int media_parcel_read_double(media_parcel* parcel, double* val);
const char* media_parcel_read_string(media_parcel* parcel);
int media_parcel_read_scanf(media_parcel* parcel, const char* val, ...);
int media_parcel_read_vscanf(media_parcel* parcel, const char* val, va_list* ap);

#ifdef __cplusplus
}
#endif

#endif
