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

#ifndef __REPORT_TASK_H__
#define __REPORT_TASK_H__

#include <giomm.h>

#include "report-dbus-generated.h"

G_BEGIN_DECLS

#define REPORT_TYPE_TASK            (report_task_get_type ())
#define REPORT_TASK(inst)           (G_TYPE_CHECK_INSTANCE_CAST ((inst), REPORT_TYPE_TASK, ReportTask))
#define REPORT_IS_TASK(inst)        (G_TYPE_CHECK_INSTANCE_TYPE ((inst), REPORT_TYPE_TASK))
#define REPORT_TASK_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), REPORT_TYPE_TASK, ReportTaskClass))
#define REPORT_IS_TASK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), REPORT_TYPE_TASK))
#define REPORT_TASK_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), REPORT_TYPE_TASK, ReportTaskClass))

typedef struct _ReportTask ReportTask;
typedef struct _ReportTaskClass ReportTaskClass;
typedef struct _ReportTaskPrivate ReportTaskPrivate;

struct _ReportTask {
	ReportDbusTaskSkeleton parent;
	ReportTaskPrivate *pv;
};

struct _ReportTaskClass {
	ReportDbusTaskSkeletonClass parent_class;
};

GType       report_task_get_type(void);
ReportTask *report_task_new     (const gchar *object_path);

G_END_DECLS

#endif /*__REPORT_TASK_H__*/
