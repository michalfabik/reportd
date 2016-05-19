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

#include <set>
#include <iostream>

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <err.h>

using namespace Glib;

static RefPtr<MainLoop> s_main_loop;

class ReportDaemonPrivate {
    public:
        std::string cachedir;
        GDBusObjectManagerServer *object_manager;
        ReportService *report_service;

        bool connected() { return this->object_manager != 0; }

        Glib::RefPtr<Gio::DBus::Proxy> get_problems_entry_proxy(const std::string& problem_entry)
        {
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

            return entry;
        }

        std::string get_cachedir()
        {
            if (this->cachedir.empty())
            {
                std::stringstream sschd;
                sschd << "/var/run/user/" << getuid() <<  "/reportd";

                std::string tmpname = {sschd.str()};
                if (g_mkdir_with_parents(tmpname.c_str(), 0700) != 0) {
                    throw Gio::DBus::Error(Gio::DBus::Error::FAILED,
                            std::string{"Cannot create directory "} + tmpname);
                }

                this->cachedir = tmpname;
            }

            return this->cachedir;
        }
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
    std::string problem_dir = d->get_cachedir();

    problem_dir.append(problem_entry.begin() + problem_entry.find_last_of('/'),
                       problem_entry.end());

    if (!access(problem_dir.c_str(), R_OK)) {
        g_debug("Already pulled problem entry '%s'", problem_entry.c_str());
        return problem_dir;
    }

    g_debug("Pulling problem entry '%s'", problem_entry.c_str());

    auto entry = this->d->get_problems_entry_proxy(problem_entry);

    Glib::Variant<std::vector<Glib::ustring> > elements;
    entry->get_cached_property(elements, "Elements");

    if (!elements) {
        g_debug("Problem entry not accessible: '%s'", problem_entry.c_str());
        throw Gio::DBus::Error(Gio::DBus::Error::ACCESS_DENIED,
                               "Problems2 Entry is not accessible");
    }

    auto elem_vector = elements.get();
    const size_t elems = elem_vector.size();

    /* D-Bus can pass only the following number of FDs in a single message */
    const size_t dbus_fd_limit = 16;

    struct dump_dir *dd = dd_create_skeleton(problem_dir.c_str(), -1, 0600, 0);

    for (size_t batch = 0; batch < elems; batch += dbus_fd_limit) {
        const size_t range(batch + dbus_fd_limit);
        auto end(range > elems ? elem_vector.end() : elem_vector.begin() + range);
        std::vector<Glib::ustring> b(elem_vector.begin() + batch, end);

        auto parameters = Glib::VariantContainerBase::create_tuple({Glib::Variant<std::vector<Glib::ustring> >::create(b),
                                                                    Glib::Variant<int>::create(1)});

        auto cancellable = Gio::Cancellable::create();
        auto in_fds = Gio::UnixFDList::create();
        auto out_fds = Gio::UnixFDList::create();
        auto reply = entry->call_sync("ReadElements",
                                      parameters,
                                      cancellable,
                                      in_fds,
                                      out_fds,
                                      -1);

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
ReportDaemon::push_problem_directory(const std::string &problem_dir)
{
    g_debug("Pushing dump directory '%s'", problem_dir.c_str());

    /* Ignoring these elements because they should not be changed. */
    static std::set<std::string> ignored_elements = {
        "analyzer", "type", "time", "count"};

    if (access(problem_dir.c_str(), R_OK)) {
        throw std::runtime_error(
                std::string("Temporary problem directory disappeared: ") + problem_dir);
    }

    std::string problem_entry{"/org/freedesktop/Problems2/Entry"};
    problem_entry.append(problem_dir.begin() + problem_dir.find_last_of('/'),
                         problem_dir.end());

    auto entry = this->d->get_problems_entry_proxy(problem_entry);

    struct dump_dir *dd = dd_opendir(problem_dir.c_str(), 0);
    if (dd == NULL) {
        throw std::runtime_error(
                std::string{"Cannot open problem directory: "} + problem_dir);
    }

    dd_init_next_file(dd);

    char *short_name = NULL;
    while (dd_get_next_file(dd, &short_name, NULL)) {
        /* Skip ignored elements */
        if (ignored_elements.count(short_name) != 0) {
            continue;
        }

        const int fd = openat(dd->dd_fd, short_name, O_RDONLY);
        if (fd < 0) {
            g_warning("Failed to open '%s' : ignoring", short_name);
            continue;
        }

        auto in_fds = Gio::UnixFDList::create();
        const int pos = in_fds->append(fd);
        close(fd);

        auto out_fds = Gio::UnixFDList::create();

        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));

        g_variant_builder_add(&builder,
                              "{sv}",
                              short_name,
                              g_variant_new("h", pos));

        GVariant *data = g_variant_builder_end(&builder);

        std::vector<Glib::VariantBase> p(2);
        p.at(0).init(data, true);
        p.at(1) = Glib::Variant<int>::create(0);

        auto parameters = Glib::VariantContainerBase::create_tuple(p);

        try {
            auto cancellable = Gio::Cancellable::create();
            auto reply = entry->call_sync("SaveElements",
                                      parameters,
                                      cancellable,
                                      in_fds,
                                      out_fds,
                                      -1);
        }
        catch ( const Glib::Error &err ) {
            g_warning("Failed to sync element '%s': %s",
                      short_name,
                      err.what().c_str());
            continue;
        }
    }
    dd_close(dd);
}

