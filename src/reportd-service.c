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

#include <dump_dir.h>
#include <run_event.h>
#include <workflow.h>

struct _ReportdService
{
    GDBusObjectSkeleton parent;

    ReportdDaemon *daemon;

    ReportdDbusService *service_iface;
    GHashTable *workflows;
    GDBusProxy *session_proxy;
    GHashTable *tasks;
};

G_DEFINE_TYPE(ReportdService, reportd_service, G_TYPE_DBUS_OBJECT_SKELETON)

enum
{
    PROP_0,
    PROP_DAEMON,
    N_PROPERTIES,
};

static GParamSpec *properties[N_PROPERTIES];

static GDBusProxy *
reportd_service_get_session_proxy (ReportdService  *self,
                                   GError         **error)
{
    g_autoptr (GDBusConnection) connection = NULL;
    g_autoptr (GDBusProxy) proxy = NULL;
    g_autoptr (GVariant) tuple = NULL;
    const char *session_path;

    if (NULL != self->session_proxy)
    {
        return self->session_proxy;
    }

    reportd_daemon_get_bus_connections (self->daemon, &connection, NULL);

    g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), NULL);

    proxy = g_dbus_proxy_new_sync (connection,
                                   G_DBUS_PROXY_FLAGS_NONE,
                                   NULL,
                                   "org.freedesktop.problems",
                                   "/org/freedesktop/Problems2",
                                   "org.freedesktop.Problems2",
                                   NULL, error);
    if (NULL == proxy)
    {
        return NULL;
    }
    tuple = g_dbus_proxy_call_sync (proxy, "GetSession", NULL,
                                    G_DBUS_CALL_FLAGS_NONE,
                                    -1, NULL, error);
    if (NULL == tuple)
    {
        return NULL;
    }

    g_variant_get_child (tuple, 0, "&o", &session_path);

    self->session_proxy = g_dbus_proxy_new_sync (connection,
                                                 G_DBUS_PROXY_FLAGS_NONE,
                                                 NULL,
                                                 "org.freedesktop.problems",
                                                 session_path,
                                                 "org.freedesktop.Problems2.Session",
                                                 NULL, error);

    return self->session_proxy;
}

static void
reportd_service_unexport_task (gpointer data,
                               gpointer user_data)
{
    ReportdTask *task;
    ReportdDaemon *daemon;

    task = REPORTD_TASK (data);
    daemon = REPORTD_DAEMON (user_data);

    reportd_daemon_unregister_object (daemon, G_DBUS_OBJECT (task));
}

typedef struct
{
    ReportdService *service;
    unsigned int bus_name_watcher_id;
} ReportdServiceBusNameWatcherData;

static void
reportd_service_on_name_vanished (GDBusConnection *connection,
                                  const char      *name,
                                  gpointer         user_data)
{
    ReportdServiceBusNameWatcherData *data;
    GPtrArray *task_array;

    data = user_data;
    task_array = g_hash_table_lookup (data->service->tasks, name);

    if (NULL != task_array)
    {
        g_ptr_array_foreach (task_array, reportd_service_unexport_task,
                             data->service->daemon);
    }

    g_hash_table_remove (data->service->tasks, name);

    g_clear_handle_id (&data->bus_name_watcher_id, g_bus_unwatch_name);
    g_clear_object (&data->service);

    g_free (data);
}

static bool
reportd_service_handle_create_task (ReportdDbusService    *object,
                                    GDBusMethodInvocation *invocation,
                                    const char            *arg_workflow,
                                    const char            *arg_problem,
                                    gpointer               user_data)
{
    ReportdService *self;
    workflow_t *workflow;
    g_autoptr (ReportdTask) task = NULL;
    const char *object_path;
    GVariant *variant;
    g_autoptr (GDBusConnection) connection = NULL;
    const char *sender;
    ReportdServiceBusNameWatcherData *data;
    GPtrArray *task_array;

    self = REPORTD_SERVICE (user_data);
    workflow = g_hash_table_lookup (self->workflows, arg_workflow);
    if (NULL == workflow)
    {
        g_dbus_method_invocation_return_error (invocation,
                                               G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                                               "Creating task failed: unknown workflow “%s”",
                                               arg_workflow);
        return true;
    }

    g_debug ("Creating task for problem “%s”", arg_problem);

    task = reportd_task_new (self->daemon, REPORTD_DBUS_TASK_PATH, arg_problem, workflow);
    object_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (task));
    variant = g_variant_new ("(o)", object_path);
    connection = g_dbus_method_invocation_get_connection (invocation);
    sender = g_dbus_method_invocation_get_sender (invocation);
    data = g_new0 (ReportdServiceBusNameWatcherData, 1);

    data->service = g_object_ref (self);
    data->bus_name_watcher_id = g_bus_watch_name_on_connection (connection,
                                                                sender,
                                                                G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                                NULL,
                                                                reportd_service_on_name_vanished,
                                                                data, NULL);

    task_array = g_hash_table_lookup (self->tasks, sender);
    if (NULL == task_array)
    {
        task_array = g_ptr_array_new ();

        g_hash_table_insert (self->tasks, g_strdup (sender), task_array);
    }

    g_ptr_array_add (task_array, task);

    g_dbus_method_invocation_return_value (invocation, variant);

    return true;
}

