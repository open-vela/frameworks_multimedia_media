#include "media_plugin.h"
#include <errno.h>
#include <libavutil/mem.h>

#include "media_common.h"

int mediad_plugin_init(MediadPlugin* plugin)
{
    int ret;

    plugin->priv = av_mallocz(plugin->priv_size);
    if (!plugin->priv) {
        return -ENOMEM;
    }

    if (plugin->init) {
        ret = plugin->init(plugin);
        if (ret < 0) {
            MEDIA_ERR("Media plugin:%s init failed: %d", plugin->name, ret);
            av_freep(&plugin->priv);
            return ret;
        }
    }

    return 0;
}

void mediad_plugin_uinit(MediadPlugin* plugin)
{
    if (plugin->uninit && plugin->priv) {
        plugin->uninit(plugin);
    }

    if (plugin->priv) {
        av_freep(&plugin->priv);
    }
}
