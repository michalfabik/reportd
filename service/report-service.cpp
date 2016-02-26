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

#include <iostream>
#include <cstring>

#include <dump_dir.h>
#include <run_event.h>
#include <workflow.h>

G_DEFINE_TYPE(ReportService, report_service, G_TYPE_DBUS_OBJECT_SKELETON);

struct _ReportServicePrivate {
    ReportDbusService *service_iface;
};

static gboolean
report_service_handle_create_task(ReportDbusService * /*object*/,
                                  GDBusMethodInvocation * /*invocation*/,
                                  const gchar * /*arg_workflow*/,
                                  const gchar * /*arg_problem*/)
{
    return TRUE;
}

static std::string
get_problem_directory(const std::string &problem_entry)
{
    std::string problem_dir("/var/tmp");
    problem_dir.append(problem_entry.begin() + problem_entry.find_last_of('/'), problem_entry.end());

    if (!access(problem_dir.c_str(), R_OK))
        return problem_dir;

    auto cancellable = Gio::Cancellable::create();
    auto connection = Gio::DBus::Connection::get_sync(Gio::DBus::BusType::BUS_TYPE_SYSTEM,
                                                      cancellable);
    if (!connection) {
        throw Gio::DBus::Error(Gio::DBus::Error::FAILED,
                               "Cannot get system bus");
    }

    auto info = Glib::RefPtr<Gio::DBus::InterfaceInfo>();
    auto entry = Gio::DBus::Proxy::create_sync(connection,
                                               "org.freedesktop.problems",
                                               problem_entry,
                                               "org.freedesktop.Problems2.Entry",
                                               cancellable,
                                               info,
                                               Gio::DBus::PROXY_FLAGS_NONE);

    if (!entry) {
        throw Gio::DBus::Error(Gio::DBus::Error::INVALID_ARGS,
                               "Problems2 Entry is not accessible");
    }


    Glib::Variant<std::vector<Glib::ustring> > elements;
    entry->get_cached_property(elements, "Elements");

    if (!elements) {
        throw Gio::DBus::Error(Gio::DBus::Error::FAILED,
                               "Problems2 Entry does not have property Elements");
    }

    auto elem_vector = elements.get();
    const size_t elems(elem_vector.size());
    const size_t dbus_fd_limit(16);

    struct dump_dir *dd = dd_create_skeleton(problem_dir.c_str(), -1, 0600, 0);

    for (size_t batch = 0; batch < elems; batch += dbus_fd_limit) {
        const size_t range(batch + dbus_fd_limit);
        auto end(range > elems ? elem_vector.end() : elem_vector.begin() + range);
        std::vector<Glib::ustring> b(elem_vector.begin() + batch, end);

        auto parameters = Glib::VariantContainerBase::create_tuple({Glib::Variant<std::vector<Glib::ustring> >::create(b),
                                                                    Glib::Variant<int>::create(1)});

        auto in_fds = Gio::UnixFDList::create();
        auto out_fds = Gio::UnixFDList::create();
        auto reply = entry->call_sync("ReadElements",
                                      parameters,
                                      cancellable,
                                      in_fds,
                                      out_fds,
                                      -1);

        batch = range;

        Glib::Variant<std::map<std::string, Glib::VariantBase> > data;
        reply.get_child(data);
        for (auto kv : data.get()) {
            Glib::Variant<gint32> fd_pos = Glib::VariantBase::cast_dynamic< Glib::Variant<gint32> >(kv.second);

            int fd = out_fds->get(fd_pos.get());
            dd_copy_fd(dd, kv.first.c_str(), fd, 0, 0);
            close(fd);
        }
    }
    dd_close(dd);
    return problem_dir;
}

static gboolean
report_service_handle_get_workflows(ReportDbusService * /*object*/,
                                    GDBusMethodInvocation *invocation,
                                    const gchar *arg_problem)
{
    std::string problem_dir;

    try {
        problem_dir = get_problem_directory(arg_problem);
    }
    catch (const Glib::Error &err) {
        g_dbus_method_invocation_return_error(invocation,
                                              err.domain(),
                                              err.code(),
                                              err.what().c_str());
        return TRUE;
    }

    GList *wfs = list_possible_events_glist(problem_dir.c_str(), "workflow");
    GHashTable *workflow_table = load_workflow_config_data_from_list(wfs,
                                                                     "/usr/share/libreport/workflows/");
    g_list_free_full(wfs, free);
    GList *wf_list = g_hash_table_get_values(workflow_table);

    GVariantBuilder top_builder;
    g_variant_builder_init(&top_builder, G_VARIANT_TYPE("a(sss)"));

    for (GList *wf_iter = wf_list; wf_iter; wf_iter = g_list_next(wf_iter)) {
        workflow_t *w = (workflow_t *)wf_iter->data;

        GVariant *children[3];
        children[0] = g_variant_new_string(wf_get_name(w));
        children[1] = g_variant_new_string(wf_get_screen_name(w));
        children[2] = g_variant_new_string(wf_get_description(w));
        GVariant *entry = g_variant_new_tuple(children, 3);

        g_variant_builder_add_value(&top_builder, entry);
    }

    GVariant *retval[1];
    retval[0] = g_variant_builder_end(&top_builder);
    GVariant *value = g_variant_new_tuple(retval, 1);

    g_dbus_method_invocation_return_value(invocation, value);

    g_list_free(wf_list);
    g_hash_table_destroy(workflow_table);

    return TRUE;
}

static void
report_service_init(ReportService *self)
{
    self->pv = G_TYPE_INSTANCE_GET_PRIVATE(self, REPORT_TYPE_SERVICE, ReportServicePrivate);

    self->pv->service_iface = report_dbus_service_skeleton_new();

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
