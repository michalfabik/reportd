configure_file(
  input: service_in,
  output: 'org.freedesktop.reportd.service',
  configuration: configuration_data({
    'libexecdir': join_paths(prefix, libexecdir),
    'options': '',
  }),
  install_dir: join_paths(get_option('datadir'), 'dbus-1', 'services'),
)
