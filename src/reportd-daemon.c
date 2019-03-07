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

#include <errno.h>
#include <fcntl.h>
#include <gio/gunixfdlist.h>
#include <dump_dir.h>
#include <stdlib.h>

/* D-Bus can pass only the following number of FDs in a single message */
#define DBUS_FD_LIMIT 16

struct _ReportdDaemon
{
    GObject parent_instance;

    GBusType bus_type;

    GFile *cache_directory;

    GMainLoop *main_loop;

    GDBusConnection *system_bus_connection;
    GDBusConnection *session_bus_connection;

    unsigned int bus_id;
    GDBusObjectManagerServer *object_manager;
    ReportdService *service;
    GError *error;
};

G_DEFINE_TYPE (ReportdDaemon, reportd_daemon, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_BUS_TYPE,
    N_PROPERTIES,
};

static GParamSpec *properties[N_PROPERTIES];

static void
reportd_daemon_init (ReportdDaemon *self)
{
    self->main_loop = g_main_loop_new (NULL, FALSE);
}

static void
reportd_daemon_set_property (GObject      *object,
                             unsigned int  property_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
    ReportdDaemon *self;

    self = REPORTD_DAEMON (object);

    switch (property_id)
    {
        case PROP_BUS_TYPE:
        {
            self->bus_type = g_value_get_enum (value);
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        }
    }
}

static void
reportd_daemon_get_property (GObject      *object,
                             unsigned int  property_id,
                             GValue       *value,
                             GParamSpec   *pspec)
{
    ReportdDaemon *self;

    self = REPORTD_DAEMON (object);

    switch (property_id)
    {
        case PROP_BUS_TYPE:
        {
            g_value_set_enum (value, self->bus_type);
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        }
    }
}

static void
reportd_daemon_dispose (GObject *object)
{
    ReportdDaemon *self;

    self = REPORTD_DAEMON (object);

    g_clear_object (&self->cache_directory);
}

static void
reportd_daemon_finalize (GObject *object)
{
    ReportdDaemon *self;

    self = REPORTD_DAEMON (object);

    g_clear_pointer (&self->main_loop, g_main_loop_unref);
    g_clear_handle_id (&self->bus_id, g_bus_unown_name);
}

static void
reportd_daemon_class_init (ReportdDaemonClass *klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = reportd_daemon_set_property;
    object_class->get_property = reportd_daemon_get_property;
    object_class->dispose = reportd_daemon_dispose;
    object_class->finalize = reportd_daemon_finalize;

    properties[PROP_BUS_TYPE] = g_param_spec_enum ("bus-type", "Bus Type",
                                                   "The bus type to use for D-Bus connection",
                                                   G_TYPE_BUS_TYPE,
                                                   G_BUS_TYPE_SESSION,
                                                   (G_PARAM_READWRITE |
                                                    G_PARAM_CONSTRUCT_ONLY |
                                                    G_PARAM_STATIC_STRINGS));

    g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

static void
reportd_daemon_on_name_acquired (GDBusConnection *connection,
                                 const char      *name,
                                 gpointer         user_data)
{
    ReportdDaemon *self;

    self = REPORTD_DAEMON (user_data);

    self->object_manager = g_dbus_object_manager_server_new (REPORTD_DBUS_OBJECT_MANAGER_PATH);
    self->service = reportd_service_new (self, REPORTD_DBUS_SERVICE_PATH);

    g_dbus_object_manager_server_set_connection (self->object_manager, connection);
}

static void
reportd_daemon_on_name_lost (GDBusConnection *connection,
                             const char      *name,
                             gpointer         user_data)
{
    ReportdDaemon *daemon;
    g_autoptr (GError) error = NULL;

    daemon = REPORTD_DAEMON (user_data);

    error = g_error_new (G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                         "Bus name “%s” lost", name);

    reportd_daemon_quit (daemon, error);
}

bool
reportd_daemon_connect_to_bus (ReportdDaemon  *self,
                               GCancellable   *cancellable,
                               GError        **error)
{
    GDBusConnection *connection;

    g_return_val_if_fail (REPORTD_IS_DAEMON (self), false);

    if (g_cancellable_set_error_if_cancelled (cancellable, error))
    {
        return false;
    }

    self->system_bus_connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, cancellable, error);
    if (NULL == self->system_bus_connection)
    {
        return false;
    }
    if (self->bus_type == G_BUS_TYPE_SESSION)
    {
        self->session_bus_connection = g_bus_get_sync (G_BUS_TYPE_SESSION, cancellable, error);
        if (NULL == self->session_bus_connection)
        {
            return false;
        }

        connection = self->session_bus_connection;
    }
    else
    {
        connection = self->system_bus_connection;
    }
    self->bus_id = g_bus_own_name_on_connection (connection,
                                                 REPORTD_DBUS_BUS_NAME,
                                                 G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE,
                                                 reportd_daemon_on_name_acquired,
                                                 reportd_daemon_on_name_lost,
                                                 self, NULL);

    return true;
}

