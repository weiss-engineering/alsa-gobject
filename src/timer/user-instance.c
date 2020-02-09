// SPDX-License-Identifier: LGPL-3.0-or-later
#include "user-instance.h"
#include "query.h"
#include "privates.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

struct _ALSATimerUserInstancePrivate {
    int fd;
};
G_DEFINE_TYPE_WITH_PRIVATE(ALSATimerUserInstance, alsatimer_user_instance, G_TYPE_OBJECT)

static void timer_user_instance_finalize(GObject *obj)
{
    ALSATimerUserInstance *self = ALSATIMER_USER_INSTANCE(obj);
    ALSATimerUserInstancePrivate *priv =
                            alsatimer_user_instance_get_instance_private(self);

    if (priv->fd >= 0)
        close(priv->fd);

    G_OBJECT_CLASS(alsatimer_user_instance_parent_class)->finalize(obj);
}

static void alsatimer_user_instance_class_init(ALSATimerUserInstanceClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = timer_user_instance_finalize;
}

static void alsatimer_user_instance_init(ALSATimerUserInstance *self)
{
    ALSATimerUserInstancePrivate *priv =
                            alsatimer_user_instance_get_instance_private(self);

    priv->fd = -1;
}

/**
 * alsatimer_user_instance_open:
 * @self: A #ALSATimerUserInstance.
 * @error: A #GError.
 *
 * Open ALSA Timer character device to allocate queue.
 */
void alsatimer_user_instance_open(ALSATimerUserInstance *self, GError **error)
{
    ALSATimerUserInstancePrivate *priv;
    char *devnode;

    g_return_if_fail(ALSATIMER_IS_USER_INSTANCE(self));
    priv = alsatimer_user_instance_get_instance_private(self);

    alsatimer_get_devnode(&devnode, error);
    if (*error != NULL)
        return;

    priv->fd = open(devnode, O_RDONLY);
    g_free(devnode);
    if (priv->fd < 0) {
        generate_error(error, errno);
    }
}

ALSATimerUserInstance *alsatimer_user_instance_new()
{
    return g_object_new(ALSATIMER_TYPE_USER_INSTANCE, NULL);
}

/**
 * alsatimer_user_instance_attach:
 * @self: A #ALSATimerUserInstance.
 * @device_id: A #ALSATimerDeviceId to which the instance is attached.
 * @error: A #GError.
 *
 * Attach the instance to the timer device.
 */
void alsatimer_user_instance_attach(ALSATimerUserInstance *self,
                                    ALSATimerDeviceId *device_id,
                                    GError **error)
{
    ALSATimerUserInstancePrivate *priv;
    struct snd_timer_select sel = {0};

    g_return_if_fail(ALSATIMER_IS_USER_INSTANCE(self));
    g_return_if_fail(device_id != NULL);
    priv = alsatimer_user_instance_get_instance_private(self);

    sel.id = *device_id;
    if (ioctl(priv->fd, SNDRV_TIMER_IOCTL_SELECT, &sel) < 0)
        generate_error(error, errno);
}

/**
 * alsatimer_user_instance_attach_as_slave:
 * @self: A #ALSATimerUserInstance.
 * @slave_class: The class identifier of master instance, one of
 *               #ALSATimerSlaveClass.
 * @slave_id: The numerical identifier of master instance.
 * @error: A #GError.
 *
 * Attach the instance to timer device as an slave to another instance indicated
 * by a pair of slave_class and slave_id. If the slave_class is for application
 * (=ALSATIMER_SLAVE_CLASS_APPLICATION), the slave_id is for the PID of
 * application process which owns the instance of timer. If the slave_class is
 * for ALSA sequencer (=ALSATIMER_SLAVE_CLASS_SEQUENCER), the slave_id is the
 * numerical ID of queue bound for timer device.
 */
void alsatimer_user_instance_attach_as_slave(ALSATimerUserInstance *self,
                                        ALSATimerSlaveClass slave_class,
                                        int slave_id,
                                        GError **error)
{
    ALSATimerUserInstancePrivate *priv;
    struct snd_timer_select sel = {0};

    g_return_if_fail(ALSATIMER_IS_USER_INSTANCE(self));
    priv = alsatimer_user_instance_get_instance_private(self);

    sel.id.dev_class = SNDRV_TIMER_CLASS_SLAVE;
    sel.id.dev_sclass = slave_class;
    sel.id.device = slave_id;
    if (ioctl(priv->fd, SNDRV_TIMER_IOCTL_SELECT, &sel) < 0)
        generate_error(error, errno);
}

/**
 * alsatimer_user_instance_get_info:
 * @self: A #ALSATimerUserInstance.
 * @instance_info: (out): A #ALSATimerInstanceInfo.
 * @error: A #GError.
 *
 * Return the information of device if attached to the instance.
 */
void alsatimer_user_instance_get_info(ALSATimerUserInstance *self,
                                      ALSATimerInstanceInfo **instance_info,
                                      GError **error)
{
    ALSATimerUserInstancePrivate *priv;
    struct snd_timer_info *info;

    g_return_if_fail(ALSATIMER_IS_USER_INSTANCE(self));
    priv = alsatimer_user_instance_get_instance_private(self);

    *instance_info = g_object_new(ALSATIMER_TYPE_INSTANCE_INFO, NULL);
    timer_instance_info_refer_private(*instance_info, &info);

    if (ioctl(priv->fd, SNDRV_TIMER_IOCTL_INFO, info) < 0) {
        generate_error(error, errno);
        g_object_unref(*instance_info);
    }
}
