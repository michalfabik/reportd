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

#include "reportd.h"

#include "reportd-dbus-generated.h"

#include <client.h>
#include <internal_libreport.h>
#include <run_event.h>
#include <signal.h>
#include <workflow.h>

typedef enum
{
    REPORTD_TASK_STATE_READY,
    REPORTD_TASK_STATE_RUNNING,
    REPORTD_TASK_STATE_COMPLETED,
    REPORTD_TASK_STATE_FAILED,
    REPORTD_TASK_STATE_CANCELED,
} ReportdTaskState;

struct _ReportdTask
{
    GDBusObjectSkeleton parent;

    ReportdDaemon *daemon;

    ReportdDbusTask *task_iface;
    gchar *problem_path;
    workflow_t *workflow;
    struct run_event_state *run_state;

    GCancellable *cancellable;

    GCond prompt_cond;
    GMutex prompt_mutex;
};

G_DEFINE_QUARK (reportd-task-error-quark, reportd_task_error)
G_DEFINE_TYPE (ReportdTask, reportd_task, G_TYPE_DBUS_OBJECT_SKELETON)

enum
{
    PROP_0,
    PROP_DAEMON,
    PROP_PROBLEM_PATH,
    PROP_WORKFLOW,
    N_PROPERTIES,
};

typedef enum
{
    ASK,
    ASK_YES_NO,
    ASK_YES_NO_YESFOREVER,
    ASK_YES_NO_SAVE,
    ASK_PASSWORD,
} PromptType;

static GParamSpec *properties[N_PROPERTIES];

/*** Event running ***/

static char *
do_log2 (char *log_line,
         void *param)
{
    ReportdTask *self;

    self = REPORTD_TASK (param);

    reportd_dbus_task_emit_progress (self->task_iface, log_line);

    return log_line;
}

static void
reportd_task_error_callback (const char *error_line,
                             void       *param)
{
    ReportdTask *self;

    self = REPORTD_TASK (param);

    reportd_dbus_task_emit_progress (self->task_iface, error_line);
}

static int
export_config_and_run_event (struct run_event_state *state,
                             const char             *dump_dir_name,
                             const char             *event)
{
    GList *env_list;
    int retval;

    env_list = export_event_config (event);

    prepare_commands (state);

    while (spawn_next_command (state, dump_dir_name, event, EXECFLG_SETPGID) >= 0)
    {
        retval = consume_event_command_output (state, dump_dir_name);
        if (0 != retval)
        {
            break;
        }
    }

    free_commands (state);
    unexport_event_config (env_list);

    return retval;
}

static bool
reportd_task_run_event_chain (ReportdTask             *self,
                              const char              *dump_dir_name,
                              GList                   *chain,
                              GError                 **error)
{
    for (GList *l = chain; NULL != l; l = l->next)
    {
        const char *event_name;
        int exit_code;
        const struct
        {
            const char *event_name;
            int quirk_code;
            int quirk_mapping;
        } quirks[] =
        {
            /* For some reason, abrt-action-ureport exits if it detects a
             * Bugzilla report…
             */
            { "report_uReport", 70, 0 },
        };

        event_name = l->data;

        if (g_cancellable_set_error_if_cancelled (self->cancellable, error))
        {
            return false;
        }

        exit_code = export_config_and_run_event (self->run_state, dump_dir_name, event_name);

        if (g_cancellable_set_error_if_cancelled (self->cancellable, error))
        {
            return false;
        }

        for (int i = 0; i < G_N_ELEMENTS (quirks); i++)
        {
            if (g_strcmp0 (quirks[i].event_name, event_name) == 0 &&
                quirks[i].quirk_code == exit_code)
            {
                g_message ("Correcting quirk: event “%s” exited with code %d; replacing with %d",
                           event_name, exit_code, quirks[i].quirk_mapping);

                exit_code = quirks[i].quirk_mapping;
            }
        }

        if (0 != exit_code)
        {
            /* Nothing was run (bad backtrace, user declined, etc... */
            g_set_error (error, REPORTD_TASK_ERROR, REPORTD_TASK_ERROR_EVENT_HANDLER_FAILED,
                         "Event %s handler exited with code %d", event_name, exit_code);

            return false;
        }
        else if (0 == self->run_state->children_count)
        {
            g_warning ("No processing specified for event “%s”", event_name);

            g_set_error (error, REPORTD_TASK_ERROR, REPORTD_TASK_ERROR_NO_EVENT_HANDLERS,
                         "Event %s has no handlers defined", event_name);

            return false;
        }
    }

    return true;
}

