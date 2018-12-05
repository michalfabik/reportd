reportd_sources = files(
  'report-daemon.cpp',
  'report-daemon.h',
  'report-task.cpp',
  'report-task.h',
  'report-service.cpp',
  'report-service.h',
)

reportd_dependencies = [
  giomm,
  gio_unix,
  libreport,
  reportd_dbus,
]

executable('reportd', reportd_sources,
  dependencies: reportd_dependencies,
  install: true,
)
