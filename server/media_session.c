/****************************************************************************
 * frameworks/media/server/media_session.c
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
 * Session Architecture
 *
 * +---------+
 * | session |
 * +---------+    "controllee -> controller : backward event notification"
 *      |                                   |
 *      |    +-----------------+    +--------------+    +-----------+
 *      |--> | controllee list | -> | music player | -> | bt player | -> ...
 *      |    +-----------------+    +--------------+    +-----------+
 *      |   "register from client"          |
 *      |                                   v backward: broadcast.
 *      |                               ^ forward: only the most active one.
 *      |                               |
 *      |    +-----------------+    +-------+    +------------------+
 *      |--> | controller list | -> | avrcp | -> | system music bar | -> ...
 *           +-----------------+    +-------+    +------------------+
 *            "open from client"        |
 *            "controller -> controllee : forward control message"
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/queue.h>

#include "media_metadata.h"
#include "media_server.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef TAILQ_HEAD(MediaControllerList, MediaControllerPriv) MediaControllerList;
typedef TAILQ_HEAD(MediaControlleeList, MediaControlleePriv) MediaControlleeList;
typedef TAILQ_ENTRY(MediaControllerPriv) MediaControllerEntry;
typedef TAILQ_ENTRY(MediaControlleePriv) MediaControlleeEntry;

typedef struct MediaControllerPriv {
    MediaControllerEntry entry;
    void* cookie;
    bool event; /* Indicates whether event is needed. */
} MediaControllerPriv;

typedef struct MediaControlleePriv {
    MediaControlleeEntry entry;
    void* cookie;
    media_metadata_t data;
} MediaControlleePriv;

typedef struct MediaSessionPriv {
    MediaControllerList controllers;
    MediaControlleeList controllees;
} MediaSessionPriv;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* TODO: remove after direct passing event between client&server. */
static int media_session_cmd2event(const char* cmd);

static void* media_session_controller_open(MediaSessionPriv* priv, void* cookie);
static int media_session_controller_transfer(MediaSessionPriv* priv,
    const char* cmd, const char* arg, char* res, int len);
static void media_session_controller_close(MediaSessionPriv* priv,
    MediaControllerPriv* controller);

void* media_session_controllee_register(MediaSessionPriv* priv, void* cookie);
void media_session_controllee_notify(MediaSessionPriv* priv,
    MediaControlleePriv* controllee, int event, int result, const char* extra);
void media_session_controllee_update(MediaSessionPriv* priv,
    MediaControlleePriv* controllee, const char* arg);
void media_session_controllee_unregister(MediaSessionPriv* priv,
    MediaControlleePriv* controllee);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int media_session_cmd2event(const char* cmd)
{
    if (!strcmp(cmd, "start"))
        return MEDIA_EVENT_START;
    else if (!strcmp(cmd, "pause"))
        return MEDIA_EVENT_PAUSE;
    else if (!strcmp(cmd, "stop"))
        return MEDIA_EVENT_STOP;
    else if (!strcmp(cmd, "prev"))
        return MEDIA_EVENT_PREV_SONG;
    else if (!strcmp(cmd, "next"))
        return MEDIA_EVENT_NEXT_SONG;
    else if (!strcmp(cmd, "volumeup"))
        return MEDIA_EVENT_INCREASE_VOLUME;
    else if (!strcmp(cmd, "volumedown"))
        return MEDIA_EVENT_DECREASE_VOLUME;
    else
        return -ENOSYS;
}

static void* media_session_controller_open(MediaSessionPriv* priv, void* cookie)
{
    MediaControllerPriv* controller;

    controller = zalloc(sizeof(MediaControllerPriv));
    if (!controller)
        return NULL;

    controller->cookie = cookie;
    TAILQ_INSERT_HEAD(&priv->controllers, controller, entry);
    return controller;
}

static int media_session_controller_transfer(MediaSessionPriv* priv,
    const char* cmd, const char* arg, char* res, int len)
{
    MediaControlleePriv* controllee = NULL;
    int ret;

    /* Find the most active controllee. */
    controllee = TAILQ_FIRST(&priv->controllees);
    if (!controllee)
        return -ENOENT;

    /* Directly return metadata if query sth. */
    if (!strcmp(cmd, "query"))
        return media_metadata_serialize(&controllee->data, res, len);

    /* Transfer control-message to controllee client. */
    ret = media_session_cmd2event(cmd);
    if (ret > 0) {
        media_stub_notify_event(controllee->cookie, ret, 0, arg);
        ret = 0;
    }

    return ret;
}

static void media_session_controller_close(MediaSessionPriv* priv,
    MediaControllerPriv* controller)
{
    TAILQ_REMOVE(&priv->controllers, controller, entry);
    free(controller);
}

