configure_file(
  input: files('reportd.service.in'),
  output: 'reportd.service',
  configuration: configuration_data({
    'libexecdir': join_paths(prefix, libexecdir),
  }),
  install_dir: systemd.get_pkgconfig_variable('systemduserunitdir'),
)