static bool
reportd_task_prompt_handle_commit (ReportdDbusTaskPrompt *object,
                                   GDBusMethodInvocation *invocation,
                                   gpointer               user_data)
{
    ReportdTask *self;

    self = REPORTD_TASK (user_data);

    g_object_set_data (G_OBJECT (object), "handled", GINT_TO_POINTER (1));

    g_mutex_lock (&self->prompt_mutex);
    g_cond_signal (&self->prompt_cond);
    g_mutex_unlock (&self->prompt_mutex);

    return true;
}

static ReportdDbusTaskPrompt *
reportd_task_emit_prompt (ReportdTask *self,
                          const char  *message,
                          PromptType   type)
{
    g_autoptr(GDBusObjectSkeleton) prompt_skeleton = NULL;
    ReportdDbusTaskPrompt *prompt_interface = NULL;
    const char *object_path;

    prompt_skeleton = g_dbus_object_skeleton_new (REPORTD_DBUS_TASK_PROMPT_PATH);
    prompt_interface = reportd_dbus_task_prompt_skeleton_new ();

    g_dbus_object_skeleton_add_interface (prompt_skeleton,
                                          G_DBUS_INTERFACE_SKELETON (prompt_interface));

    reportd_daemon_register_object (self->daemon, prompt_skeleton);

    object_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (prompt_skeleton));

    g_signal_connect (prompt_interface, "handle-commit",
                      G_CALLBACK (reportd_task_prompt_handle_commit), self);

    reportd_dbus_task_emit_prompt (self->task_iface, object_path, message, type);

    g_mutex_lock (&self->prompt_mutex);

    while (g_object_get_data (G_OBJECT (prompt_interface), "handled") != GINT_TO_POINTER (1))
    {
        gint64 end_time;

        end_time = g_get_monotonic_time () + 250 * G_TIME_SPAN_MILLISECOND;

        if (g_cancellable_is_cancelled (self->cancellable))
        {
            break;
        }

        g_cond_wait_until (&self->prompt_cond, &self->prompt_mutex, end_time);
    }

    g_mutex_unlock (&self->prompt_mutex);

    g_object_set_data (G_OBJECT (prompt_interface), "handled", NULL);

    reportd_daemon_unregister_object (self->daemon, G_DBUS_OBJECT (prompt_skeleton));

    return prompt_interface;
}

static char *
reportd_task_ask_callback (const char *msg,
                           void       *interaction_param)
{
    ReportdTask *self;
    g_autoptr (ReportdDbusTaskPrompt) prompt_interface = NULL;
    const char *input;

    self = REPORTD_TASK (interaction_param);
    prompt_interface = reportd_task_emit_prompt (self, msg, ASK);
    if (g_cancellable_is_cancelled (self->cancellable))
    {
        return NULL;
    }
    input = reportd_dbus_task_prompt_get_input (prompt_interface);

    return g_strdup (input);
}

static int
reportd_task_ask_yes_no_callback (const char *msg,
                                  void       *interaction_param)
{
    ReportdTask *self;
    g_autoptr (ReportdDbusTaskPrompt) prompt_interface = NULL;

    self = REPORTD_TASK (interaction_param);
    prompt_interface = reportd_task_emit_prompt (self, msg, ASK_YES_NO);
    if (g_cancellable_is_cancelled (self->cancellable))
    {
        return -1;
    }

    return reportd_dbus_task_prompt_get_response (prompt_interface);
}