static bool
reportd_daemon_populate_dump_directory (ReportdDaemon    *self,
                                        const char       *entry,
                                        struct dump_dir  *dump_directory,
                                        const char      **elements,
                                        size_t            element_count,
                                        GError          **error)
{
    for (size_t i = 0; i < element_count; i += DBUS_FD_LIMIT)
    {
        GVariant *strv;
        g_autoptr (GVariantBuilder) builder = NULL;
        g_autoptr (GUnixFDList) out_fd_list = NULL;
        g_autoptr (GVariant) tuple = NULL;
        g_autoptr (GVariant) dictionary = NULL;
        GVariantIter iter;
        char *key;
        GVariant *value;

        strv = g_variant_new_strv (elements + i, MIN (DBUS_FD_LIMIT, element_count - i));
        builder = g_variant_builder_new (G_VARIANT_TYPE_TUPLE);

        g_variant_builder_add_value (builder, strv);
        g_variant_builder_add_parsed (builder, "1");

        tuple = g_dbus_connection_call_with_unix_fd_list_sync (self->system_bus_connection,
                                                               "org.freedesktop.problems",
                                                               entry,
                                                               "org.freedesktop.Problems2.Entry",
                                                               "ReadElements",
                                                               g_variant_builder_end (builder),
                                                               G_VARIANT_TYPE ("(a{sv})"),
                                                               G_DBUS_CALL_FLAGS_NONE,
                                                               -1,
                                                               NULL, &out_fd_list,
                                                               NULL,
                                                               error);
        if (NULL == tuple)
        {
            return false;
        }
        dictionary = g_variant_get_child_value (tuple, 0);

        g_variant_iter_init (&iter, dictionary);

        while (g_variant_iter_loop (&iter, "{sv}", &key, &value))
        {
            int index;
            int fd;

            index = g_variant_get_handle (value);
            fd = g_unix_fd_list_get (out_fd_list, index, error);
            if (-1 == fd)
            {
                close (fd);
                dd_close (dump_directory);

                return false;
            }

            dd_copy_fd (dump_directory, key, fd, 0, 0);

            close (fd);
        }
    }

    return true;
}

