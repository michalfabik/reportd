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

#include "reportd-types.h"

#include <gio/gio.h>

#define REPORTD_TYPE_SERVICE reportd_service_get_type ()
G_DECLARE_FINAL_TYPE(ReportdService, reportd_service, REPORTD, SERVICE,
                     GDBusObjectSkeleton)

ReportdService *reportd_service_new (ReportdDaemon *daemon,
                                     const char    *object_path);
