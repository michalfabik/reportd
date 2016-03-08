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

#include "report-daemon.h"
#include "report-service.h"
#include "report-task.h"
#include "report-dbus-constants.h"

#include <glib-unix.h>
#include <glibmm.h>
#include <giomm.h>

#include <dump_dir.h>

using namespace Glib;

static RefPtr<MainLoop> s_main_loop;

class ReportDaemonPrivate {
    public:
        GDBusObjectManagerServer *object_manager;
        ReportService *report_service;

        bool connected() { return this->object_manager != 0; }
};

ReportDaemon::ReportDaemon() :
    d(new ReportDaemonPrivate())
{}

ReportDaemon::~ReportDaemon()
{
    delete d;
}

/* static */ ReportDaemon &
ReportDaemon::inst()
{
    static ReportDaemon daemon;

    return daemon;
}

std::string
ReportDaemon::get_problem_directory(const std::string &problem_entry)
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

void
ReportDaemon::settle_connection(GDBusConnection *connection)
{
    if (d->connected()) {
        g_warning("report-daemon already settled a connection");
        return;
    }

    d->object_manager = g_dbus_object_manager_server_new(REPORTD_DBUS_OBJECT_MANAGER_PATH);

    d->report_service = report_service_new(REPORTD_DBUS_SERVICE_PATH);
    g_dbus_object_manager_server_export(d->object_manager, G_DBUS_OBJECT_SKELETON(d->report_service));

    g_dbus_object_manager_server_set_connection(d->object_manager, connection);
}

/* static */ void
ReportDaemon::on_name_acquired(GDBusConnection *connection,
                 const gchar      *name,
                 gpointer         )
{
    g_debug("Session bus with '%s' acquired", name);
    ReportDaemon::inst().settle_connection(connection);
}

void
ReportDaemon::register_object(GDBusObjectSkeleton *object)
{
    if (!d->connected()) {
        /* TODO : throw an exception if the daemon isn't settled yet */
        g_warning("report-daemon not yet settled a connection: cannot register an object");
        return;
    }

    g_dbus_object_manager_server_export(d->object_manager, object);
}


static void
on_name_lost(GDBusConnection *,
             const gchar     *name,
             gpointer        )
{
    g_warning("DBus name has been lost: %s", name);
    s_main_loop->quit();
}

static gboolean
on_signal_quit(gpointer data)
{
    (*static_cast<RefPtr<MainLoop> *>(data))->quit();
    return FALSE;
}

int
main(void)
{
    Gio::init();

    guint bus_name_id = g_bus_own_name(G_BUS_TYPE_SESSION,
                                       REPORTD_DBUS_BUS_NAME,
                                       G_BUS_NAME_OWNER_FLAGS_NONE,
                                       NULL,
                                       ReportDaemon::on_name_acquired,
                                       on_name_lost,
                                       NULL,
                                       NULL);

    s_main_loop = MainLoop::create();

    g_unix_signal_add(SIGINT,  on_signal_quit, &s_main_loop);
    g_unix_signal_add(SIGTERM, on_signal_quit, &s_main_loop);

    s_main_loop->run();

    if (bus_name_id > 0) {
        g_bus_unown_name(bus_name_id);
    }

    return bus_name_id > 0;
}
