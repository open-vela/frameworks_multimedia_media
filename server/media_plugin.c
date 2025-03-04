#include "media_plugin.h"
#include <errno.h>
#include <stdlib.h>

#include "media_common.h"

int mediad_plugin_init(media_plugin_t* plugin)
{
    int ret;

    plugin->priv = zalloc(plugin->priv_size);
    if (!plugin->priv) {
        return -ENOMEM;
    }

    if (plugin->init) {
        ret = plugin->init(plugin);
        if (ret < 0) {
            MEDIA_ERR("Media plugin:%s init failed: %d", plugin->name, ret);
            free(plugin->priv);
            plugin->priv = NULL;
            return ret;
        }
    }

    return 0;
}

void mediad_plugin_uinit(media_plugin_t* plugin)
{
    if (plugin->uninit && plugin->priv) {
        MEDIA_INFO("Media plugin:%s uninit", plugin->name);
        plugin->uninit(plugin);
    }

    free(plugin->priv);
    plugin->priv = NULL;
}
