/****************************************************************************
 * frameworks/media/server/media_daemon.c
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

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "media_common.h"
#include "media_plugin.h"
#include "media_server.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MAX_POLLFDS CONFIG_MEDIA_SERVER_MAX_POLLFDS
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct MediaPriv {
    int idx[MAX_POLLFDS];
    struct pollfd fds[MAX_POLLFDS];
    void* ctx[MAX_POLLFDS];
} MediaPriv;

/****************************************************************************
 * Private Data
 ****************************************************************************/
#ifdef CONFIG_MEDIA_FOCUS
extern MediadPlugin media_focus_plugin;
#endif
#ifdef CONFIG_LIB_FFMPEG
extern MediadPlugin audio_graph_plugin;
extern MediadPlugin media_session_plugin;
extern MediadPlugin media_player_plugin;
extern MediadPlugin media_recorder_plugin;
#endif
#ifdef CONFIG_LIB_PFW
extern MediadPlugin media_policy_plugin;
#endif
extern MediadPlugin media_server_plugin;
#ifdef CONFIG_MEDIA_TRIGGER
extern MediadPlugin media_trigger_plugin;
#endif

MediadPlugin* g_media[] = {
#ifdef CONFIG_MEDIA_FOCUS
    &media_focus_plugin,
#endif
#ifdef CONFIG_LIB_FFMPEG
    &audio_graph_plugin,
    &media_session_plugin,
    &media_player_plugin,
    &media_recorder_plugin,
#endif
#ifdef CONFIG_LIB_PFW
    &media_policy_plugin,
#endif
#ifdef CONFIG_MEDIA_TRIGGER
    &media_trigger_plugin,
#endif
    &media_server_plugin,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

MediadPlugin* media_plugin_get(const char* name)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(g_media); i++) {
        if (!strcmp(name, g_media[i]->name))
            return g_media[i];
    }

    return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char* argv[])
{
    MediaPriv* priv;
    int ret, n, i;

    priv = zalloc(sizeof(MediaPriv));
    if (!priv)
        return -ENOMEM;

    for (i = 0; i < ARRAY_SIZE(g_media); i++) {
        ret = mediad_plugin_init(g_media[i]);
        if (ret < 0) {
            MEDIA_ERR("%s create failed ret:%d\n", g_media[i]->name, ret);
            goto out;
        }
    }

    while (1) {
        for (n = i = 0; i < ARRAY_SIZE(g_media); i++) {
            if (!g_media[i]->get)
                continue;

            ret = g_media[i]->get(g_media[i], &priv->fds[n], &priv->ctx[n], MAX_POLLFDS - n);
            if (ret < 0) {
                MEDIA_ERR("%s get_pollfds failed %d\n", g_media[i]->name, ret);
                continue;
            }

            while (ret--)
                priv->idx[n++] = i;
        }

        assert(n > 0 && n < MAX_POLLFDS);

        poll(priv->fds, n, -1);

        for (i = 0; i < n; i++) {
            if (!priv->fds[i].revents)
                continue;

            ret = g_media[priv->idx[i]]->available(g_media[priv->idx[i]],
                &priv->fds[i], priv->ctx[i]);
            if (ret < 0 && ret != -EAGAIN && ret != -EPIPE)
                MEDIA_ERR("%s poll_available failed %d\n", g_media[priv->idx[i]]->name, ret);
        }

        for (i = 0; i < ARRAY_SIZE(g_media); i++) {
            if (!g_media[i]->run_once)
                continue;

            ret = g_media[i]->run_once(g_media[i]);
            if (ret < 0)
                MEDIA_ERR("%s run_once failed %d\n", g_media[i]->name, ret);
        }
    }

out:
    MEDIA_INFO("media daemon exit\n");
    for (i = 0; i < ARRAY_SIZE(g_media); i++)
        mediad_plugin_uinit(g_media[i]);

    free(priv);
    return 0;
}
