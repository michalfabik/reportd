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

#include <gio/gio.h>

G_BEGIN_DECLS

#define REPORT_TYPE_TASK report_task_get_type ()

G_DECLARE_FINAL_TYPE(ReportTask, report_task, REPORT, TASK, GDBusObjectSkeleton)

ReportTask *report_task_new(const gchar     *object_path,
                            const gchar     *problem_path,
                            struct workflow *workflow);

G_END_DECLS

#endif /*__REPORT_TASK_H__*/
