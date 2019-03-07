#include "reportd.h"

#include <locale.h>
#include <stdlib.h>

#include <internal_libreport.h>
#include <glib-unix.h>

static gboolean
on_signal_quit (gpointer user_data)
{
    ReportdDaemon *daemon;

    daemon = REPORTD_DAEMON (user_data);

    reportd_daemon_quit (daemon, NULL);

    return G_SOURCE_CONTINUE;
}

int
main (int    argc,
      char **argv)
{
    bool use_system_bus;
    const GOptionEntry option_entries[] =
    {
        { "system", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
          &use_system_bus, "Connect to the system bus", NULL },
        { NULL, }
    };
    g_autoptr (GOptionContext) option_context = NULL;
    g_autoptr (GError) error = NULL;
    g_autoptr (ReportdDaemon) daemon = NULL;
    unsigned int sigint_source;
    unsigned int sigterm_source;
    int status;

    setlocale (LC_ALL, "");

    use_system_bus = false;
    option_context = g_option_context_new (NULL);

    g_option_context_add_main_entries (option_context, option_entries, NULL);
    if (!g_option_context_parse (option_context, &argc, &argv, &error))
    {
        g_warning ("Could not parse options: %s", error->message);

        return EXIT_FAILURE;
    }

    daemon = reportd_daemon_new (use_system_bus);
    sigint_source = g_unix_signal_add (SIGINT, on_signal_quit, daemon);
    sigterm_source = g_unix_signal_add (SIGTERM, on_signal_quit, daemon);

    load_user_settings ("reportd");

    status = reportd_daemon_run (daemon, &error);
    if (status != EXIT_SUCCESS)
    {
        g_warning ("Daemon stopped with an error: %s", error->message);
    }

    save_user_settings ();

    g_clear_handle_id (&sigint_source, g_source_remove);
    g_clear_handle_id (&sigterm_source, g_source_remove);

    return status;
}