void
ReportDaemon::settle_connection(const Glib::RefPtr<Gio::DBus::Connection> &connection)
{
    if (d->connected()) {
        g_warning("report-daemon already settled a connection");
        return;
    }

    d->object_manager = g_dbus_object_manager_server_new(REPORTD_DBUS_OBJECT_MANAGER_PATH);

    d->report_service = report_service_new(REPORTD_DBUS_SERVICE_PATH);
    g_dbus_object_manager_server_export(d->object_manager, G_DBUS_OBJECT_SKELETON(d->report_service));

    g_dbus_object_manager_server_set_connection(d->object_manager, connection->gobj());
}

/* static */ void
ReportDaemon::on_name_acquired(const Glib::RefPtr<Gio::DBus::Connection> &connection,
                               const Glib::ustring                       &name)
{
    g_debug("Session bus with '%s' acquired", name.c_str());
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
on_bus_acquired(const Glib::RefPtr<Gio::DBus::Connection>&,
                const Glib::ustring                      & name)
{
    g_debug("DBus name has been acquired: %s", name.c_str());
}

static void
on_name_lost(const Glib::RefPtr<Gio::DBus::Connection> &,
             const Glib::ustring                       &name)
{
    g_warning("DBus name has been lost: %s", name.c_str());
    s_main_loop->quit();
}

static gboolean
on_signal_quit(gpointer)
{
    s_main_loop->quit();
    return FALSE;
}

int
main(int argc, char *argv[])
{
    Gio::init();

    {
        bool verbose = false;
        Glib::OptionEntry opt_v;
        opt_v.set_short_name('v');
        opt_v.set_long_name("verbose");
        opt_v.set_description("Produce debugging output");

        Glib::OptionGroup options{"OPTIONS", "Program options"};
        options.add_entry(opt_v, verbose);

        Glib::OptionContext context;
        context.set_help_enabled();
        context.set_main_group(options);

        if (!context.parse(argc, argv)) {
            errx(EXIT_FAILURE, "Invalid command line arguments");
        }

        if (verbose) {
            Glib::setenv("G_MESSAGES_DEBUG", "all");
        }
    }

    { /* Test availability of Session bus */
        try {
            auto connection = Gio::DBus::Connection::get_sync(Gio::DBus::BusType::BUS_TYPE_SESSION);
            if (!connection) {
                errx(EXIT_FAILURE, "The user's session bus is not available.");
            }
        }
        catch(const Glib::Error &error) {
            errx(EXIT_FAILURE, "Failed to connect to user's session bus : %s", error.what().c_str());
        }
    }

    guint bus_name_id = Gio::DBus::own_name(Gio::DBus::BusType::BUS_TYPE_SESSION,
                                            REPORTD_DBUS_BUS_NAME,
                                            sigc::ptr_fun(on_bus_acquired),
                                            sigc::ptr_fun(ReportDaemon::on_name_acquired),
                                            sigc::ptr_fun(on_name_lost));

    s_main_loop = MainLoop::create();

    g_unix_signal_add(SIGINT,  on_signal_quit, NULL);
    g_unix_signal_add(SIGTERM, on_signal_quit, NULL);

    s_main_loop->run();

    if (bus_name_id > 0) {
        Gio::DBus::unown_name(bus_name_id);
    }

    return bus_name_id > 0;
}