static int
reportd_task_ask_yes_no_yesforever_callback (const char *key,
                                             const char *msg,
                                             void       *interaction_param)
{
    const char *value;
    ReportdTask *self;
    g_autoptr (ReportdDbusTaskPrompt) prompt_interface = NULL;
    bool response;
    bool remember;

    value = get_user_setting (key);
    /* The following is replicating the madness inside libreport, where
     * “no” means “yes, forever”, and “yes” means nothing, really.
     *
     * Yes (no?), each implementation has a similar comment.
     */
    if (value != NULL && !string_to_bool(value))
    {
        return TRUE;
    }
    self = REPORTD_TASK (interaction_param);
    prompt_interface = reportd_task_emit_prompt (self, msg, ASK_YES_NO_YESFOREVER);
    if (g_cancellable_is_cancelled (self->cancellable))
    {
        return -1;
    }
    response = reportd_dbus_task_prompt_get_response (prompt_interface);
    remember = reportd_dbus_task_prompt_get_remember (prompt_interface);
    if (remember && !response)
    {
        set_user_setting (key, "no");
    }

    return response;
}

static int
reportd_task_ask_yes_no_save_result_callback (const char *key,
                                              const char *msg,
                                              void       *interaction_param)
{
    const char *value;
    ReportdTask *self;
    g_autoptr (ReportdDbusTaskPrompt) prompt_interface = NULL;
    bool response;
    bool remember;

    value = get_user_setting (key);
    if (value != NULL)
    {
        return string_to_bool (value);
    }
    self = REPORTD_TASK (interaction_param);
    prompt_interface = reportd_task_emit_prompt (self, msg, ASK_YES_NO_SAVE);
    if (g_cancellable_is_cancelled (self->cancellable))
    {
        return -1;
    }
    response = reportd_dbus_task_prompt_get_response (prompt_interface);
    remember = reportd_dbus_task_prompt_get_remember (prompt_interface);
    if (remember)
    {
        value = response? "yes" : "no";

        set_user_setting (key, value);
    }

    return response;
}

static char *
reportd_task_ask_password_callback (const char *msg,
                                    void       *interaction_param)
{
    ReportdTask *self;
    g_autoptr (ReportdDbusTaskPrompt) prompt_interface = NULL;
    const char *password;

    self = REPORTD_TASK (interaction_param);
    prompt_interface = reportd_task_emit_prompt (self, msg, ASK_PASSWORD);
    if (g_cancellable_is_cancelled (self->cancellable))
    {
        return NULL;
    }
    password = reportd_dbus_task_prompt_get_input (prompt_interface);

    return g_strdup (password);
}

static void
reportd_task_on_finished (GObject      *source_object,
                          GAsyncResult *res,
                          gpointer      user_data)
{
    GTask *task;
    gpointer task_data;
    ReportdTask *self;
    GError *error = NULL;
    GDBusMethodInvocation *invocation;
    ReportdDbusTask *proxy;

    task = G_TASK (res);
    task_data = g_task_get_task_data (task);
    self = REPORTD_TASK (task_data);
    invocation = G_DBUS_METHOD_INVOCATION (user_data);
    proxy = REPORTD_DBUS_TASK (source_object);

    (void) g_task_propagate_boolean (task, &error);

    if (g_task_had_error (task))
    {
        GCancellable *cancellable;

        cancellable = g_task_get_cancellable (task);

        g_message ("Task %s finished with an error: %s",
                   self->problem_path, error->message);

        if (g_cancellable_is_cancelled (cancellable))
        {
            reportd_dbus_task_set_status (proxy, REPORTD_TASK_STATE_CANCELED);
        }
        else
        {
            reportd_dbus_task_set_status (proxy, REPORTD_TASK_STATE_FAILED);
        }

        g_dbus_method_invocation_return_gerror (invocation, error);

        return;
    }

    g_message ("Task %s finished successfully", self->problem_path);

    reportd_dbus_task_set_status (proxy, REPORTD_TASK_STATE_COMPLETED);

    reportd_dbus_task_complete_start (proxy, invocation);
}

