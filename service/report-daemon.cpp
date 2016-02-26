/* reportd -- Software problem reporting service
 *
 * Copyright 2016 Red Hat Inc
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the licence or (at
 * your option) any later version.
 *
 * See the included COPYING file for more information.
 *
 * Author: Jakub Filak <jfilak@redhat.com>
 */

#include "report-daemon.h"
#include "report-service.h"
#include "report-task.h"
#include "report-dbus-constants.h"

#include <glibmm.h>
#include <giomm.h>

using namespace Glib;

static RefPtr<MainLoop> s_main_loop;
static GDBusObjectManagerServer *object_manager;

static void
on_name_acquired(GDBusConnection *connection,
                 const gchar      *name,
                 gpointer         )
{
    g_debug("Session bus with '%s' acquired", name);

    object_manager = g_dbus_object_manager_server_new(REPORTD_DBUS_OBJECT_MANAGER_PATH);

    ReportService *service = report_service_new(REPORTD_DBUS_SERVICE_PATH);
    g_dbus_object_manager_server_export(object_manager, G_DBUS_OBJECT_SKELETON(service));

    ReportTask *task = report_task_new(REPORTD_DBUS_TASK_BASE_PATH "1");
    g_dbus_object_manager_server_export(object_manager, G_DBUS_OBJECT_SKELETON(task));

    g_dbus_object_manager_server_set_connection(object_manager, connection);
}

static void
on_name_lost(GDBusConnection *,
             const gchar     *name,
             gpointer        )
{
    g_warning("DBus name has been lost: %s", name);
    s_main_loop->quit();
}

void
on_signal_quit(int)
{
    s_main_loop->quit();
}

int
main(void)
{
    Gio::init();

    guint bus_name_id = g_bus_own_name(G_BUS_TYPE_SESSION,
                                       REPORTD_DBUS_BUS_NAME,
                                       G_BUS_NAME_OWNER_FLAGS_NONE,
                                       NULL,
                                       on_name_acquired,
                                       on_name_lost,
                                       NULL,
                                       NULL);

    s_main_loop = MainLoop::create();

    //signal(SIGINT, on_signal_quit);
    //signal(SIGTERM, on_signal_quit);

    s_main_loop->run();

    if (bus_name_id > 0) {
        g_bus_unown_name(bus_name_id);
    }

    return bus_name_id > 0;
}