static bool
reportd_service_handle_get_workflows (ReportdDbusService    *object,
                                      GDBusMethodInvocation *invocation,
                                      const char            *arg_problem,
                                      gpointer               user_data)
{
    ReportdService *self;
    g_autoptr (GError) error = NULL;
    g_autofree char *problem_directory = NULL;
    g_autoptr (GList) workflows = NULL;
    g_autoptr (GVariantBuilder) builder = NULL;
    GVariant *tuple;

    self = REPORTD_SERVICE (user_data);
    problem_directory = reportd_daemon_get_problem_directory (self->daemon,
                                                              arg_problem,
                                                              &error);
    if (NULL == problem_directory)
    {
        g_dbus_method_invocation_return_gerror (invocation, error);

        return true;
    }

    g_debug ("Getting workflows for problem directory “%s”", problem_directory);

    workflows = list_possible_events_glist (problem_directory, "workflow");
    builder = g_variant_builder_new (G_VARIANT_TYPE ("(a(sss))"));

    g_variant_builder_open (builder, G_VARIANT_TYPE ("a(sss)"));

    for (GList *l = workflows; NULL != l; l = l->next)
    {
        g_autofree char *workflow_name = NULL;
        workflow_t *workflow;

        workflow_name = l->data;
        workflow = g_hash_table_lookup (self->workflows, workflow_name);
        if (NULL == workflow)
        {
            g_debug ("Possible workflow without configuration: %s", workflow_name);

            continue;
        }

        g_variant_builder_add (builder,
                               "(sss)",
                               wf_get_name (workflow),
                               wf_get_screen_name (workflow),
                               wf_get_description (workflow));
    }

    g_variant_builder_close (builder);

    tuple = g_variant_builder_end (builder);

    g_dbus_method_invocation_return_value (invocation, tuple);

    return true;
}

static void
reportd_service_on_session_proxy_g_signal (GDBusProxy *proxy,
                                           char       *sender_name,
                                           char       *signal_name,
                                           GVariant   *parameters,
                                           gpointer    user_data)
{
    GDBusMethodInvocation *invocation;
    int status;

    invocation = G_DBUS_METHOD_INVOCATION (user_data);

    if (g_strcmp0 (signal_name, "AuthorizationChanged") != 0)
    {
        return;
    }

    g_variant_get_child (parameters, 0, "i", &status);

    switch (status)
    {
        case 0:
        {
            g_dbus_method_invocation_return_value (invocation, NULL);
        }
        break;

        case 1:
        {
            return;
        }
        break;

        default:
        {
            g_dbus_method_invocation_return_error (invocation,
                                                   G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                                                   "Authorization has been lost or it failed");
        }
    }

    g_signal_handlers_disconnect_by_func (proxy,
                                          reportd_service_on_session_proxy_g_signal,
                                          user_data);
}

