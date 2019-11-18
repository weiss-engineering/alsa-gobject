// SPDX-License-Identifier: LGPL-3.0-or-later
#include "query.h"
#include "privates.h"

#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdbool.h>

#include <libudev.h>

#define CARD_SYSNAME_TEMPLATE       "card%u"
#define CONTROL_SYSNAME_TEMPLATE    "controlC%u"

// For error handling.
G_DEFINE_QUARK("alsactl-error", alsactl_error)

static void prepare_udev_enum(struct udev_enumerate **enumerator,
                              GError **error)
{
    struct udev *ctx;
    int err;

    ctx = udev_new();
    if (ctx == NULL) {
        generate_error(error, errno);
        return;
    }

    *enumerator = udev_enumerate_new(ctx);
    if (*enumerator == NULL) {
        generate_error(error, errno);
        udev_unref(ctx);
        return;
    }

    err = udev_enumerate_add_match_subsystem(*enumerator, "sound");
    if (err < 0) {
        generate_error(error, -err);
        udev_enumerate_unref(*enumerator);
        udev_unref(ctx);
        return;
    }

    err = udev_enumerate_scan_devices(*enumerator);
    if (err < 0) {
        generate_error(error, -err);
        udev_enumerate_unref(*enumerator);
        udev_unref(ctx);
    }
}

static struct udev_device *detect_dev(struct udev_enumerate *enumerator,
                                      struct udev_list_entry *entry,
                                      const char *prefix)
{
    struct udev *ctx = udev_enumerate_get_udev(enumerator);
    const char *syspath;
    struct udev_device *dev;
    const char *sysname;

    syspath = udev_list_entry_get_name(entry);
    if (syspath == NULL)
        return NULL;

    dev = udev_device_new_from_syspath(ctx, syspath);
    if (dev == NULL)
        return NULL;

    sysname = udev_device_get_sysname(dev);
    if (sysname == NULL) {
        udev_device_unref(dev);
        return NULL;
    }

    if (strstr(sysname, prefix) != sysname) {
        udev_device_unref(dev);
        return NULL;
    }

    return dev;
}

static void release_udev_enum(struct udev_enumerate *enumerator)
{
    struct udev *ctx = udev_enumerate_get_udev(enumerator);

    udev_enumerate_unref(enumerator);
    udev_unref(ctx);
}

static int compare_guint(const void *l, const void *r)
{
    const guint *x = l;
    const guint *y = r;

    return *x > *y;
}

/**
 * alsactl_get_card_id_list:
 * @entries: (array length=entry_count)(out): The list of numerical ID for sound
 *           cards.
 * @entry_count: The number of entries.
 * @error: A #GError.
 *
 * Get the list of numerical ID for available sound cards.
 */
void alsactl_get_card_id_list(guint **entries, gsize *entry_count,
                              GError **error)
{
    struct udev_enumerate *enumerator;
    struct udev_list_entry *entry, *entry_list;
    unsigned int count;
    unsigned int index;

    if (entries == NULL || entry_count == NULL || error == NULL) {
        generate_error(error, EINVAL);
        return;
    }

    prepare_udev_enum(&enumerator, error);
    if (enumerator == NULL)
        return;

    entry_list = udev_enumerate_get_list_entry(enumerator);

    count = 0;
    udev_list_entry_foreach(entry, entry_list) {
        struct udev_device *dev = detect_dev(enumerator, entry, "card");
        if (dev != NULL) {
            ++count;
            udev_device_unref(dev);
        }
    }

    // Nothing available.
    if (count == 0)
        goto end;

    *entries = g_try_malloc0_n(count, sizeof(**entries));
    if (*entries == NULL) {
        generate_error(error, ENOMEM);
        goto end;
    }

    index = 0;
    udev_list_entry_foreach(entry, entry_list) {
        struct udev_device *dev = detect_dev(enumerator, entry, "card");
        if (dev != NULL) {
            const char *sysnum = udev_device_get_sysnum(dev);
            long val;
            char *endptr;

            errno = 0;
            val = strtol(sysnum, &endptr, 10);
            if (errno == 0 && *endptr == '\0' && val >= 0) {
                (*entries)[index] = (guint)val;
                ++index;
            }
            udev_device_unref(dev);
        }
    }
    if (index != count) {
        generate_error(error, ENOENT);
        g_free(*entries);
        *entries = NULL;
        goto end;
    }
    *entry_count = count;

    qsort(*entries, count, sizeof(guint), compare_guint);
end:
    release_udev_enum(enumerator);
}

static void allocate_sysname(char **sysname ,const char *template,
                             guint card_id, GError **error)
{
    unsigned int digits;
    unsigned int number;
    unsigned int length;

    digits = 0;
    number = card_id;
    while (number > 0) {
        number /= 10;
        ++digits;
    }

    length = strlen(template) + digits;
    *sysname = g_try_malloc0(length + 1);
    if (*sysname == NULL) {
        generate_error(error, ENOMEM);
        return;
    }

    snprintf(*sysname, length, template, card_id);
}

static bool check_existence(char *sysname, GError **error)
{
    struct udev *ctx;
    struct udev_device *dev;
    bool result;

    ctx = udev_new();
    if (ctx == NULL) {
        generate_error(error, errno);
        return false;
    }

    dev = udev_device_new_from_subsystem_sysname(ctx, "sound", sysname);
    if (dev == NULL) {
        generate_error(error, errno);
        result = false;
    } else {
        result = true;
    }
    udev_device_unref(dev);

    udev_unref(ctx);

    return result;
}

/**
 * alsactl_get_card_sysname:
 * @card_id: The numeridcal ID of sound card.
 * @sysname: (out): The string for sysname of the sound card.
 * @error: A #GError.
 *
 * Allocate sysname for the sound card and return it when it exists.
 */
void alsactl_get_card_sysname(guint card_id, char **sysname, GError **error)
{
    char *name;

    g_return_if_fail(sysname != NULL);

    allocate_sysname(&name, CARD_SYSNAME_TEMPLATE, card_id, error);
    if (*error != NULL)
        return;

    if (!check_existence(name, error)) {
        g_free(name);
        return;
    }

    *sysname = name;
}

/**
 * alsactl_get_control_sysname:
 * @card_id: The numeridcal ID of sound card.
 * @sysname: (out): The string for sysname of control device for the sound card.
 * @error: A #GError.
 *
 * Allocate sysname of control device for the sound card and return it when
 * it exists.
 */
void alsactl_get_control_sysname(guint card_id, char **sysname, GError **error)
{
    char *name;

    g_return_if_fail(sysname != NULL);

    allocate_sysname(&name, CONTROL_SYSNAME_TEMPLATE, card_id, error);
    if (*error != NULL)
        return;

    if (!check_existence(name, error)) {
        g_free(name);
        return;
    }

    *sysname = name;
}
