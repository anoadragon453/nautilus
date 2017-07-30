/*
 *  nautilus-trash-monitor.c: Nautilus trash state watcher.
 *
 *  Copyright (C) 2000, 2001 Eazel, Inc.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public
 *  License along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 *  Author: Pavel Cisler <pavel@eazel.com>
 */

#include <config.h>
#include "nautilus-trash-monitor.h"

#include "nautilus-directory-notify.h"
#include "nautilus-directory.h"
#include "nautilus-file-attributes.h"
#include <eel/eel-debug.h>
#include <gio/gio.h>
#include <string.h>

struct NautilusTrashMonitorDetails
{
    gboolean empty;
    GFileMonitor *file_monitor;
};

enum
{
    TRASH_STATE_CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];
static NautilusTrashMonitor *nautilus_trash_monitor = NULL;

G_DEFINE_TYPE (NautilusTrashMonitor, nautilus_trash_monitor, G_TYPE_OBJECT)

static void
nautilus_trash_monitor_finalize (GObject *object)
{
    NautilusTrashMonitor *trash_monitor;

    trash_monitor = NAUTILUS_TRASH_MONITOR (object);

    if (trash_monitor->details->file_monitor)
    {
        g_object_unref (trash_monitor->details->file_monitor);
    }

    G_OBJECT_CLASS (nautilus_trash_monitor_parent_class)->finalize (object);
}

static void
nautilus_trash_monitor_class_init (NautilusTrashMonitorClass *klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = nautilus_trash_monitor_finalize;

    signals[TRASH_STATE_CHANGED] = g_signal_new
                                       ("trash-state-changed",
                                       G_TYPE_FROM_CLASS (object_class),
                                       G_SIGNAL_RUN_LAST,
                                       G_STRUCT_OFFSET (NautilusTrashMonitorClass, trash_state_changed),
                                       NULL, NULL,
                                       g_cclosure_marshal_VOID__BOOLEAN,
                                       G_TYPE_NONE, 1,
                                       G_TYPE_BOOLEAN);

    g_type_class_add_private (object_class, sizeof (NautilusTrashMonitorDetails));
}

static void
update_empty_info (NautilusTrashMonitor *trash_monitor,
                   gboolean              is_empty)
{
    if (trash_monitor->details->empty == is_empty)
    {
        return;
    }

    trash_monitor->details->empty = is_empty;

    /* trash got empty or full, notify everyone who cares */
    g_signal_emit (trash_monitor,
                   signals[TRASH_STATE_CHANGED], 0,
                   trash_monitor->details->empty);
}

/* Use G_FILE_ATTRIBUTE_TRASH_ITEM_COUNT since we only want to know whether the
 * trash is empty or not, not access its children. This is available for the
 * trash backend since it uses a cache. In this way we prevent flooding the
 * trash backend with enumeration requests when trashing > 1000 files
 */
static void
trash_query_info_cb (GObject      *source,
                     GAsyncResult *res,
                     gpointer      user_data)
{
    NautilusTrashMonitor *trash_monitor = user_data;
    GFileInfo *info;
    guint32 item_count;
    gboolean is_empty = TRUE;

    info = g_file_query_info_finish (G_FILE (source), res, NULL);

    if (info != NULL)
    {
        item_count = g_file_info_get_attribute_uint32 (info,
                                                       G_FILE_ATTRIBUTE_TRASH_ITEM_COUNT);
        is_empty = item_count == 0;

        g_object_unref (info);
    }

    update_empty_info (trash_monitor, is_empty);

    g_object_unref (trash_monitor);
}

static void
schedule_update_info (NautilusTrashMonitor *trash_monitor)
{
    GFile *location;

    location = g_file_new_for_uri ("trash:///");
    g_file_query_info_async (location,
                             G_FILE_ATTRIBUTE_TRASH_ITEM_COUNT,
                             G_FILE_QUERY_INFO_NONE,
                             G_PRIORITY_DEFAULT, NULL,
                             trash_query_info_cb, g_object_ref (trash_monitor));

    g_object_unref (location);
}

static void
file_changed (GFileMonitor      *monitor,
              GFile             *child,
              GFile             *other_file,
              GFileMonitorEvent  event_type,
              gpointer           user_data)
{
    NautilusTrashMonitor *trash_monitor;

    trash_monitor = NAUTILUS_TRASH_MONITOR (user_data);

    schedule_update_info (trash_monitor);
}

static void
nautilus_trash_monitor_init (NautilusTrashMonitor *trash_monitor)
{
    GFile *location;

    trash_monitor->details = G_TYPE_INSTANCE_GET_PRIVATE (trash_monitor,
                                                          NAUTILUS_TYPE_TRASH_MONITOR,
                                                          NautilusTrashMonitorDetails);

    trash_monitor->details->empty = TRUE;

    location = g_file_new_for_uri ("trash:///");

    trash_monitor->details->file_monitor = g_file_monitor_file (location, 0, NULL, NULL);

    g_signal_connect (trash_monitor->details->file_monitor, "changed",
                      (GCallback) file_changed, trash_monitor);

    g_object_unref (location);

    schedule_update_info (trash_monitor);
}

static void
unref_trash_monitor (void)
{
    g_object_unref (nautilus_trash_monitor);
}

NautilusTrashMonitor *
nautilus_trash_monitor_get (void)
{
    if (nautilus_trash_monitor == NULL)
    {
        /* not running yet, start it up */

        nautilus_trash_monitor = NAUTILUS_TRASH_MONITOR
                                     (g_object_new (NAUTILUS_TYPE_TRASH_MONITOR, NULL));
        eel_debug_call_at_shutdown (unref_trash_monitor);
    }

    return nautilus_trash_monitor;
}

gboolean
nautilus_trash_monitor_is_empty (void)
{
    NautilusTrashMonitor *monitor;

    monitor = nautilus_trash_monitor_get ();
    return monitor->details->empty;
}
