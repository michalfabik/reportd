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

#pragma once

#include <stdbool.h>

#include <gio/gio.h>

#define REPORTD_TYPE_DAEMON reportd_daemon_get_type ()

G_DECLARE_FINAL_TYPE (ReportdDaemon, reportd_daemon, REPORTD, DAEMON, GObject)

char          *reportd_daemon_get_problem_directory  (ReportdDaemon        *daemon,
                                                      const char           *entry,
                                                      GError              **error);
bool           reportd_daemon_push_problem_directory (ReportdDaemon        *daemon,
                                                      const char           *problem_directory,
                                                      GError              **error);

void           reportd_daemon_get_bus_connections    (ReportdDaemon        *daemon,
                                                      GDBusConnection     **system_bus_connection,
                                                      GDBusConnection     **session_bus_connection);

void           reportd_daemon_register_object        (ReportdDaemon        *daemon,
                                                      GDBusObjectSkeleton  *object);
void           reportd_daemon_unregister_object      (ReportdDaemon        *daemon,
                                                      GDBusObject          *object);

int            reportd_daemon_run                    (ReportdDaemon        *daemon,
                                                      GError              **error);
void           reportd_daemon_quit                   (ReportdDaemon        *daemon,
                                                      GError               *error);

ReportdDaemon *reportd_daemon_new                    (bool                  use_system_bus);