char *
reportd_daemon_get_problem_directory (ReportdDaemon  *self,
                                      const char     *entry,
                                      GError        **error)
{
    g_autofree char *cache_directory_path = NULL;
    g_autofree char *canonical_entry = NULL;
    g_autofree char *base_name = NULL;
    g_autofree char *cache_problem_directory_path = NULL;
    g_autoptr (GVariant) tuple = NULL;
    g_autoptr (GVariant) variant = NULL;
    g_autoptr (GVariant) elements_variant = NULL;
    size_t element_count;
    g_autofree const char **elements = NULL;
    struct dump_dir *dump_directory;

    cache_directory_path = g_file_get_path (self->cache_directory);
    canonical_entry = g_canonicalize_filename (entry, "/");
    base_name = g_path_get_basename (canonical_entry);
    cache_problem_directory_path = g_build_path ("/", cache_directory_path, base_name, NULL);

    g_return_val_if_fail (g_strcmp0 (cache_directory_path, cache_problem_directory_path) != 0, NULL);

    if (g_file_test (cache_problem_directory_path, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR))
    {
        g_debug ("Cache directory for entry “%s” already exists, returning", entry);

        return g_steal_pointer (&cache_problem_directory_path);
    }

    g_debug ("Pulling entry “%s”", entry);

    tuple = g_dbus_connection_call_sync (self->system_bus_connection,
                                         "org.freedesktop.problems",
                                         entry,
                                         "org.freedesktop.DBus.Properties",
                                         "Get",
                                         g_variant_new ("(ss)",
                                                        "org.freedesktop.Problems2.Entry",
                                                        "Elements"),
                                         G_VARIANT_TYPE ("(v)"),
                                         G_DBUS_CALL_FLAGS_NONE,
                                         -1,
                                         NULL,
                                         error);
    if (NULL == tuple)
    {
        return NULL;
    }
    variant = g_variant_get_child_value (tuple, 0);
    elements_variant = g_variant_get_variant (variant);
    elements = g_variant_get_strv (elements_variant, &element_count);
    dump_directory = dd_create_skeleton (cache_problem_directory_path, -1, 0600, 0);

    if (!reportd_daemon_populate_dump_directory (self, entry, dump_directory,
                                                 elements, element_count, error))
    {
        dd_close (dump_directory);

        return NULL;
    }

    dd_close (dump_directory);

    return g_steal_pointer (&cache_problem_directory_path);
}

static bool
reportd_daemon_save_elements (ReportdDaemon  *self,
                              const char     *entry,
                              GVariant       *dictionary,
                              GUnixFDList    *fd_list,
                              GError        **error)
{
    g_autoptr (GVariantBuilder) builder = NULL;
    g_autoptr (GVariant) variant = NULL;

    builder = g_variant_builder_new (G_VARIANT_TYPE_TUPLE);

    g_variant_builder_add_value (builder, dictionary);
    g_variant_builder_add_parsed (builder, "0");

    variant = g_dbus_connection_call_with_unix_fd_list_sync (self->system_bus_connection,
                                                             "org.freedesktop.problems",
                                                             entry,
                                                             "org.freedesktop.Problems2.Entry",
                                                             "SaveElements",
                                                             g_variant_builder_end (builder),
                                                             NULL,
                                                             G_DBUS_CALL_FLAGS_NONE,
                                                             -1,
                                                             fd_list, NULL,
                                                             NULL,
                                                             error);

    return variant != NULL;
}

static int
reportd_daemon_get_n_elements (struct dump_dir  *dump_directory,
                               int               n,
                               GUnixFDList     **fd_list,
                               GVariantDict    **dictionary,
                               GError          **error)
{
    const char *ignored_elements[] =
    {
        "analyzer",
        "count",
        "time",
        "type",
        NULL,
    };
    int count = 0;
    char *tmp;

    g_return_val_if_fail (NULL != fd_list, 0);
    g_return_val_if_fail (NULL != dictionary, 0);

    *fd_list = g_unix_fd_list_new ();
    *dictionary = g_variant_dict_new (NULL);

    for (int i = 0; i < n && dd_get_next_file (dump_directory, &tmp, NULL); i++)
    {
        g_autofree char *short_name = NULL;
        int fd;
        int pos;

        short_name = g_steal_pointer (&tmp);

        if (g_strv_contains (ignored_elements, short_name))
        {
            continue;
        }

        fd = openat (dump_directory->dd_fd, short_name, O_RDONLY);
        if (-1 == fd)
        {
            g_warning ("Failed to open “%s”, ignoring", short_name);

            continue;
        }
        pos = g_unix_fd_list_append (*fd_list, fd, error);
        if (-1 == pos)
        {
            close (fd);

            return -1;
        }

        g_variant_dict_insert (*dictionary, short_name, "h", pos);

        count++;

        close (fd);
    }

    return count;
}

