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

#include "report-task.h"
#include "report-daemon.h"
#include "report-dbus-generated.h"

#include <report-dbus-constants.h>

#include <workflow.h>
#include <client.h>
#include <internal_libreport.h>
#include <run_event.h>

struct _ReportTask
{
    GDBusObjectSkeleton parent;

    ReportDbusTask   *task_iface;
    gchar            *problem_path;
    workflow_t       *workflow;
};

G_DEFINE_TYPE(ReportTask, report_task, G_TYPE_DBUS_OBJECT_SKELETON)

typedef enum
{
    ASK,
    ASK_YES_NO,
    ASK_YES_NO_YESFOREVER,
    ASK_YES_NO_SAVE,
    ASK_PASSWORD,
} PromptType;

/*** Event running ***/

static char *
do_log2(char *log_line, void *param)
{
    ReportTask *self = REPORT_TASK(param);
    report_dbus_task_emit_progress(self->task_iface, log_line);
    return log_line;
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

int run_event_chain(struct run_event_state *run_state, const char *dump_dir_name, GList *chain)
{
    int retval = 0;
    for (GList *eitem = chain; eitem; eitem = g_list_next(eitem))
    {
        const char *event_name = (const char *)eitem->data;
        retval = run_event_on_dir_name_batch(run_state, dump_dir_name, event_name);

        if (retval < 0 || retval != 0)
            /* Nothing was run (bad backtrace, user declined, etc... */
            break;

        if (retval == 0 && run_state->children_count == 0)
        {
            printf("Error: no processing is specified for event '%s'\n", event_name);
            retval = 1;
        }
    }

    return retval;
}

static gboolean
report_task_prompt_handle_commit(ReportDbusTaskPrompt  *object,
                                 GDBusMethodInvocation *invocation,
                                 gpointer               user_data)
{
    g_object_set_data(G_OBJECT(object), "handled", GINT_TO_POINTER(1));

    return TRUE;
}

static ReportDbusTaskPrompt *
report_task_emit_prompt(ReportTask *self,
                        const char *message,
                        PromptType  type)
{
    g_autoptr(GDBusObjectSkeleton) prompt_skeleton = NULL;
    ReportDbusTaskPrompt *prompt_interface = NULL;
    const char *object_path;

    prompt_skeleton = g_dbus_object_skeleton_new(REPORTD_DBUS_TASK_PROMPT_PATH);
    prompt_interface = report_dbus_task_prompt_skeleton_new();

    g_dbus_object_skeleton_add_interface(prompt_skeleton,
                                         G_DBUS_INTERFACE_SKELETON(prompt_interface));

    ReportDaemon::inst().register_object(prompt_skeleton);

    object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(prompt_skeleton));

    g_signal_connect(prompt_interface, "handle-commit",
                     G_CALLBACK(report_task_prompt_handle_commit), NULL);

    report_dbus_task_emit_prompt(self->task_iface, object_path, message, type);

    while (g_object_get_data(G_OBJECT(prompt_interface), "handled") != GINT_TO_POINTER(1))
    {
        g_main_context_iteration(NULL, TRUE);
    }

    g_object_set_data(G_OBJECT(prompt_interface), "handled", NULL);

    ReportDaemon::inst().unregister_object(G_DBUS_OBJECT(prompt_skeleton));

    return prompt_interface;
}

static char *
report_task_ask_callback (const char *msg,
                          void       *interaction_param)
{
    ReportTask *self;
    g_autoptr(ReportDbusTaskPrompt) prompt_interface = NULL;
    const char *input;

    self = REPORT_TASK(interaction_param);
    prompt_interface = report_task_emit_prompt(self, msg, ASK);
    input = report_dbus_task_prompt_get_input(prompt_interface);

    return g_strdup(input);
}

static int
report_task_ask_yes_no_callback(const char *msg,
                                void       *interaction_param)
{
    ReportTask *self;
    g_autoptr(ReportDbusTaskPrompt) prompt_interface = NULL;

    self = REPORT_TASK(interaction_param);
    prompt_interface = report_task_emit_prompt(self, msg, ASK_YES_NO);

    return report_dbus_task_prompt_get_response(prompt_interface);
}

static int
report_task_ask_yes_no_yesforever_callback(const char *key,
                                           const char *msg,
                                           void       *interaction_param)
{
    const char *value;
    ReportTask *self;
    g_autoptr(ReportDbusTaskPrompt) prompt_interface = NULL;
    bool response;
    bool remember;

    value = get_user_setting(key);
    /* The following is replicating the madness inside libreport, where
     * “no” means “yes, forever”, and “yes” means nothing, really.
     *
     * Yes (no?), each implementation has a similar comment.
     */
    if (value != NULL && !string_to_bool(value))
    {
        return TRUE;
    }
    self = REPORT_TASK(interaction_param);
    prompt_interface = report_task_emit_prompt(self, msg, ASK_YES_NO_YESFOREVER);
    response = report_dbus_task_prompt_get_response(prompt_interface);
    remember = report_dbus_task_prompt_get_remember(prompt_interface);
    if (remember && !response)
    {
        set_user_setting(key, "no");
    }

    return response;
}