static void
reportd_task_start (GTask        *task,
                    gpointer      source_object,
                    gpointer      task_data,
                    GCancellable *cancellable)
{
    ReportdTask *self;
    const char *workflow_name;
    char *workflow_env;
    GError *error = NULL;
    g_autofree char *problem_directory = NULL;
    GList *event_names;

    self = REPORTD_TASK (task_data);
    workflow_name = wf_get_name (self->workflow);
    workflow_env = g_strdup_printf ("LIBREPORT_WORKFLOW=%s", workflow_name);
    problem_directory = reportd_daemon_get_problem_directory (self->daemon,
                                                              self->problem_path,
                                                              &error);
    if (NULL == problem_directory)
    {
        g_task_return_error (task, error);

        return;
    }
    event_names = wf_get_event_names (self->workflow);

    self->run_state = new_run_event_state ();

    self->run_state->logging_callback = do_log2;
    self->run_state->logging_param = self;
    self->run_state->error_callback = reportd_task_error_callback;
    self->run_state->error_param = self;
    self->run_state->interaction_param = self;
    self->run_state->ask_callback = reportd_task_ask_callback;
    self->run_state->ask_yes_no_callback = reportd_task_ask_yes_no_callback;
    self->run_state->ask_yes_no_yesforever_callback = reportd_task_ask_yes_no_yesforever_callback;
    self->run_state->ask_yes_no_save_result_callback = reportd_task_ask_yes_no_save_result_callback;
    self->run_state->ask_password_callback = reportd_task_ask_password_callback;

    g_ptr_array_add (self->run_state->extra_environment, workflow_env);

    g_message ("Starting task “%s”", self->problem_path);

    if (g_task_return_error_if_cancelled (task))
    {
        goto cleanup;
    }

    reportd_dbus_task_set_status (self->task_iface, REPORTD_TASK_STATE_RUNNING);

    if (!reportd_task_run_event_chain (self, problem_directory, event_names, &error))
    {
        g_task_return_error (task, error);

        goto cleanup;
    }

    if (g_task_return_error_if_cancelled (task))
    {
        goto cleanup;
    }

    g_clear_error (&error);

    if (!reportd_daemon_push_problem_directory (self->daemon, problem_directory, &error))
    {
        g_task_return_error (task, error);

        goto cleanup;
    }

    g_task_return_boolean (task, true);

cleanup:
    g_clear_pointer (&self->run_state, free_run_event_state);
    g_list_free_full (event_names, g_free);
}

static bool
reportd_task_handle_start (ReportdDbusTask       *object,
                           GDBusMethodInvocation *invocation,
                           gpointer               user_data)
{
    ReportdTask *self;
    g_autoptr (GTask) task = NULL;
    g_autoptr (GThread) thread = NULL;

    self = REPORTD_TASK (user_data);
    task = g_task_new (object, self->cancellable, reportd_task_on_finished, invocation);

    g_task_set_task_data (task, user_data, NULL);

    g_task_run_in_thread (task, reportd_task_start);

    return true;
}

static bool
reportd_task_handle_cancel (ReportdDbusTask       *object,
                            GDBusMethodInvocation *invocation,
                            gpointer               user_data)
{
    ReportdTask *self;

    self = REPORTD_TASK (user_data);

    g_message ("Canceling task “%s”", self->problem_path);

    g_cancellable_cancel (self->cancellable);

    if (self->run_state->command_pid > 0)
    {
        kill (-self->run_state->command_pid, SIGTERM);
    }

    reportd_dbus_task_complete_cancel (object, invocation);

    return true;
}

static void
reportd_task_init (ReportdTask *self)
{
    self->task_iface = reportd_dbus_task_skeleton_new ();
    self->cancellable = g_cancellable_new ();

    g_cond_init (&self->prompt_cond);
    g_mutex_init (&self->prompt_mutex);

    g_signal_connect (self->task_iface, "handle-start",
                      G_CALLBACK (reportd_task_handle_start), self);
    g_signal_connect (self->task_iface, "handle-cancel",
                      G_CALLBACK (reportd_task_handle_cancel), self);

    g_dbus_object_skeleton_add_interface (G_DBUS_OBJECT_SKELETON (self),
                                          G_DBUS_INTERFACE_SKELETON (self->task_iface));
}

