service_conf = configuration_data()

service_conf.set('libexecdir', get_option('libexecdir'))

configure_file(
  input: files('org.freedesktop.reportd.service.in'),
  output: 'org.freedesktop.reportd.service',
  configuration: service_conf,
  install_dir: join_paths(get_option('datadir'), 'dbus-1', 'services'),
)