void* media_session_controllee_register(MediaSessionPriv* priv, void* cookie)
{
    MediaControlleePriv* controllee;

    controllee = zalloc(sizeof(MediaControlleePriv));
    if (!controllee)
        return NULL;

    controllee->cookie = cookie;
    media_metadata_init(&controllee->data);
    TAILQ_INSERT_TAIL(&priv->controllees, controllee, entry);

    /* Would notify changed event only if there are no other controllees. */
    media_session_controllee_notify(priv, controllee, MEDIA_EVENT_CHANGED, 0, NULL);
    return controllee;
}

void media_session_controllee_notify(MediaSessionPriv* priv,
    MediaControlleePriv* controllee, int event, int result, const char* extra)
{
    MediaControllerPriv* controller;

    /* Only most active controllee can broadcast notification to all controllers. */
    if (controllee == TAILQ_FIRST(&priv->controllees)) {
        TAILQ_FOREACH(controller, &priv->controllers, entry)
        {
            if (controller->event)
                media_stub_notify_event(controller->cookie, event, result, extra);
        }
    }
}

void media_session_controllee_update(MediaSessionPriv* priv,
    MediaControlleePriv* controllee, const char* arg)
{
    media_metadata_t diff;
    int diff_flags;

    media_metadata_init(&diff);
    media_metadata_unserialize(&diff, arg);
    diff_flags = diff.flags;
    media_metadata_update(&controllee->data, &diff);

    /* Lastest playing controlee becomes the most active one. */
    if (controllee == TAILQ_FIRST(&priv->controllees)) {
        media_session_controllee_notify(priv, controllee,
            MEDIA_EVENT_UPDATED, diff_flags, NULL);
    } else if ((controllee->data.flags & MEDIA_METAFLAG_STATE)
        && controllee->data.state > 0) {
        TAILQ_REMOVE(&priv->controllees, controllee, entry);
        TAILQ_INSERT_HEAD(&priv->controllees, controllee, entry);
        media_session_controllee_notify(priv, controllee,
            MEDIA_EVENT_CHANGED, diff_flags, NULL);
    }
}

void media_session_controllee_unregister(MediaSessionPriv* priv,
    MediaControlleePriv* controllee)
{
    bool changed = controllee == TAILQ_FIRST(&priv->controllees);
    int flags = 0;

    media_metadata_deinit(&controllee->data);
    TAILQ_REMOVE(&priv->controllees, controllee, entry);
    free(controllee);

    /* Notify changed event if most active controlee was unregistered. */
    if (changed) {
        controllee = TAILQ_FIRST(&priv->controllees);
        if (controllee)
            flags = controllee->data.flags;

        media_session_controllee_notify(priv, controllee,
            MEDIA_EVENT_CHANGED, flags, NULL);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void* media_session_create(void* param)
{
    MediaSessionPriv* priv;

    priv = zalloc(sizeof(MediaSessionPriv));
    if (!priv)
        return NULL;

    TAILQ_INIT(&priv->controllers);
    TAILQ_INIT(&priv->controllees);
    return priv;
}

int media_session_destroy(void* session)
{
    MediaSessionPriv* priv = session;

    free(priv);
    return 0;
}

int media_session_handler(void* session, void* cookie, const char* target,
    const char* cmd, const char* arg, char* res, int res_len)
{
    MediaControllerPriv* controller = media_server_get_data(cookie);
    MediaControlleePriv* controllee = media_server_get_data(cookie);
    MediaSessionPriv* priv = session;
    int event, result;

    /* Controllee methods. */
    if (!strcmp(cmd, "ping"))
        return 0;

    if (!strcmp(cmd, "register")) {
        controllee = media_session_controllee_register(priv, cookie);
        if (!controllee)
            return -ENOMEM;

        media_server_set_data(cookie, controllee);
        return 0;
    }

    if (controllee) {
        if (!strcmp(cmd, "unregister")) {
            media_stub_notify_finalize(&controllee->cookie);
            media_session_controllee_unregister(priv, controllee);
            return 0;
        }

        if (!strcmp(cmd, "event")) {
            sscanf(arg, "%d:%d", &event, &result);
            media_session_controllee_notify(priv, controllee, event, result, target);
            return 0;
        }

        if (!strcmp(cmd, "update")) {
            media_session_controllee_update(priv, controllee, arg);
            return 0;
        }
    }

    /* Controller methods. */
    if (!strcmp(cmd, "open")) {
        controller = media_session_controller_open(priv, cookie);
        if (!controller)
            return -ENOMEM;

        media_server_set_data(cookie, controller);
        return 0;
    }

    if (controller) {
        if (!strcmp(cmd, "close")) {
            media_stub_notify_finalize(&controller->cookie);
            media_session_controller_close(priv, controller);
            return 0;
        }

        if (!strcmp(cmd, "set_event")) {
            controller->event = true;
            return 0;
        }

        return media_session_controller_transfer(priv, cmd, arg, res, res_len);
    }

    return -EINVAL;
}
