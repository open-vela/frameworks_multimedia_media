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

/**
 * Set criterion with interger value.
 * @param[in] name      criterion name
 * @param[in] value     new integer value
 * @param[in] apply     whether apply configurations
 * @return Zero on success; a negated errno value on failure.
 * @note Should use wrapper functions rather than using this directly.
 */
int media_policy_set_int(const char *name, int value, int apply);

/**
 * Get criterion in interger value.
 * @param[in] name      criterion name
 * @param[out] value    current integer value
 * @return Zero on success; a negated errno value on failure.
 * @note Should use wrapper functions rather than using this directly.
 */
int media_policy_get_int(const char *name, int *value);

/**
 * Set criterion with string value.
 * @param[in] name      criterion name
 * @param[in] value     new string value
 * @param[in] apply     whether apply configurations
 * @return Zero on success; a negated errno value on failure.
 * @note Should use wrapper functions rather than using this directly.
 */
int media_policy_set_string(const char *name, const char *value, int apply);

/**
 * Get criterion in string value.
 * @param[in] name      criterion name
 * @param[out] value    current string value
 * @param[in] len       sizeof value
 * @return Zero on success; a negated errno value on failure.
 * @note Should use wrapper functions rather than using this directly.
 */
int media_policy_get_string(const char *name, char *value, int len);

/**
 * Include(insert) string values to InclusiveCriterion.
 * @param[in] name      criterion name
 * @param[in] values    string values
 * @param[in] apply     whether apply configurations
 * @return Zero on success; a negated errno value on failure.
 * @note Should use wrapper functions rather than using this directly.
 * @warning only for InclusiveCriterion, never call on ExclusiveCriterion.
 */
int media_policy_include(const char *name, const char *values, int apply);

/**
 * Exclude(remove) string values from InclusiveCriterion.
 * @param[in] name      criterion name
 * @param[in] values    string values
 * @param[in] apply     whether apply configurations
 * @return Zero on success; a negated errno value on failure.
 * @note Should use wrapper functions rather than using this directly.
 * @warning only for InclusiveCriterion, never call on ExclusiveCriterion.
 */
int media_policy_exclude(const char *name, const char *values, int apply);

/**
 * Check whether string values included in InclusiveCriterion.
 * @param[in] name      criterion name
 * @param[in] values    string values
 * @param[out] result   whether included or not
 * @return Zero on success; a negated errno value on failure.
 * @note Should use wrapper functions rather than using this directly.
 * @warning only for InclusiveCriterion, never call on ExclusiveCriterion.
 */
int media_policy_contain(const char *name, const char *values, int *result);

/**
 * Increase criterion interger value by 1.
 * @param[in] name      criterion name
 * @param[in] apply     whether apply configurations
 * @return Zero on success; a negated errno value on failure.
 * @note Should use wrapper functions rather than using this directly.
 * @warning only for ExclusiveCriterion, never call on InclusiveCriterion.
 */
int media_policy_increase(const char *name, int apply);

/**
 * Decrease criterion interger value by 1.
 * @param[in] name      criterion name
 * @param[in] apply     whether apply configurations
 * @return Zero on success; a negated errno value on failure.
 * @note Should use wrapper functions rather than using this directly.
 * @warning only for ExclusiveCriterion, never call on InclusiveCriterion.
 */
int media_policy_decrease(const char *name, int apply);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORKS_MEDIA_INCLUDE_MEDIA_POLICY_H */
