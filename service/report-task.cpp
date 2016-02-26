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

#include "report-task.h"

G_DEFINE_TYPE(ReportTask, report_task, G_TYPE_DBUS_OBJECT_SKELETON);

struct _ReportTaskPrivate
{
    ReportDbusTask *task_iface;
};

static gboolean
report_task_handle_start(ReportDbusTask * /*object*/,
                         GDBusMethodInvocation * /*invocation*/)
{
    return TRUE;
}

static gboolean
report_task_handle_cancel(ReportDbusTask * /*object*/,
                          GDBusMethodInvocation * /*invocation*/)
{
    return TRUE;
}

static void
report_task_init(ReportTask *self)
{
    self->pv = G_TYPE_INSTANCE_GET_PRIVATE(self, REPORT_TYPE_TASK, ReportTaskPrivate);

    self->pv->task_iface = report_dbus_task_skeleton_new();

    g_signal_connect(self->pv->task_iface, "handle-start",
                     G_CALLBACK (report_task_handle_start), self);

    g_signal_connect(self->pv->task_iface, "handle-cancel",
                     G_CALLBACK (report_task_handle_cancel), self);

    g_dbus_object_skeleton_add_interface(G_DBUS_OBJECT_SKELETON(self),
                                         G_DBUS_INTERFACE_SKELETON(self->pv->task_iface));
}

static void
report_task_constructed(GObject *obj)
{
    ReportTask *self = REPORT_TASK(obj);

    G_OBJECT_CLASS(report_task_parent_class)->constructed(obj);

    /* The dbus version property of the task */
    report_dbus_task_set_status(self->pv->task_iface, "NEW");
}

static void
report_task_class_init(ReportTaskClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->constructed = report_task_constructed;

    g_type_class_add_private (klass, sizeof (ReportTaskPrivate));
}

ReportTask *report_task_new(const gchar *object_path)
{
    gpointer object = g_object_new(REPORT_TYPE_TASK,
                                   "g-object-path", object_path,
                                   NULL);

    return static_cast<ReportTask *>(object);
}
