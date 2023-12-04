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
 * TODO: so far we only support register controllee from local server,
 *  might need to support register from remote server in the future.
 ****************************************************************************/

typedef LIST_HEAD(MediaControllerList, MediaControllerPriv) MediaControllerList;
typedef LIST_HEAD(MediaControlleeList, MediaControlleePriv) MediaControlleeList;
typedef LIST_ENTRY(MediaControllerPriv) MediaControllerEntry;
typedef LIST_ENTRY(MediaControlleePriv) MediaControlleeEntry;

typedef struct MediaControllerPriv {
    MediaControllerEntry entry;
    void* cookie;
    bool event;
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

static int media_session_cmd2event(const char* cmd);

static void* media_session_controller_open(MediaSessionPriv* priv, void* cookie);
static void media_session_controller_close(MediaControllerPriv* controller);
static int media_session_controller_transfer(MediaSessionPriv* priv,
    const char* cmd, const char* arg, char* res, int len);

void* media_session_controllee_register(MediaSessionPriv* priv, void* cookie);
void media_session_controllee_notify(MediaSessionPriv* priv,
    MediaControlleePriv* controllee, int event, int result, const char* extra);
void media_session_controllee_update(MediaSessionPriv* priv,
    MediaControlleePriv* controllee, const char* arg);
void media_session_controllee_unregister(MediaControlleePriv* controllee);

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
        return MEDIA_EVENT_PREV;
    else if (!strcmp(cmd, "next"))
        return MEDIA_EVENT_NEXT;
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
    LIST_INSERT_HEAD(&priv->controllers, controller, entry);

    return controller;
}

static void media_session_controller_close(MediaControllerPriv* controller)
{
    LIST_REMOVE(controller, entry);
    free(controller);
}

static int media_session_controller_transfer(MediaSessionPriv* priv,
    const char* cmd, const char* arg, char* res, int len)
{
    MediaControlleePriv* controllee = NULL;
    int ret;

    /* Find the most active controllee. */
    controllee = LIST_FIRST(&priv->controllees);
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

void* media_session_controllee_register(MediaSessionPriv* priv, void* cookie)
{
    MediaControlleePriv* controllee;

    controllee = zalloc(sizeof(MediaControlleePriv));
    if (!controllee)
        return NULL;

    controllee->cookie = cookie;
    LIST_INSERT_HEAD(&priv->controllees, controllee, entry);
    media_metadata_init(&controllee->data);
    return controllee;
}

void media_session_controllee_notify(MediaSessionPriv* priv,
    MediaControlleePriv* controllee, int event, int result, const char* extra)
{
    MediaControllerPriv* controller;
    bool most_active;

    /* Only the most active controllee can notify. */
    if (event == MEDIA_EVENT_STARTED && result == 0) {
        most_active = true;
        LIST_REMOVE(controllee, entry);
        LIST_INSERT_HEAD(&priv->controllees, controllee, entry);
    } else if (controllee == LIST_FIRST(&priv->controllees)) {
        most_active = true;
    } else {
        most_active = false;
    }

    /* Broadcast notification to all controllers that cares. */
    if (most_active) {
        LIST_FOREACH(controller, &priv->controllers, entry)
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

    media_metadata_init(&diff);
    media_metadata_unserialize(&diff, arg);

    /* Notify state change to controller. */
    if ((diff.flags & MEDIA_METAFLAG_STATE) && diff.state != controllee->data.state)
        media_session_controllee_notify(priv, controllee, diff.state, 0, NULL);

    media_metadata_update(&controllee->data, &diff);
    return;
}

void media_session_controllee_unregister(MediaControlleePriv* controllee)
{
    media_metadata_deinit(&controllee->data);
    LIST_REMOVE(controllee, entry);
    free(controllee);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void* media_session_create(void* param)
{
    MediaSessionPriv* priv;

    (void)param;
    priv = malloc(sizeof(MediaSessionPriv));
    if (!priv)
        return NULL;

    LIST_INIT(&priv->controllers);
    LIST_INIT(&priv->controllees);
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
            media_session_controllee_unregister(controllee);
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
            media_session_controller_close(controller);
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
