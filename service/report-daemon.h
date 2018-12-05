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

#ifndef __REPORT_DAEMON_H__
#define __REPORT_DAEMON_H__

#include <giomm.h>

class ReportDaemon {
    public:
        std::string get_problem_directory  (const std::string &);
        void        push_problem_directory (const std::string &);

        void        register_object       (GDBusObjectSkeleton *);


        static ReportDaemon& inst();

        static void on_name_acquired(const Glib::RefPtr<Gio::DBus::Connection> &connection,
                                     const Glib::ustring                       &name);

    private:
        void settle_connection(const Glib::RefPtr<Gio::DBus::Connection> &connection);

        ReportDaemon();
        ReportDaemon(const ReportDaemon &) = delete;
        ReportDaemon& operator=(const ReportDaemon &) = delete;

        class ReportDaemonPrivate *d;

        ~ReportDaemon();
};


#endif /*__REPORT_DAEMON_H__*/