bool
reportd_daemon_push_problem_directory (ReportdDaemon  *self,
                                       const char     *problem_directory,
                                       GError        **error)
{
    g_autoptr (GFile) file = NULL;
    g_autofree char *base_name = NULL;
    g_autofree char *entry = NULL;
    struct dump_dir *dump_directory;
    g_autoptr (GVariantDict) dictionary = NULL;
    g_autoptr (GUnixFDList) fd_list = NULL;
    g_autoptr (GError) tmp_error = NULL;

    g_debug ("Pushing problem directory “%s”", problem_directory);

    file = g_file_new_for_path (problem_directory);

    if (!g_file_has_parent (file, self->cache_directory))
    {
        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                     "“%s” is outside the cache directory", problem_directory);

        return false;
    }
    if (!g_file_test (problem_directory, G_FILE_TEST_IS_DIR))
    {
        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_NOTDIR,
                     "“%s” is not a directory", problem_directory);

        return false;
    }

    base_name = g_file_get_basename (file);
    entry = g_strdup_printf ("/org/freedesktop/Problems2/Entry/%s", base_name);
    dump_directory = dd_opendir (problem_directory, 0);
    if (NULL == dump_directory)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                     "Opening problem directory “%s” failed", problem_directory);

        return false;
    }

    dd_init_next_file (dump_directory);

    while (reportd_daemon_get_n_elements (dump_directory, DBUS_FD_LIMIT,
                                          &fd_list, &dictionary, &tmp_error) > 0)
    {
        GVariant *variant;

        variant = g_variant_dict_end (dictionary);

        if (!reportd_daemon_save_elements (self, entry, variant, fd_list, error))
        {
            dd_close (dump_directory);

            return false;
        }

        g_clear_object (&fd_list);
        g_clear_pointer (&dictionary, g_variant_dict_unref);
    }

    dd_close (dump_directory);

    if (NULL != tmp_error)
    {
        g_propagate_error (error, tmp_error);

        return false;
    }

    return true;
}

void
reportd_daemon_get_bus_connections (ReportdDaemon    *self,
                                    GDBusConnection **system_bus_connection,
                                    GDBusConnection **session_bus_connection)
{
    g_return_if_fail (REPORTD_IS_DAEMON (self));

    if (NULL != system_bus_connection)
    {
        *system_bus_connection = g_object_ref (self->system_bus_connection);
    }
    if (NULL != session_bus_connection && NULL != self->session_bus_connection)
    {
        *session_bus_connection = g_object_ref (self->session_bus_connection);
    }
}

void
reportd_daemon_register_object (ReportdDaemon       *self,
                                GDBusObjectSkeleton *object)
{
    g_return_if_fail (REPORTD_IS_DAEMON (self));

    g_dbus_object_manager_server_export_uniquely (self->object_manager, object);
}

void
reportd_daemon_unregister_object (ReportdDaemon *self,
                                  GDBusObject   *object)
{
    const char *object_path;

    g_return_if_fail (REPORTD_IS_DAEMON (self));

    object_path = g_dbus_object_get_object_path (object);

    g_dbus_object_manager_server_unexport (self->object_manager, object_path);
}

int
reportd_daemon_run (ReportdDaemon  *self,
                    GError        **error)
{
    g_autofree char *path = NULL;
    g_autoptr (GFile) file = NULL;

    g_return_val_if_fail (REPORTD_IS_DAEMON (self), EXIT_FAILURE);

    path = g_build_path ("/", g_get_user_runtime_dir (), "reportd", NULL);

    self->cache_directory = g_file_new_for_path (path);

    if (g_mkdir_with_parents (path, 0700) == -1)
    {
        g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno), "%s",
                     strerror (errno));

        return EXIT_FAILURE;
    }

    g_main_loop_run (self->main_loop);

    if (NULL != self->error)
    {
        g_propagate_error (error, self->error);

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

void
reportd_daemon_quit (ReportdDaemon *self,
                     GError        *error)
{
    g_return_if_fail (REPORTD_IS_DAEMON (self));

    /* The handlers for SIGINT and SIGTERM are set up before we even spin up
     * the main loop. Let’s just allow such calls.
     */
    if (!g_main_loop_is_running (self->main_loop))
    {
        return;
    }

    g_main_loop_quit (self->main_loop);

    if (NULL != error)
    {
        self->error = g_error_copy (error);
    }
}

ReportdDaemon *
reportd_daemon_new (bool use_system_bus)
{
    GBusType bus_type;

    bus_type = use_system_bus? G_BUS_TYPE_SYSTEM : G_BUS_TYPE_SESSION;

    return g_object_new (REPORTD_TYPE_DAEMON, "bus-type", bus_type, NULL);
}
