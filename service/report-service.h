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

#include <giomm.h>

#include "report-dbus-generated.h"

G_BEGIN_DECLS

#define REPORT_TYPE_SERVICE            (report_service_get_type())
#define REPORT_SERVICE(inst)           (G_TYPE_CHECK_INSTANCE_CAST ((inst), REPORT_TYPE_SERVICE, ReportService))
#define REPORT_IS_SERVICE(inst)        (G_TYPE_CHECK_INSTANCE_TYPE ((inst), REPORT_TYPE_SERVICE))
#define REPORT_SERVICE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), REPORT_TYPE_SERVICE, ReportServiceClass))
#define REPORT_IS_SERVICE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), REPORT_TYPE_SERVICE))
#define REPORT_SERVICE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), REPORT_TYPE_SERVICE, ReportServiceClass))

typedef struct _ReportService ReportService;
typedef struct _ReportServiceClass ReportServiceClass;
typedef struct _ReportServicePrivate ReportServicePrivate;

struct _ReportService {
	ReportDbusServiceSkeleton parent;
	ReportServicePrivate *pv;
};

struct _ReportServiceClass {
	ReportDbusServiceSkeletonClass parent_class;
};

GType          report_service_get_type(void);

ReportService *report_service_new(const gchar *object_path);

G_END_DECLS

#endif /*__REPORT_SERVICE_H__*/