static int
report_task_ask_yes_no_save_result_callback(const char *key,
                                            const char *msg,
                                            void       *interaction_param)
{
    const char *value;
    ReportTask *self;
    g_autoptr(ReportDbusTaskPrompt) prompt_interface = NULL;
    bool response;
    bool remember;

    value = get_user_setting(key);
    if (value != NULL)
    {
        return string_to_bool(value);
    }
    self = REPORT_TASK(interaction_param);
    prompt_interface = report_task_emit_prompt(self, msg, ASK_YES_NO_SAVE);
    response = report_dbus_task_prompt_get_response(prompt_interface);
    remember = report_dbus_task_prompt_get_remember(prompt_interface);
    if (remember)
    {
        value = response? "yes" : "no";

        set_user_setting(key, value);
    }

    return response;
}

static char *
report_task_ask_password_callback(const char *msg,
                                  void       *interaction_param)
{
    ReportTask *self;
    g_autoptr(ReportDbusTaskPrompt) prompt_interface = NULL;
    const char *password;

    self = REPORT_TASK(interaction_param);
    prompt_interface = report_task_emit_prompt(self, msg, ASK_PASSWORD);
    password = report_dbus_task_prompt_get_input(prompt_interface);

    return g_strdup(password);
}

static gboolean
report_task_handle_start(ReportDbusTask        *object,
                         GDBusMethodInvocation *invocation,
                         ReportTask            *self)
{
    GList *event_names = wf_get_event_names(self->workflow);

    report_dbus_task_set_status(self->task_iface, "RUNNING");
    g_debug("Started task: %s", self->problem_path);

    std::string problem_dir{ReportDaemon::inst().get_problem_directory(self->problem_path)};

    struct run_event_state *run_state = new_run_event_state();
    run_state->logging_callback = do_log2;
    run_state->logging_param = self;
    run_state->interaction_param = self;
    run_state->ask_callback = report_task_ask_callback;
    run_state->ask_yes_no_callback = report_task_ask_yes_no_callback;
    run_state->ask_yes_no_yesforever_callback = report_task_ask_yes_no_yesforever_callback;
    run_state->ask_yes_no_save_result_callback = report_task_ask_yes_no_save_result_callback;
    run_state->ask_password_callback = report_task_ask_password_callback;

    run_event_chain(run_state, problem_dir.c_str(), event_names);

    free_run_event_state(run_state);

    g_list_free_full(event_names, free);

    ReportDaemon::inst().push_problem_directory(problem_dir);
    report_dbus_task_set_status(self->task_iface, "FINISHED");
    g_debug("Finished task: %s", self->problem_path);

    g_dbus_method_invocation_return_value(invocation, g_variant_new("()"));
    return TRUE;
}

static gboolean
report_task_handle_cancel(ReportDbusTask        * /*object*/,
                          GDBusMethodInvocation *invocation,
                          ReportTask            *self)
{
    g_debug("Canceled task: %s", self->problem_path);
    g_dbus_method_invocation_return_value(invocation, g_variant_new("()"));
    return TRUE;
}

static void
report_task_init(ReportTask *self)
{
    self->task_iface = report_dbus_task_skeleton_new();

    g_signal_connect(self->task_iface, "handle-start",
                     G_CALLBACK (report_task_handle_start), self);

    g_signal_connect(self->task_iface, "handle-cancel",
                     G_CALLBACK (report_task_handle_cancel), self);

    g_dbus_object_skeleton_add_interface(G_DBUS_OBJECT_SKELETON(self),
                                         G_DBUS_INTERFACE_SKELETON(self->task_iface));
}

static void
report_task_constructed(GObject *obj)
{
    ReportTask *self = REPORT_TASK(obj);

    G_OBJECT_CLASS(report_task_parent_class)->constructed(obj);

    /* The dbus version property of the task */
    report_dbus_task_set_status(self->task_iface, "NEW");
}

static void
report_task_dispose(GObject *obj)
{
    ReportTask *self = REPORT_TASK(obj);

    g_free(self->problem_path);

    G_OBJECT_CLASS(report_task_parent_class)->finalize(obj);
}

static void
report_task_class_init(ReportTaskClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->constructed = report_task_constructed;
    object_class->dispose     = report_task_dispose;
}

ReportTask *report_task_new(const gchar      *object_path,
                            const gchar      *problem_path,
                            workflow_t       *workflow)
{
    gpointer object = g_object_new(REPORT_TYPE_TASK,
                                   "g-object-path", object_path,
                                   NULL);

    ReportTask *task = static_cast<ReportTask *>(object);
    task->problem_path = g_strdup(problem_path);
    task->workflow  = workflow;

    return task;
}
