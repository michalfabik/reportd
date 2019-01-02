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
#include "report-service.h"
#include "report-task.h"
#include "report-daemon.h"
#include "report-dbus-constants.h"
#include "report-dbus-generated.h"

#include <iostream>
#include <cstring>

#include <dump_dir.h>
#include <run_event.h>
#include <workflow.h>

struct _ReportService {
    GDBusObjectSkeleton parent;

    ReportDbusService *service_iface;
    GHashTable        *workflows;
    unsigned long      task_cnt;
    Glib::RefPtr<Gio::DBus::Proxy> problems_session;
};

G_DEFINE_TYPE(ReportService, report_service, G_TYPE_DBUS_OBJECT_SKELETON)

static Glib::RefPtr<Gio::DBus::Proxy>
get_problems_session(ReportService *self)
{
    if (!self->problems_session) {
        auto cancellable = Gio::Cancellable::create();
        auto connection = Gio::DBus::Connection::get_sync(Gio::DBus::BusType::BUS_TYPE_SYSTEM,
                                                          cancellable);
        if (!connection) {
            throw Gio::DBus::Error(Gio::DBus::Error::FAILED,
                                   "Cannot get system bus");
        }

        auto info = Glib::RefPtr<Gio::DBus::InterfaceInfo>();
        auto problems2 = Gio::DBus::Proxy::create_sync(connection,
                                                       "org.freedesktop.problems",
                                                       "/org/freedesktop/Problems2",
                                                       "org.freedesktop.Problems2",
                                                       cancellable,
                                                       info,
                                                       Gio::DBus::PROXY_FLAGS_NONE);
        if (!problems2) {
            throw Gio::DBus::Error(Gio::DBus::Error::FAILED,
                                   "Problems2 not accessible");
        }

        Glib::ustring session_path;
        try {
            auto retval = problems2->call_sync("GetSession",
                                               cancellable);
            auto path = Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring> >(retval.get_child(0));
            session_path = path.get();
        }
        catch ( const Glib::Error &err ) {
            g_warning("Cannot get Problems2 Session: %s", err.what().c_str());
            return {};
        }

        self->problems_session = Gio::DBus::Proxy::create_sync(connection,
                                                               "org.freedesktop.problems",
                                                               session_path,
                                                               "org.freedesktop.Problems2.Session",
                                                               cancellable,
                                                             info,
                                                             Gio::DBus::PROXY_FLAGS_NONE);
        if (!self->problems_session) {
            throw Gio::DBus::Error(Gio::DBus::Error::FAILED,
                                   "Own Problems2 Session not accessible");
        }
    }

    return self->problems_session;
}

static workflow_t *
report_service_find_workflow_by_name(ReportService *self,
                                     const char    *wf_name)
{
    return (workflow_t *)g_hash_table_lookup(self->workflows, (gpointer)wf_name);
}

static gboolean
report_service_handle_create_task(ReportDbusService     * /*object*/,
                                  GDBusMethodInvocation *invocation,
                                  const gchar           *arg_workflow,
                                  const gchar           *arg_problem,
                                  ReportService         *self)
{
    const char *object_path;

    if (self->task_cnt == ULONG_MAX) {
        g_dbus_method_invocation_return_error(invocation,
                G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                "Reportd Service cannot create a new task");
        return TRUE;
    }

    workflow_t *wf = report_service_find_workflow_by_name(self, arg_workflow);
    if (wf == NULL) {
        g_dbus_method_invocation_return_error(invocation,
                G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                "Cannot create a new task from unknown workflow: '%s'",
                arg_workflow);
        return TRUE;
    }

    g_debug("Creating task for problem '%s'", arg_problem);

    ReportTask *t = report_task_new(REPORTD_DBUS_TASK_PATH,
                                    arg_problem,
                                    wf);

    ReportDaemon::inst().register_object(G_DBUS_OBJECT_SKELETON(t));

    object_path = g_dbus_object_get_object_path(G_DBUS_OBJECT(t));

    GVariant *retval = g_variant_new("(o)", object_path);
    g_dbus_method_invocation_return_value(invocation, retval);

    g_object_unref(t);

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
                                              "%s",
                                              err.what().c_str());
        return TRUE;
    }

    g_debug("Getting workflows for problem directory '%s'", problem_dir.c_str());

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

