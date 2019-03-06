project('reportd', 'c',
  license: 'GPL2+',
  version: '0.6.1',
  default_options: [
    'c_std=c99',
  ],
)

cc = meson.get_compiler('c')

add_project_arguments(
  cc.get_supported_arguments([
    '-Wall',
    '-Wduplicated-branches',
    '-Wduplicated-cond',
    '-Wlogical-op',

    '-Werror=declaration-after-statement',
    '-Werror=empty-body',
    '-Werror=format=2',
    '-Werror=implicit-function-declaration',
    '-Werror=incompatible-pointer-types',
    '-Werror=init-self',
    '-Werror=missing-include-dirs',
    '-Werror=missing-prototypes',
    '-Werror=pointer-arith',
    '-Werror=sequence-point',
    '-Werror=shadow',
    '-Werror=strict-prototypes',
#    '-Werror=undef',
    '-Werror=uninitialized',

    '-Wno-error=maybe-uninitialized',
  ]),
  language: 'c',
)

libexecdir = get_option('libexecdir')
prefix = get_option('prefix')

gio = dependency('gio-2.0')
gio_unix = dependency('gio-unix-2.0')
libreport = dependency('libreport', version: '>= 2.10.0')
systemd = dependency('systemd')

gnome = import('gnome')

subdir('dbus')
subdir('src')
subdir('systemd')
