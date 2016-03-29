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
#include "report-daemon.h"

#include <workflow.h>
#include <client.h>
#include <run_event.h>

G_DEFINE_TYPE(ReportTask, report_task, G_TYPE_DBUS_OBJECT_SKELETON);

struct _ReportTaskPrivate
{
    ReportDbusTask   *task_iface;
    gchar            *problem_path;
    workflow_t       *workflow;
};
/*** Event running ***/

struct logging_state {
    bool output_was_produced;
};

static char *
do_log(char *log_line, void *)
{
    client_log(log_line);
    return log_line;
}

static char *
do_log2(char *log_line, void *param)
{
    struct logging_state *l_state = (struct logging_state *)param;
    l_state->output_was_produced |= (log_line[0] != '\0');
    return do_log(log_line, param);
}

static int export_config_and_run_event(
                struct run_event_state *state,
                const char *dump_dir_name,
                const char *event)
{
    /* Export overridden settings as environment variables */
    GList *env_list = export_event_config(event);

    int r = run_event_on_dir_name(state, dump_dir_name, event);

    unexport_event_config(env_list);

    return r;
}

static int run_event_on_dir_name_batch(
                struct run_event_state *state,
                const char *dump_dir_name,
                const char *event_name)
{
    int retval = -1;
    retval = export_config_and_run_event(state, dump_dir_name, event_name);
    return retval;
}

int run_event_chain(const char *dump_dir_name, GList *chain)
{
    struct logging_state l_state;

    struct run_event_state *run_state = new_run_event_state();
    run_state->logging_callback = do_log2;
    run_state->logging_param = &l_state;

    int retval = 0;
    for (GList *eitem = chain; eitem; eitem = g_list_next(eitem))
    {
        l_state.output_was_produced = 0;
        const char *event_name = (const char *)eitem->data;
        retval = run_event_on_dir_name_batch(run_state, dump_dir_name, event_name);

        if (retval < 0)
            /* Nothing was run (bad backtrace, user declined, etc... */
            break;
        if (retval == 0 && run_state->children_count == 0)
        {
            printf("Error: no processing is specified for event '%s'\n", event_name);
            retval = 1;
        }
        else
        /* If program failed, or if it finished successfully without saying anything... */
        if (retval != 0 || !l_state.output_was_produced)
        {
            char *msg = exit_status_as_string(event_name, run_state->process_status);
            fputs(msg, stdout);
            free(msg);
        }
        if (retval != 0)
            break;
    }

    free_run_event_state(run_state);

    return retval;
}

static gboolean
report_task_handle_start(ReportDbusTask        * /*object*/,
                         GDBusMethodInvocation *invocation,
                         ReportTask            *self)
{
    GList *event_names = wf_get_event_names(self->pv->workflow);

    report_dbus_task_set_status(self->pv->task_iface, "RUNNING");

    std::string problem_dir(ReportDaemon::inst().get_problem_directory(self->pv->problem_path));
    run_event_chain(problem_dir.c_str(), event_names);

    g_list_free_full(event_names, free);

    ReportDaemon::inst().push_problem_directory(problem_dir);
    report_dbus_task_set_status(self->pv->task_iface, "FINISHED");

    g_dbus_method_invocation_return_value(invocation, g_variant_new("()"));
    return TRUE;
}

static gboolean
report_task_handle_cancel(ReportDbusTask        * /*object*/,
                          GDBusMethodInvocation *invocation,
                          gpointer               /*user_data*/)
{
    g_message("Canceled task!");
    g_dbus_method_invocation_return_value(invocation, g_variant_new("()"));
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
report_task_dispose(GObject *obj)
{
    ReportTask *self = REPORT_TASK(obj);

    g_free(self->pv->problem_path);

    G_OBJECT_CLASS(report_task_parent_class)->finalize(obj);
}

static void
report_task_class_init(ReportTaskClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->constructed = report_task_constructed;
    object_class->dispose     = report_task_dispose;

    g_type_class_add_private (klass, sizeof (ReportTaskPrivate));
}

ReportTask *report_task_new(const gchar      *object_path,
                            const gchar      *problem_path,
                            workflow_t       *workflow)
{
    gpointer object = g_object_new(REPORT_TYPE_TASK,
                                   "g-object-path", object_path,
                                   NULL);

    ReportTask *task = static_cast<ReportTask *>(object);
    task->pv->problem_path = g_strdup(problem_path);
    task->pv->workflow  = workflow;

    return task;
}
