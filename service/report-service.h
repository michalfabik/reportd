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

#include "config.h"

#ifndef __REPORT_SERVICE_H__
#define __REPORT_SERVICE_H__

#include <gio/gio.h>

G_BEGIN_DECLS

#define REPORT_TYPE_SERVICE report_service_get_type()

G_DECLARE_FINAL_TYPE(ReportService, report_service, REPORT, SERVICE,
                     GDBusObjectSkeleton)

ReportService *report_service_new(const gchar *object_path);

G_END_DECLS

#endif /*__REPORT_SERVICE_H__*/
