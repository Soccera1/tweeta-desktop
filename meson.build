project('tweeta-desktop', 'c',
  version : '0.1',
  default_options : ['warning_level=2', 'c_std=c99'])

gtk_dep = dependency('gtk+-3.0')
json_glib_dep = dependency('json-glib-1.0')
curl_dep = dependency('libcurl')

inc = include_directories('src')

sources = [
  'src/globals.c',
  'src/network.c',
  'src/json_utils.c',
  'src/session.c',
  'src/ui_utils.c',
  'src/ui_components.c',
  'src/views.c',
  'src/actions.c'
]

executable('tweeta-desktop',
  sources: sources + ['src/main.c'],
  dependencies : [gtk_dep, json_glib_dep, curl_dep],
  include_directories : inc,
  install : true
)

test_runner = executable('test_runner',
  sources: sources + ['test_main.c'],
  dependencies : [gtk_dep, json_glib_dep, curl_dep],
  include_directories : inc,
  install : false
)

test('basic', test_runner)

# Installation of extra files
install_data('tweeta-desktop.desktop',
  install_dir : get_option('datadir') / 'applications'
)

install_data('logo.png',
  rename : 'tweeta-desktop.png',
  install_dir : get_option('datadir') / 'pixmaps'
)

install_man('tweeta-desktop.1')
