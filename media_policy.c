/****************************************************************************
 * frameworks/media/media_policy.c
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

#include <bindings/c/ParameterFramework.h>
#include <cutils/properties.h>
#include <libavutil/log.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "media_internal.h"

#ifdef CONFIG_PFW

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MEDIA_PERSIST                   "persist.media."
#define MEDIA_CRITERIA_MAXNUM           64
#define MEDIA_CRITERIA_LINE_MAXLENGTH   256

/****************************************************************************
 * Private Data
 ****************************************************************************/

typedef struct MediaPolicyPriv {
    PfwHandler *pfw;
} MediaPolicyPriv;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/**
 * Save criterion value to kvdb.
 *
 * Criterion has int32_t type, though users can set/get criterion with
 * integer or string, here we just save criterion's integer value.
 *
 * Only for criteria whose real name start with MEDIA_PERSIST.
 *
 * @param pfw           parameter-framework manager instance handle.
 * @param name          criterion name.
 * @return Zero on success; a negated errno value on failure.
 */
static int media_policy_save_kvdb(PfwHandler *pfw, const char *name)
{
    char real_name[64];
    int value;

    if (!pfwGetCriterionRealName(pfw, name, real_name, sizeof(real_name)))
        return -EINVAL;

    if (strncmp(real_name, MEDIA_PERSIST, strlen(MEDIA_PERSIST)))
        return 0;

    if (!pfwGetCriterion(pfw, real_name, &value))
        return -EINVAL;

    return property_set_int32(real_name, value);
}

/**
 * Load criteria values from kvdb.
 *
 * Only for criteria whose real name start with MEDIA_PERSIST.
 *
 * @param criteria  criteria data structure.
 * @param ncriteria criteria number.
 */
static void media_policy_load_kvdb(PfwCriterion *criteria, int ncriteria)
{
    int i;

    for (i = 0; i < ncriteria; i++) {
        const char *real_name = criteria[i].names[0];

        if (strncmp(real_name, MEDIA_PERSIST, strlen(MEDIA_PERSIST)))
            continue;

        criteria[i].initial = property_get_int32(real_name, criteria[i].initial);
    }
}

static void media_policy_free_criteria(PfwCriterion *criteria, int ncriteria)
{
    int i, j;

    // free criteria
    if (criteria) {
        for (i = 0; i < ncriteria; i++) {
            // free criterion names
            if (criteria[i].names) {
                for (j = 0; criteria[i].names[j]; j++)
                    free((void *)criteria[i].names[j]);
                free(criteria[i].names);
            }

            // free criterion values
            if (criteria[i].values) {
                for (j = 0; criteria[i].values[j]; j++)
                    free((void *)criteria[i].values[j]);
                free(criteria[i].values);
            }
        }
        free(criteria);
    }
}

