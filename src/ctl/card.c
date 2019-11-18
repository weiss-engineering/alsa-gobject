// SPDX-License-Identifier: LGPL-3.0-or-later
#include "card.h"
#include "query.h"
#include "privates.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>

struct _ALSACtlCardPrivate {
    int fd;
    char *devnode;
};
G_DEFINE_TYPE_WITH_PRIVATE(ALSACtlCard, alsactl_card, G_TYPE_OBJECT)

enum ctl_card_prop_type {
    CTL_CARD_PROP_DEVNODE = 1,
    CTL_CARD_PROP_COUNT,
};
static GParamSpec *ctl_card_props[CTL_CARD_PROP_COUNT] = { NULL, };

static void ctl_card_get_property(GObject *obj, guint id, GValue *val,
                                  GParamSpec *spec)
{
    ALSACtlCard *self = ALSACTL_CARD(obj);
    ALSACtlCardPrivate *priv = alsactl_card_get_instance_private(self);

    switch (id) {
    case CTL_CARD_PROP_DEVNODE:
        g_value_set_string(val, priv->devnode);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, spec);
        break;
    }
}

static void ctl_card_finalize(GObject *obj)
{
    ALSACtlCard *self = ALSACTL_CARD(obj);
    ALSACtlCardPrivate *priv = alsactl_card_get_instance_private(self);

    if (priv->fd >= 0) {
        close(priv->fd);
        g_free(priv->devnode);
    }

    G_OBJECT_CLASS(alsactl_card_parent_class)->finalize(obj);
}

static void alsactl_card_class_init(ALSACtlCardClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = ctl_card_finalize;
    gobject_class->get_property = ctl_card_get_property;

    ctl_card_props[CTL_CARD_PROP_DEVNODE] =
        g_param_spec_string("devnode", "devnode",
                            "The full path of control character device.",
                            "",
                            G_PARAM_READABLE);

    g_object_class_install_properties(gobject_class, CTL_CARD_PROP_COUNT,
                                      ctl_card_props);
}

static void alsactl_card_init(ALSACtlCard *self)
{
    ALSACtlCardPrivate *priv = alsactl_card_get_instance_private(self);

    priv->fd = -1;
}

/**
 * alsactl_card_new:
 *
 * Allocate and return an instance of ALSACtlCard class.
 */
ALSACtlCard *alsactl_card_new()
{
    return g_object_new(ALSACTL_TYPE_CARD, NULL);
}

/**
 * alsactl_card_open:
 * @self: A #ALSACtlCard.
 * @card_id: The numerical ID of sound card.
 * @error: A #GError.
 *
 * Open ALSA control character device for the sound card.
 */
void alsactl_card_open(ALSACtlCard *self, guint card_id, GError **error)
{
    ALSACtlCardPrivate *priv;
    char *devnode;

    g_return_if_fail(ALSACTL_IS_CARD(self));
    priv = alsactl_card_get_instance_private(self);

    alsactl_get_control_devnode(card_id, &devnode, error);
    if (*error != NULL)
        return;

    priv->fd = open(devnode, O_RDONLY | O_NONBLOCK);
    if (priv->fd < 0) {
        generate_error(error, errno);
        g_free(devnode);
        return;
    }

    priv->devnode = devnode;
}

/**
 * alsactl_card_get_info:
 * @self: A #ALSACtlCard.
 * @card_info: (out): A #ALSACtlCardInfo for the sound card.
 * @error: A #GError.
 *
 * Get the information of sound card.
 */
void alsactl_card_get_info(ALSACtlCard *self, ALSACtlCardInfo **card_info,
                           GError **error)
{
    ALSACtlCardPrivate *priv;
    struct snd_ctl_card_info *info;

    g_return_if_fail(ALSACTL_IS_CARD(self));
    priv = alsactl_card_get_instance_private(self);

    *card_info = g_object_new(ALSACTL_TYPE_CARD_INFO, NULL);

    ctl_card_info_refer_private(*card_info, &info);
    if (ioctl(priv->fd, SNDRV_CTL_IOCTL_CARD_INFO, info) < 0) {
        generate_error(error, errno);
        g_object_unref(*card_info);
    }
}

static void allocate_elem_ids(int fd, struct snd_ctl_elem_list *list,
                              GError **error)
{
    struct snd_ctl_elem_id *ids;

    // Help for deallocation.
    memset(list, 0, sizeof(*list));

    // Get the number of elements in this control device.
    if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, list) < 0) {
        generate_error(error, errno);
        return;
    }

    // No elements found.
    if (list->count == 0)
        return;

    // Allocate spaces for these elements.
    ids = calloc(list->count, sizeof(*ids));
    if (!ids) {
        generate_error(error, ENOMEM);
        return;
    }

    list->offset = 0;
    while (list->offset < list->count) {
        // ALSA middleware has limitation of one operation.
        // 1000 is enought less than the limitation.
        list->space = MIN(list->count - list->offset, 1000);
        list->pids = ids + list->offset;

        // Get the IDs of elements in this control device.
        if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, list) < 0) {
            generate_error(error, errno);
            free(ids);
            list->pids = NULL;
            return;
        }

        list->offset += list->space;
    }
    list->pids = ids;
    list->space = list->count;
}

static inline void deallocate_elem_ids(struct snd_ctl_elem_list *list)
{
    if (list->pids!= NULL)
        free(list->pids);
}

/**
 * alsactl_card_get_elem_id_list:
 * @self: A #ALSACtlCard.
 * @entries: (element-type ALSACtl.ElemId)(out): The list of entries for
 *           ALSACtlElemId.
 * @error: A #GError.
 *
 * Generate a list of ALSACtlElemId for ALSA control character device
 * associated to the sound card.
 */
void alsactl_card_get_elem_id_list(ALSACtlCard *self, GList **entries,
                                   GError **error)
{
    ALSACtlCardPrivate *priv;
    struct snd_ctl_elem_list list = {0};
    int i;

    g_return_if_fail(ALSACTL_IS_CARD(self));
    priv = alsactl_card_get_instance_private(self);

    allocate_elem_ids(priv->fd, &list, error);
    if (*error != NULL)
        return;

    for (i = 0; i < list.count; ++i) {
        struct snd_ctl_elem_id *id = list.pids + i;
        ALSACtlElemId *elem_id = g_boxed_copy(ALSACTL_TYPE_ELEM_ID, id);
        *entries = g_list_append(*entries, (gpointer)elem_id);
    }

    deallocate_elem_ids(&list);
}

/**
 * alsactl_card_lock_elem:
 * @self: A #ALSACtlCard.
 * @elem_id: A #ALSACtlElemId.
 * @lock: whether to lock or unlock the element.
 * @error: A #GError.
 *
 * Lock/Unlock indicated element not to be written by the other processes.
 */
void alsactl_card_lock_elem(ALSACtlCard *self, const ALSACtlElemId *elem_id,
                            gboolean lock, GError **error)
{
    ALSACtlCardPrivate *priv;
    int ret;

    g_return_if_fail(ALSACTL_IS_CARD(self));
    priv = alsactl_card_get_instance_private(self);

    if (lock)
        ret = ioctl(priv->fd, SNDRV_CTL_IOCTL_ELEM_LOCK, elem_id);
    else
        ret = ioctl(priv->fd, SNDRV_CTL_IOCTL_ELEM_UNLOCK, elem_id);
    if (ret < 0)
        generate_error(error, errno);
}