static bool
reportd_service_handle_authorize_problems_session (ReportdDbusService    *object,
                                                   GDBusMethodInvocation *invocation,
                                                   int                    flags,
                                                   gpointer               user_data)
{
    ReportdService *self;
    GDBusProxy *session_proxy;
    g_autoptr (GError) error = NULL;
    g_autoptr (GVariant) tuple = NULL;
    int result;

    self = REPORTD_SERVICE (user_data);
    session_proxy = reportd_service_get_session_proxy (self, &error);
    if (NULL == session_proxy)
    {
        g_dbus_method_invocation_return_gerror (invocation, error);
    }

    g_signal_connect (session_proxy, "g-signal",
                      G_CALLBACK (reportd_service_on_session_proxy_g_signal),
                      invocation);

    tuple = g_dbus_proxy_call_sync (session_proxy,
                                    "Authorize", NULL,
                                    G_DBUS_CALL_FLAGS_NONE, -1, NULL,
                                    &error);
    if (NULL == tuple)
    {
        g_dbus_method_invocation_return_gerror (invocation, error);

        return true;
    }

    g_variant_get_child (tuple, 0, "i", &result);

    switch (result)
    {
        case -1:
        {
            g_dbus_method_invocation_return_error (invocation,
                                                   G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                                                   "Authorization failed");
        }
        break;

        case 0:
        {
            g_dbus_method_invocation_return_value (invocation, NULL);
        }
        break;

        case 1:
        {
            return true;
        }
        break;

        case 2:
        {
            g_dbus_method_invocation_return_error (invocation,
                                                   G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                                                   "Authorization request already pending");
        }
        break;

        default:
        {
        }
    }

    g_signal_handlers_disconnect_by_func (session_proxy,
                                          reportd_service_on_session_proxy_g_signal,
                                          invocation);

    return true;
}

static void
reportd_service_init (ReportdService *self)
{
    self->service_iface = reportd_dbus_service_skeleton_new ();
    self->workflows = load_workflow_config_data (NULL);
    self->tasks = g_hash_table_new_full (g_str_hash, g_str_equal,
                                         g_free, (GDestroyNotify) g_ptr_array_unref);

    g_signal_connect (self->service_iface,
                      "handle-create-task",
                      G_CALLBACK (reportd_service_handle_create_task),
                      self);

    g_signal_connect (self->service_iface,
                      "handle-get-workflows",
                      G_CALLBACK (reportd_service_handle_get_workflows),
                      self);

    g_signal_connect (self->service_iface,
                      "handle-authorize-problems-session",
                      G_CALLBACK (reportd_service_handle_authorize_problems_session),
                      self);

    g_dbus_object_skeleton_add_interface (G_DBUS_OBJECT_SKELETON (self),
                                          G_DBUS_INTERFACE_SKELETON (self->service_iface));
}

static void
reportd_service_set_property (GObject      *object,
                              unsigned int  property_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    ReportdService *self;

    self = REPORTD_SERVICE (object);

    switch (property_id)
    {
        case PROP_DAEMON:
        {
            ReportdDaemon *daemon;

            daemon = g_value_get_object (value);

            g_set_object (&self->daemon, daemon);
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        }
    }
}

static void
reportd_service_get_property (GObject      *object,
                              unsigned int  property_id,
                              GValue       *value,
                              GParamSpec   *pspec)
{
    ReportdService *self;

    self = REPORTD_SERVICE (object);

    switch (property_id)
    {
        case PROP_DAEMON:
        {
            g_value_set_object (value, self->daemon);
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        }
    }
}

static void
reportd_service_dispose (GObject *object)
{
    ReportdService *self;

    self = REPORTD_SERVICE (object);

    g_clear_object (&self->daemon);
    g_clear_object (&self->service_iface);
    g_clear_object (&self->session_proxy);

    G_OBJECT_CLASS (reportd_service_parent_class)->dispose (object);
}

static void
reportd_service_finalize (GObject *object)
{
    ReportdService *self;

    self = REPORTD_SERVICE (object);

    g_clear_pointer (&self->workflows, g_hash_table_destroy);

    G_OBJECT_CLASS (reportd_service_parent_class)->finalize (object);
}

static void
reportd_service_constructed (GObject *object)
{
    ReportdService *self;

    self = REPORTD_SERVICE (object);

    reportd_daemon_register_object (self->daemon, G_DBUS_OBJECT_SKELETON (self));

    G_OBJECT_CLASS (reportd_service_parent_class)->constructed (object);
}

static void
reportd_service_class_init (ReportdServiceClass *klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = reportd_service_set_property;
    object_class->get_property = reportd_service_get_property;
    object_class->dispose = reportd_service_dispose;
    object_class->finalize = reportd_service_finalize;
    object_class->constructed = reportd_service_constructed;

    properties[PROP_DAEMON] = g_param_spec_object ("daemon", "Daemon",
                                                   "The owning daemon instance",
                                                   REPORTD_TYPE_DAEMON,
                                                   (G_PARAM_READWRITE |
                                                    G_PARAM_CONSTRUCT_ONLY |
                                                    G_PARAM_STATIC_STRINGS));

    g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

ReportdService *
reportd_service_new (ReportdDaemon *daemon,
                     const char    *object_path)
{
    return g_object_new (REPORTD_TYPE_SERVICE,
                         "daemon", daemon,
                         "g-object-path", object_path,
                         NULL);
}
