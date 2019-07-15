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

#include <workflow.h>

G_BEGIN_DECLS

#define REPORTD_TASK_ERROR reportd_task_error_quark ()
#define REPORTD_TYPE_TASK reportd_task_get_type ()

G_DECLARE_FINAL_TYPE (ReportdTask, reportd_task, REPORTD, TASK, GDBusObjectSkeleton)

typedef enum
{
    REPORTD_TASK_ERROR_EVENT_HANDLER_FAILED,
    REPORTD_TASK_ERROR_NO_EVENT_HANDLERS,
} ReportdTaskError;

GQuark reportd_task_error_quark (void);

ReportdTask *reportd_task_new (ReportdDaemon   *daemon,
                               const char      *object_path,
                               const char      *problem_path,
                               struct workflow *workflow);

G_END_DECLS
