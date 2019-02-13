reportd_sources = [
  gnome.gdbus_codegen('reportd-dbus-generated',
    sources: files('org.freedesktop.reportd.xml'),
    interface_prefix: 'org.freedesktop.reportd.',
    namespace: 'ReportdDbus',
  ),
  files(
    'reportd.h',
    'reportd-daemon.c',
    'reportd-daemon.h',
    'reportd-main.c',
    'reportd-task.c',
    'reportd-task.h',
    'reportd-service.c',
    'reportd-service.h',
  ),
]

reportd_dependencies = [
  gio,
  gio_unix,
  libreport,
]

executable('reportd', reportd_sources,
  dependencies: reportd_dependencies,
  install: true,
  install_dir: get_option('libexecdir'),
)
