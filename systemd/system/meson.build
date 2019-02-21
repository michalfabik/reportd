configure_file(
  input: service_in,
  output: 'reportd.service',
  configuration: configuration_data({
    'libexecdir': join_paths(prefix, libexecdir),
    'options': '--system',
  }),
  install_dir: systemd.get_pkgconfig_variable('systemdsystemunitdir'),
)
