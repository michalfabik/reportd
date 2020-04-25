project(
  'reportd',
  'c',
  license : 'GPL2+',
  version : '0.7.4',
  default_options : ['c_std=c99']
)

cc = meson.get_compiler('c')

add_project_arguments(
  cc.get_supported_arguments([
    '-D_GNU_SOURCE',

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
libreport = dependency('libreport', version: '>= 2.13.0')
systemd = dependency('systemd')

gnome = import('gnome')

subdir('dbus')
subdir('src')
subdir('systemd')

tito = find_program('tito',
  required: false,
  disabler: true,
)

# This will, naturally, fail if the build directory is outside the git repo,
# since Tito does not provide a way to specify the working directory or the spec
# file using the CLI.
run_target('rpm',
  command: [
    tito,
    'build',
    '--offline',
    '--output=@0@/rpm'.format(meson.current_build_dir()),
    '--rpm',
    '--test'
  ],
)
run_target('srpm',
  command: [
    tito,
    'build',
    '--offline',
    '--output=@0@/rpm'.format(meson.current_build_dir()),
    '--srpm',
    '--test'
  ],
)
