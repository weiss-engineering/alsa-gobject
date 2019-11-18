// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef __ALSA_GOBJECT_ALSARAWMIDI_QUERY__H__
#define __ALSA_GOBJECT_ALSARAWMIDI_QUERY__H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

void alsarawmidi_get_device_id_list(guint card_id,
                                    guint **entries, gsize *entry_count,
                                    GError **error);

void alsarawmidi_get_rawmidi_sysname(guint card_id, guint device_id,
                                     char **sysname, GError **error);

G_END_DECLS

#endif
