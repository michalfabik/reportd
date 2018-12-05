service_conf = configuration_data()

service_conf.set('libexecdir', get_option('libexecdir'))

configure_file(
  input: files('org.freedesktop.reportd.service.in'),
  output: 'org.freedesktop.reportd.service',
  configuration: service_conf,
  install_dir: join_paths(get_option('datadir'), 'dbus-1', 'services'),
)

service_sources = files(
  'report-dbus-constants.h',
)

service_generated = gnome.gdbus_codegen('report-dbus-generated',
  sources: files('org.freedesktop.reportd.xml'),
  interface_prefix: 'org.freedesktop.reportd.',
  namespace: 'ReportDbus',
)

reportd_dbus = declare_dependency(
  include_directories: include_directories('.'),
  sources: [
    service_sources,
    service_generated,
  ],
)