class ReportServicePendingAuthorization
{
    private:
        GDBusMethodInvocation *invocation;
        sigc::connection       connection;

    public:
        ReportServicePendingAuthorization(GDBusMethodInvocation *inv) :
            invocation{inv}
        {}

        void set_connection(sigc::connection connection)
        {
            this->connection = connection;
        }

        void operator()(const Glib::ustring &,
                        const Glib::ustring &signal_name,
                        const Glib::VariantContainerBase &parameters)
        {
            if (signal_name != "AuthorizationChanged") {
                return;
            }

            Glib::VariantBase child_status;

            /* because VariantContianerBase::get_child(int) is non-const */
            parameters.get_child(child_status, 0);

            const auto var_status = Glib::VariantBase::cast_dynamic< Glib::Variant<gint32> >(child_status);
            gint32 status = var_status.get();

            g_debug("AuthorizationChanged %i", status);

            /* Pending Authorization request */
            if (status == 1) {
                return;
            }

            if (status == 0) {
                g_dbus_method_invocation_return_value(this->invocation, g_variant_new("()"));
            }
            else {
                g_dbus_method_invocation_return_error(this->invocation,
                        G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                        "reportd's Problems2 Session authorization request failed");
            }

            this->connection.disconnect();
        }
};


static gboolean
report_service_handle_authorize_problems_session(ReportDbusService     * /*object*/,
                                                 GDBusMethodInvocation *invocation,
                                                 gint32                /*flags*/,
                                                 ReportService         *self)
{
    g_debug("Authorizing reportd's Problems2 Session");

    auto session = get_problems_session(self);

    auto p = ReportServicePendingAuthorization{invocation};
    auto connection = session->signal_signal().connect(p);

    auto cancellable = Gio::Cancellable::create();

    gint32 code;
    try {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));

        const auto parameters = Glib::VariantContainerBase::create_tuple(Glib::VariantBase{g_variant_builder_end(&builder)});
        const auto retval = session->call_sync("Authorize", cancellable, parameters);

        Glib::VariantBase child_code;
        retval.get_child(child_code, 0);

        const auto var_code = Glib::VariantBase::cast_dynamic< Glib::Variant<gint32> >(child_code);
        code = var_code.get();
    }
    catch (const Glib::Error &err) {
        g_warning("Authorization call failed: %s", err.what().c_str());
        g_dbus_method_invocation_return_error(invocation,
                G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                "reportd's Problems2 Session cannot be Authorized");
        return TRUE;
    }

    /* Request accepted */
    if (code == 1) {
        return TRUE;
    }

    /* Already authorized */
    if (code == 0) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("()"));
    }

    /* An error occurred */
    if (code == -1) {
        g_dbus_method_invocation_return_error(invocation,
                G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                "Failed to create Problems2 Session authorization request");
    }

    /* Authorization is already pending */
    if (code == 2)  {
        g_dbus_method_invocation_return_error(invocation,
                G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                "Problems2 Session authorization request already pending");
    }

    connection.disconnect();
    return TRUE;
}

static void
report_service_init(ReportService *self)
{
    self->service_iface = report_dbus_service_skeleton_new();
    self->task_cnt = 1;
    self->workflows = load_workflow_config_data(/*the default location*/NULL);

    g_signal_connect(self->service_iface,
                     "handle-create-task",
                     G_CALLBACK(report_service_handle_create_task),
                     self);

    g_signal_connect(self->service_iface,
                     "handle-get-workflows",
                     G_CALLBACK(report_service_handle_get_workflows),
                     self);

    g_signal_connect(self->service_iface,
                     "handle-authorize-problems-session",
                     G_CALLBACK(report_service_handle_authorize_problems_session),
                     self);

    g_dbus_object_skeleton_add_interface(G_DBUS_OBJECT_SKELETON(self),
                                         G_DBUS_INTERFACE_SKELETON(self->service_iface));
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
}

ReportService *report_service_new(const gchar *object_path)
{
    gpointer object = g_object_new(REPORT_TYPE_SERVICE,
                                   "g-object-path", object_path,
                                   NULL);

    return static_cast<ReportService *>(object);
}
