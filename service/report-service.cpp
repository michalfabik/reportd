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

#include "report-service.h"
#include "report-task.h"
#include "report-daemon.h"
#include "report-dbus-constants.h"

#include <iostream>
#include <cstring>

#include <dump_dir.h>
#include <run_event.h>
#include <workflow.h>

G_DEFINE_TYPE(ReportService, report_service, G_TYPE_DBUS_OBJECT_SKELETON);

struct _ReportServicePrivate {
    ReportDbusService *service_iface;
    GHashTable        *workflows;
    unsigned long      task_cnt;
};

static workflow_t *
report_service_find_workflow_by_name(ReportService *self,
                                     const char    *wf_name)
{
    return (workflow_t *)g_hash_table_lookup(self->pv->workflows, (gpointer)wf_name);
}

static gboolean
report_service_handle_create_task(ReportDbusService     * /*object*/,
                                  GDBusMethodInvocation *invocation,
                                  const gchar           *arg_workflow,
                                  const gchar           *arg_problem,
                                  ReportService         *self)
{
    if (self->pv->task_cnt == ULONG_MAX) {
        g_dbus_method_invocation_return_error(invocation,
                G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                "Reportd Service cannot create a new task");
        return TRUE;
    }

    unsigned long task_id = self->pv->task_cnt++;
    std::string task_path(std::string(REPORTD_DBUS_TASK_BASE_PATH) + std::to_string(task_id));
    ReportTask *t = report_task_new(task_path.c_str(), arg_workflow, arg_problem);
    ReportDaemon::inst().register_object(G_DBUS_OBJECT_SKELETON(t));

    GVariant *retval = g_variant_new("(o)", task_path.c_str());
    g_dbus_method_invocation_return_value(invocation, retval);

    return TRUE;
}

static gboolean
report_service_handle_get_workflows(ReportDbusService * /*object*/,
                                    GDBusMethodInvocation *invocation,
                                    const gchar           *arg_problem,
                                    ReportService         *self)
{
    std::string problem_dir;

    try {
        problem_dir = ReportDaemon::inst().get_problem_directory(arg_problem);
    }
    catch (const Glib::Error &err) {
        g_dbus_method_invocation_return_error(invocation,
                                              err.domain(),
                                              err.code(),
                                              err.what().c_str());
        return TRUE;
    }

    GList *wfs = list_possible_events_glist(problem_dir.c_str(), "workflow");

    GVariantBuilder top_builder;
    g_variant_builder_init(&top_builder, G_VARIANT_TYPE("a(sss)"));

    for (GList *wf_iter = wfs; wf_iter; wf_iter = g_list_next(wf_iter)) {
        const char *possible_wf = (const char *)wf_iter->data;
        workflow_t *wf = report_service_find_workflow_by_name(self,
                                                              possible_wf);

        if (wf == NULL) {
            g_message("Possible workflow without configuration: %s", possible_wf);
            continue;
        }

        GVariant *children[3];
        children[0] = g_variant_new_string(wf_get_name(wf));
        children[1] = g_variant_new_string(wf_get_screen_name(wf));
        children[2] = g_variant_new_string(wf_get_description(wf));
        GVariant *entry = g_variant_new_tuple(children, 3);

        g_variant_builder_add_value(&top_builder, entry);
    }

    GVariant *retval[1];
    retval[0] = g_variant_builder_end(&top_builder);
    GVariant *value = g_variant_new_tuple(retval, 1);

    g_dbus_method_invocation_return_value(invocation, value);

    g_list_free(wfs);

    return TRUE;
}

static void
report_service_init(ReportService *self)
{
    self->pv = G_TYPE_INSTANCE_GET_PRIVATE(self, REPORT_TYPE_SERVICE, ReportServicePrivate);

    self->pv->service_iface = report_dbus_service_skeleton_new();
    self->pv->task_cnt = 1;
    self->pv->workflows = load_workflow_config_data(/*the default location*/NULL);

    g_signal_connect(self->pv->service_iface,
                     "handle-create-task",
                     G_CALLBACK(report_service_handle_create_task),
                     self);

    g_signal_connect(self->pv->service_iface,
                     "handle-get-workflows",
                     G_CALLBACK(report_service_handle_get_workflows),
                     self);

    g_dbus_object_skeleton_add_interface(G_DBUS_OBJECT_SKELETON(self),
                                         G_DBUS_INTERFACE_SKELETON(self->pv->service_iface));
}

static void
report_service_constructed(GObject *obj)
{
    G_OBJECT_CLASS(report_service_parent_class)->constructed(obj);
}

static void
report_service_class_init(ReportServiceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->constructed = report_service_constructed;

    g_type_class_add_private(klass, sizeof (ReportServicePrivate));
}

ReportService *report_service_new(const gchar *object_path)
{
    gpointer object = g_object_new(REPORT_TYPE_SERVICE,
                                   "g-object-path", object_path,
                                   NULL);

    return static_cast<ReportService *>(object);
}
