configure_file(
  input: service_in,
  output: 'reportd.service',
  configuration: configuration_data({
    'libexecdir': join_paths(prefix, libexecdir),
    'options': '',
  }),
  install_dir: systemd.get_pkgconfig_variable('systemduserunitdir'),
)