static int media_policy_parse_criteria(PfwCriterion **pcriteria, const char *path)
{
    const char *delim = " \t\r\n";
    char *saveptr;
    FILE *fp;
    PfwCriterion *criteria;
    int ret = -EINVAL;
    int i = 0, j;

    if (!(fp = fopen(path, "r")))
        return -errno;

    // prase criteria definition : calloc + parse
    criteria = calloc(MEDIA_CRITERIA_MAXNUM, sizeof(PfwCriterion));
    if (!criteria)
        goto err_alloc;

    /** one criterion definition each line, example:
      *
      * text to be parsed:
      *  ExclusiveCriterion Color Colour : Red Grean Blue : 1
      *  InclusiveCriterion Alphabet     : A B C D E F G
      *
      * after parsing:
      * {
      *     {
      *         .names     = { "Color", "Colour", NULL },
      *         .inclusive = false,
      *         .values    = { "Red", "Grean", "Blue", NULL },
      *         .initial   = 1,
      *     },
      *     {
      *         .names     = { "Alphabet", NULL },
      *         .inclusive = true,
      *         .values    = { "A", "B", "C", "D", "E", "F", "G", NULL },
      *         .initial   = 0,
      *     },
      * }
      *
      */
    for (; !feof(fp) && i < MEDIA_CRITERIA_MAXNUM; i++) {
        char line[MEDIA_CRITERIA_LINE_MAXLENGTH];
        char *token;

        if (!fgets(line, sizeof(line), fp)) {
            i--; // ignore empty line
            continue;
        }

        // parse criterion type
        if (!(token = strtok_r(line, delim, &saveptr)))
            goto err_parse;

        if (!strcmp("InclusiveCriterion", token))
            criteria[i].inclusive = true;
        else if (!strcmp("ExclusiveCriterion", token))
            criteria[i].inclusive = false;
        else
            goto err_parse;

        // parse criterion names : calloc + parse
        criteria[i].names = calloc(MEDIA_CRITERIA_MAXNUM + 1, sizeof(char *));
        if (!criteria[i].names)
            goto err_alloc;

        for (j = 0; j < MEDIA_CRITERIA_MAXNUM; j++) {
            if (!(token = strtok_r(NULL, delim, &saveptr)))
                goto err_parse;
            if (!strcmp(":", token)) {
                if (j == 0)
                    goto err_parse;
                break; // end of names definition
            }
            if (!(token = strdup(token)))
                goto err_alloc;
            criteria[i].names[j] = token;
        }

        // parse criterion values : calloc + parse
        criteria[i].values = calloc(MEDIA_CRITERIA_MAXNUM + 1, sizeof(char *));
        if (!criteria[i].values)
            goto err_alloc;

        for (j = 0; j < MEDIA_CRITERIA_MAXNUM; j++) {
            if (!(token = strtok_r(NULL, delim, &saveptr))) {
                if (j == 0)
                    goto err_parse;
                break; // end of values definition
            }
            if (!strcmp(":", token)) {
                if (j == 0)
                    goto err_parse;
                if (!(token = strtok_r(NULL, delim, &saveptr)))
                    goto err_parse;
                criteria[i].initial = atoi(token);
                break; // end of initial value definition
            }
            if (!(token = strdup(token)))
                goto err_alloc;
            criteria[i].values[j] = token;
        }
    }

    fclose(fp);

    *pcriteria = criteria;
    return i; //< ncriteria

err_alloc:
    ret = -ENOMEM;
err_parse:
    media_policy_free_criteria(criteria, i + 1); // failed on (i + 1)'th.
    fclose(fp);

    return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int media_policy_control(void *handle, const char *name, const char *cmd,
                         const char *value, int apply, char **res, int res_len)
{
    MediaPolicyPriv *priv = handle;
    int ret = -EINVAL, tmp;

    if (!priv || !priv->pfw)
        return -EINVAL;

    if (res_len && res && !*res) {
        *res = malloc(res_len);
        if (!*res)
            return -ENOMEM;
    }

    if (!strcmp(cmd, "set_int")) {
        ret = pfwSetCriterion(priv->pfw, name, atoi(value));
    } else if (!strcmp(cmd, "set_string")) {
        ret = pfwSetCriterionString(priv->pfw, name, value);
    } else if (!strcmp(cmd, "include")) {
        ret = pfwIncludeCriterion(priv->pfw, name, value);
    } else if (!strcmp(cmd, "exclude")) {
        ret = pfwExcludeCriterion(priv->pfw, name, value);
    } else if (!strcmp(cmd, "increase")) {
        if (!pfwGetCriterion(priv->pfw, name, &tmp))
            return -EINVAL;

        ret = pfwSetCriterion(priv->pfw, name, tmp + 1);
    } else if (!strcmp(cmd, "decrease")) {
        if (!pfwGetCriterion(priv->pfw, name, &tmp))
            return -EINVAL;

        ret = pfwSetCriterion(priv->pfw, name, tmp - 1);
    } else if (!strcmp(cmd, "get_int")) {
        if (!res || !*res || !pfwGetCriterion(priv->pfw, name, &tmp))
            return -EINVAL;

        return snprintf(*res, res_len, "%d", tmp);
    } else if (!strcmp(cmd, "get_string")) {
        if (!res || !*res)
            return -EINVAL;

        return pfwGetCriterionString(priv->pfw, name, *res, res_len) ? 0 : -EINVAL;
    } else if (!strcmp(cmd, "dump")) {
        if (!res)
            return -EINVAL;

        *res = pfwDump(priv->pfw, value);
        return 0;
    }

    if (!ret) {
        av_log(NULL, AV_LOG_ERROR, "%s, unkown name:%s cmd:%s value:%s\n",
               __func__, name, cmd, value);
        return -EINVAL;
    }

    if (apply && !pfwApplyConfigurations(priv->pfw))
        return -EINVAL;

    return media_policy_save_kvdb(priv->pfw, name);
}

int media_policy_destroy(void *handle)
{
    MediaPolicyPriv *priv = handle;

    if (!priv)
        return -EINVAL;

    if (priv->pfw)
        pfwDestroy(priv->pfw);
    free(priv);

    return 0;
}

void *media_policy_create(void *files)
{
    const char **file_paths = files;
    MediaPolicyPriv *priv;
    PfwCriterion *criteria;
    PfwLogger logger = {};
    int ncriteria;

    if (!files)
        return NULL;

    priv = calloc(1, sizeof(MediaPolicyPriv));
    if (!priv)
        return NULL;

    // parse criteria.
    ncriteria = media_policy_parse_criteria(&criteria, file_paths[1]);
    if (ncriteria <= 0)
        goto err_parse;

    // load persist criteria values from kvdb.
    media_policy_load_kvdb(criteria, ncriteria);

    // create and start parameter-framework manager.
    if (!(priv->pfw = pfwCreate()))
        goto err_pfw;
    if (!pfwStart(priv->pfw, file_paths[0], criteria, ncriteria, &logger))
        goto err_pfw;

    media_policy_free_criteria(criteria, ncriteria);
    return priv;

err_pfw:
    media_policy_free_criteria(criteria, ncriteria);
err_parse:
    media_policy_destroy(priv);
    return NULL;
}

int media_policy_get_stream_name(void *handle, const char *stream,
                                 char *name, int len)
{
    return media_policy_control(handle, stream, "get_string", NULL, 0,
                                &name, len);
}

int media_policy_set_stream_status(void *handle, const char *name,
                                   const char *stream, bool active)
{
    return media_policy_control(handle, name, active ? "include" : "exclude",
                                stream, 0, NULL, 0);
}

#else // CONFIG_PFW

/****************************************************************************
 * Dummy Public Functions
 ****************************************************************************/

void *media_policy_create(void *file)
{
    return (void *)1;
}

int media_policy_destroy(void *handle)
{
    return 0;
}

int media_policy_control(void *handle, const char *name, const char *cmd,
                         const char *value, int apply, char **res, int res_len)
{
    return -ENOSYS;
}

int media_policy_get_stream_name(void *handle, const char *stream,
                                 char *name, int len)
{
    return -ENOSYS;
}

int media_policy_set_stream_status(void *handle, const char *name,
                                   const char *input, bool active)
{
    return -ENOSYS;
}

#endif
