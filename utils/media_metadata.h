/****************************************************************************
 * frameworks/media/utils/media_metadata.h
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

#ifndef __FRAMEWORKS_MEDIA_UTILS_MEDIA_METADATA_H
#define __FRAMEWORKS_MEDIA_UTILS_MEDIA_METADATA_H

#include <media_event.h>

void media_metadata_init(media_metadata_t* data);
void media_metadata_deinit(media_metadata_t* data);
void media_metadata_update(media_metadata_t* data, media_metadata_t* diff);
int media_metadata_serialize(const media_metadata_t* data, char* base, int len);
int media_metadata_unserialize(media_metadata_t* data, const char* str);

#endif /* __FRAMEWORKS_MEDIA_UTILS_MEDIA_METADATA_H */
