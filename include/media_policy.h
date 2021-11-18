/****************************************************************************
 * frameworks/media/include/med_policy.h
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

#ifndef FRAMEWORKS_MEDIA_INCLUDE_MEDIA_POLICY_H
#define FRAMEWORKS_MEDIA_INCLUDE_MEDIA_POLICY_H

#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: media_policy_set_int
 *
 * Description:
 *   Set criterion with interger value.
 *   You should use wrapper functions rather than using this funtion directly.
 *
 * Input Parameters:
 *   name   - criterion name
 *   value  - new value
 *   apply  - whether apply configurations
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_set_int(const char *name, int value, int apply);

/****************************************************************************
 * Name: media_policy_get_int
 *
 * Description:
 *   Get criterion in interger value.
 *   You should use wrapper functions rather than using this funtion directly.
 *
 * Input Parameters:
 *   name   - criterion name
 *   value  - address to return current value
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_get_int(const char *name, int *value);

/****************************************************************************
 * Name: media_policy_set_string
 *
 * Description:
 *   Get criterion with string value.
 *   You should use wrapper functions rather than using this funtion directly.
 *
 * Input Parameters:
 *   name   - criterion name
 *   value  - new value
 *   apply  - whether apply configurations
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_set_string(const char *name, const char *value, int apply);

/****************************************************************************
 * Name: media_policy_get_string
 *
 * Description:
 *   Get criterion in string value.
 *   You should use wrapper functions rather than using this funtion directly.
 *
 * Input Parameters:
 *   name   - criterion name
 *   value  - address to return current value
 *   len    - size of value
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_get_string(const char *name, char *value, int len);

/****************************************************************************
 * Name: media_policy_include
 *
 * Description:
 *   Include(insert) inclusive criterion values.
 *   You should use wrapper functions rather than using this funtion directly.
 *
 * Input Parameters:
 *   name    - inclusive criterion name
 *   values  - new values
 *   apply   - whether apply configurations
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_include(const char *name, const char *values, int apply);

/****************************************************************************
 * Name: media_policy_exclude
 *
 * Description:
 *   Exclude(remove) inclusive criterion values.
 *   You should use wrapper functions rather than using this funtion directly.
 *
 * Input Parameters:
 *   name    - inclusive criterion name
 *   values  - new values
 *   apply   - whether apply configurations
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_exclude(const char *name, const char *values, int apply);

/****************************************************************************
 * Name: media_policy_increase
 *
 * Description:
 *   Increase criterion value.
 *   You should use wrapper functions rather than using this funtion directly.
 *
 * Input Parameters:
 *   name   - criterion name
 *   apply  - whether apply configurations
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_increase(const char *name, int apply);

/****************************************************************************
 * Name: media_policy_decrease
 *
 * Description:
 *   Decrease criterion value.
 *   You should use wrapper functions rather than using this funtion directly.
 *
 * Input Parameters:
 *   name   - criterion name
 *   apply  - whether apply configurations
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int media_policy_decrease(const char *name, int apply);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_POLICY_H */
