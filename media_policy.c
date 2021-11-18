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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "media_internal.h"

#ifdef CONFIG_PFW

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MEDIA_CRITERIA_MAXNUM           64
#define MEDIA_CRITERIA_LINE_MAXLENGTH   256

/****************************************************************************
 * Private Data
 ****************************************************************************/

typedef struct MediaPolicyPriv {
    PfwHandler   *pfw;
    PfwCriterion *criteria;
    int           ncriteria;
} MediaPolicyPriv;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void media_policy_free_criteria(MediaPolicyPriv *priv)
{
    int i, j;

    // free criteria
    if (priv->criteria) {
        for (i = 0; i < priv->ncriteria; i++) {
            // free criterion names
            if (priv->criteria[i].names) {
                for (j = 0; priv->criteria[i].names[j]; j++)
                    free(priv->criteria[i].names[j]);
                free(priv->criteria[i].names);
            }

            // free criterion values
            if (priv->criteria[i].values) {
                for (j = 0; priv->criteria[i].values[j]; j++)
                    free(priv->criteria[i].values[j]);
                free(priv->criteria[i].values);
            }
        }
        free(priv->criteria);
        priv->criteria = NULL;
        priv->ncriteria = 0;
    }
}

static int media_policy_parse_criteria(MediaPolicyPriv *priv, const char *path)
{
    const char *delim = " \t\r\n";
    char *saveptr;
    void *addr;
    FILE *fp;
    int ret = -EINVAL;
    int i, j;

    if (!(fp = fopen(path, "r")))
        return -errno;

    // prase criteria definition : calloc + parse + realloc
    priv->criteria = calloc(MEDIA_CRITERIA_MAXNUM, sizeof(PfwCriterion));
    if (!priv->criteria)
        goto err_alloc;

    /** one criterion definition each line
      *
      * example:
      *  ExclusiveCriterion Color Colour : Red Grean Blue
      *  InclusiveCriterion Alphabet     : A B C D E F G
      */
    for (i = 0; !feof(fp) && i < MEDIA_CRITERIA_MAXNUM; i++) {
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
            priv->criteria[i].inclusive = true;
        else if (!strcmp("ExclusiveCriterion", token))
            priv->criteria[i].inclusive = false;
        else
            goto err_parse;

        // parse criterion names : calloc + parse + realloc
        priv->criteria[i].names = calloc(MEDIA_CRITERIA_MAXNUM + 1, sizeof(char *));
        if (!priv->criteria[i].names)
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
            priv->criteria[i].names[j] = token;
        }

        addr = realloc(priv->criteria[i].names, (j + 1) * sizeof(char *));
        if (addr)
            priv->criteria[i].names = addr;
        else
            goto err_alloc;

        // parse criterion values : calloc + parse + realloc
        priv->criteria[i].values = calloc(MEDIA_CRITERIA_MAXNUM + 1, sizeof(char *));
        if (!priv->criteria[i].values)
            goto err_alloc;

        for (j = 0; j < MEDIA_CRITERIA_MAXNUM; j++) {
            if (!(token = strtok_r(NULL, delim, &saveptr))) {
                if (j == 0)
                    goto err_parse;
                break; // end of values definition
            }
            if (!(token = strdup(token)))
                goto err_alloc;
            priv->criteria[i].values[j] = token;
        }

        addr = realloc(priv->criteria[i].values, (j + 1) * sizeof(char *));
        if (addr)
            priv->criteria[i].values = addr;
        else
            goto err_alloc;
    }

    addr = realloc(priv->criteria, i * sizeof(PfwCriterion));
    if (addr)
        priv->criteria = addr;
    else
        goto err_alloc;

    priv->ncriteria = i;
    fclose(fp);
    return 0;

err_alloc:
    ret = -ENOMEM;
err_parse:
    priv->ncriteria = i + 1; // parsing failed on (i + 1)'th criterion.
    media_policy_free_criteria(priv);
    fclose(fp);

    return ret;
}

/****************************************************************************
 * Public Functions for RPC
 ****************************************************************************/

int media_policy_set_int_(const char *name, int value, int apply)
{
    MediaPolicyPriv *priv = media_get_policy();

    if (!priv || !priv->pfw || !name)
        return -EINVAL;

    if (!pfwSetCriterion(priv->pfw, name, value))
        return -EINVAL;
    if (apply && !pfwApplyConfigurations(priv->pfw))
        return -EINVAL;

    return 0;
}

