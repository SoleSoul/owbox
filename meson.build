project(
	'owbox',
	'c',
	version: '0.1.0',
	license: 'GPL-2.0-or-later',
	meson_version: '>=1.3.0',
	default_options: [
		'c_std=c11',
		'warning_level=2',
		'werror=false',
		'buildtype=debugoptimized',
	],
)

cc = meson.get_compiler('c')
add_project_arguments(
	'-D_POSIX_C_SOURCE=200809L',
	'-D_DEFAULT_SOURCE',
	'-DWLR_USE_UNSTABLE',
	'-DHAVE_STDLIB_H', '-DHAVE_STRING_H', '-DHAVE_UNISTD_H',
	'-DHAVE_SYS_STAT_H', '-DHAVE_SYS_TYPES_H', '-DHAVE_GRP_H', '-DHAVE_PWD_H',
	'-DHAVE_SIGNAL_H', '-DHAVE_FCNTL_H', '-DHAVE_LOCALE_H',
	cc.get_supported_arguments([
		'-Wno-unused-parameter',
		'-Wno-missing-field-initializers',
		'-Wno-sign-compare',
		'-Wundef',
		'-Wvla',
	]),
	language: 'c',
)

prefix = get_option('prefix')
datadir = prefix / get_option('datadir')
sysconfdir = prefix / get_option('sysconfdir')
owbox_datadir = datadir / 'owbox'

add_project_arguments(
	'-DOWBOX_VERSION="@0@"'.format(meson.project_version()),
	'-DOWBOX_DATADIR="@0@"'.format(owbox_datadir),
	'-DOWBOX_THEMEDIR="@0@"'.format(owbox_datadir / 'themes'),
	'-DOWBOX_SYSCONFDIR="@0@"'.format(sysconfdir / 'xdg'),
	'-DCONFIGDIR="@0@"'.format(sysconfdir / 'xdg'),
	'-DDATADIR="@0@"'.format(datadir),
	'-DDEFAULT_THEME="Clearlooks"',
	'-DUSE_LIBRSVG',
	language: 'c',
)

glib = dependency('glib-2.0')
cairo = dependency('cairo')
pangocairo = dependency('pangocairo')
librsvg = dependency('librsvg-2.0')
libxml2 = dependency('libxml-2.0')
math = cc.find_library('m')

root_inc = include_directories('src')

obt_lib = static_library(
	'obt',
	files('src/obt/paths.c', 'src/obt/xml.c'),
	include_directories: root_inc,
	dependencies: [glib, libxml2],
)
obt = declare_dependency(
	link_with: obt_lib,
	include_directories: root_inc,
	dependencies: [glib, libxml2],
)

render_lib = static_library(
	'obrender',
	files(
		'src/render/button.c',
		'src/render/color.c',
		'src/render/font.c',
		'src/render/gradient.c',
		'src/render/image.c',
		'src/render/imagecache.c',
		'src/render/instance.c',
		'src/render/mask.c',
		'src/render/render.c',
		'src/render/theme.c',
		'src/render/xrm.c',
	),
	include_directories: root_inc,
	dependencies: [glib, cairo, pangocairo, librsvg, obt, math],
	c_args: cc.get_supported_arguments(['-Wno-unused-but-set-variable']),
)
obrender = declare_dependency(
	link_with: render_lib,
	include_directories: [root_inc, include_directories('src/render')],
	dependencies: [glib, cairo, pangocairo, obt],
)

executable(
	'owbox-theme-render',
	files('tools/theme-render.c'),
	dependencies: [obrender],
	install: false,
)
