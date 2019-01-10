project('reportd', 'c', 'cpp',
  license: 'GPL2+',
  version: '0.2',
)

giomm = dependency('giomm-2.4')
gio_unix = dependency('gio-unix-2.0')
libreport = dependency('libreport', version: '>= 2.10.0')

gnome = import('gnome')

subdir('dbus')
subdir('service')
