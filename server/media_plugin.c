#include "media_plugin.h"
#include <errno.h>
#include <stdlib.h>

#include "media_common.h"

int mediad_plugin_init(MediadPlugin *plugin)
{
    int ret;

    plugin->priv = calloc(1, plugin->priv_size);
    if (!plugin->priv) {
        return -ENOMEM;
    }

    if (plugin->init) {
        ret = plugin->init(plugin);
        if (ret < 0) {
            MEDIA_ERR("Media plugin:%s init failed: %d", plugin->name, ret);
            free(plugin->priv);
            return ret;
        }
    }

    return 0;
}

void mediad_plugin_uinit(MediadPlugin *plugin)
{
    if (plugin->uninit) {
        plugin->uninit(plugin);
    }

    if (plugin->priv) {
        free(plugin->priv);
        plugin->priv = NULL;
    }
}