static void
reportd_task_set_property (GObject      *object,
                           unsigned int  property_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
    ReportdTask *self;

    self = REPORTD_TASK (object);

    switch (property_id)
    {
        case PROP_DAEMON:
        {
            ReportdDaemon *daemon;

            daemon = g_value_get_object (value);

            g_set_object (&self->daemon, daemon);
        }
        break;

        case PROP_PROBLEM_PATH:
        {
            self->problem_path = g_value_dup_string (value);
        }
        break;

        case PROP_WORKFLOW:
        {
            self->workflow = g_value_get_pointer (value);
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        }
    }
}

static void
reportd_task_get_property (GObject      *object,
                           unsigned int  property_id,
                           GValue       *value,
                           GParamSpec   *pspec)
{
    ReportdTask *self;

    self = REPORTD_TASK (object);

    switch (property_id)
    {
        case PROP_DAEMON:
        {
            g_value_set_object (value, self->daemon);
        }
        break;

        case PROP_PROBLEM_PATH:
        {
            g_value_set_string (value, self->problem_path);
        }
        break;

        case PROP_WORKFLOW:
        {
            g_value_set_pointer (value, self->workflow);
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        }
    }
}

static void
reportd_task_constructed (GObject *object)
{
    ReportdTask *self;

    self = REPORTD_TASK (object);

    G_OBJECT_CLASS (reportd_task_parent_class)->constructed (object);

    reportd_daemon_register_object (self->daemon, G_DBUS_OBJECT_SKELETON (self));
    reportd_dbus_task_set_status (self->task_iface, REPORTD_TASK_STATE_READY);
}

static void
reportd_task_dispose (GObject *object)
{
    ReportdTask *self;

    self = REPORTD_TASK (object);

    g_clear_object (&self->cancellable);
    g_clear_object (&self->daemon);

    G_OBJECT_CLASS (reportd_task_parent_class)->dispose (object);
}

static void
reportd_task_finalize (GObject *object)
{
    ReportdTask *self;

    self = REPORTD_TASK (object);

    g_clear_pointer (&self->problem_path, g_free);
    g_cond_clear (&self->prompt_cond);
    g_mutex_clear (&self->prompt_mutex);

    G_OBJECT_CLASS (reportd_task_parent_class)->finalize (object);
}

static void
reportd_task_class_init (ReportdTaskClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = reportd_task_set_property;
    object_class->get_property = reportd_task_get_property;
    object_class->constructed = reportd_task_constructed;
    object_class->dispose = reportd_task_dispose;
    object_class->finalize = reportd_task_finalize;

    properties[PROP_DAEMON] = g_param_spec_object ("daemon", "Daemon",
                                                   "The owning daemon instance",
                                                   REPORTD_TYPE_DAEMON,
                                                   (G_PARAM_READWRITE |
                                                    G_PARAM_CONSTRUCT_ONLY |
                                                    G_PARAM_STATIC_STRINGS));
    properties[PROP_PROBLEM_PATH] = g_param_spec_string ("problem-path",
                                                         "Problem Path",
                                                         "Object path to the problem on the message bus",
                                                         NULL,
                                                         (G_PARAM_READWRITE |
                                                          G_PARAM_CONSTRUCT_ONLY |
                                                          G_PARAM_STATIC_STRINGS));
    properties[PROP_WORKFLOW] = g_param_spec_pointer ("workflow", "Workflow",
                                                      "The workflow object for this task",
                                                      (G_PARAM_READWRITE |
                                                       G_PARAM_CONSTRUCT_ONLY |
                                                       G_PARAM_STATIC_STRINGS));

    g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

ReportdTask *
reportd_task_new (ReportdDaemon *daemon,
                  const char    *object_path,
                  const char    *problem_path,
                  workflow_t    *workflow)
{
    return g_object_new (REPORTD_TYPE_TASK,
                         "daemon", daemon,
                         "g-object-path", object_path,
                         "problem-path", problem_path,
                         "workflow", workflow,
                         NULL);
}