int media_policy_get_int_(const char *name, int *value)
{
    MediaPolicyPriv *priv = media_get_policy();

    if (!priv || !priv->pfw || !name || !value)
        return -EINVAL;

    return pfwGetCriterion(priv->pfw, name, value) ? 0 : -EINVAL;
}

int media_policy_set_string_(const char *name, const char *value, int apply)
{
    MediaPolicyPriv *priv = media_get_policy();

    if (!priv || !priv->pfw || !name)
        return -EINVAL;

    if (!pfwSetStringCriterion(priv->pfw, name, value))
        return -EINVAL;
    if (apply && !pfwApplyConfigurations(priv->pfw))
        return -EINVAL;

    return 0;
}

int media_policy_get_string_(const char *name, char *value, size_t len)
{
    MediaPolicyPriv *priv = media_get_policy();

    if (!priv || !priv->pfw || !name || !value)
        return -EINVAL;

    return pfwGetStringCriterion(priv->pfw, name, value, len) ? 0 : -EINVAL;
}

int media_policy_include_(const char *name, const char *values, int apply)
{
    MediaPolicyPriv *priv = media_get_policy();

    if (!priv || !priv->pfw || !name)
        return -EINVAL;

    if (!pfwIncludeStringCriterion(priv->pfw, name, values))
        return -EINVAL;
    if (apply && !pfwApplyConfigurations(priv->pfw))
        return -EINVAL;

    return 0;
}

int media_policy_exclude_(const char *name, const char *values, int apply)
{
    MediaPolicyPriv *priv = media_get_policy();

    if (!priv || !priv->pfw || !name)
        return -EINVAL;

    if (!pfwExcludeStringCriterion(priv->pfw, name, values))
        return -EINVAL;
    if (apply && !pfwApplyConfigurations(priv->pfw))
        return -EINVAL;

    return 0;
}

int media_policy_increase_(const char *name, int apply)
{
    MediaPolicyPriv *priv = media_get_policy();
    int value;

    if (!priv || !priv->pfw || !name)
        return -EINVAL;

    if (!pfwGetCriterion(priv->pfw, name, &value))
        return -EINVAL;
    if (!pfwSetCriterion(priv->pfw, name, value + 1))
        return -EINVAL;
    if (apply && !pfwApplyConfigurations(priv->pfw))
        return -EINVAL;

    return 0;
}

int media_policy_decrease_(const char *name, int apply)
{
    MediaPolicyPriv *priv = media_get_policy();
    int value;

    if (!priv || !priv->pfw || !name)
        return -EINVAL;

    if (!pfwGetCriterion(priv->pfw, name, &value))
        return -EINVAL;
    if (!pfwSetCriterion(priv->pfw, name, value - 1))
        return -EINVAL;
    if (apply && !pfwApplyConfigurations(priv->pfw))
        return -EINVAL;

    return 0;
}

/****************************************************************************
 * Public Functions for daemon
 ****************************************************************************/

int media_policy_destroy(void *handle)
{
    MediaPolicyPriv *priv = handle;

    if (!priv)
        return -EINVAL;

    media_policy_free_criteria(priv);
    if (priv->pfw)
        pfwDestroy(priv->pfw);
    free(priv);

    return 0;
}

void *media_policy_create(void *files)
{
    const char **file_paths = files;
    MediaPolicyPriv *priv;

    if (!files)
        return NULL;

    priv = calloc(1, sizeof(MediaPolicyPriv));
    if (!priv)
        return NULL;

    if (media_policy_parse_criteria(priv, file_paths[1]) < 0)
        goto err;
    if (!(priv->pfw = pfwCreate()))
        goto err;
    if (!pfwStart(priv->pfw, file_paths[0], priv->criteria, priv->ncriteria, NULL))
        goto err;

    return priv;
err:
    media_policy_destroy(priv);
    return NULL;
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

int media_policy_set_int_(const char *name, int value, int apply)
{
    return 0;
}

int media_policy_get_int_(const char *name, int *value)
{
    return 0;
}

int media_policy_set_string_(const char *name, const char *value, int apply)
{
    return 0;
}

int media_policy_get_string_(const char *name, char *value, size_t len)
{
    return 0;
}

int media_policy_include_(const char *name, const char *values, int apply)
{
    return 0;
}

int media_policy_exclude_(const char *name, const char *values, int apply)
{
    return 0;
}

int media_policy_increase_(const char *name, int apply)
{
    return 0;
}

int media_policy_decrease_(const char *name, int apply)
{
    return 0;
}

#endif
