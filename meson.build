project(
	'owbox',
	'c',
	version: '0.4.0',
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
	c_args: ['-DG_LOG_DOMAIN="Obt"'],
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
	c_args: cc.get_supported_arguments(['-Wno-unused-but-set-variable']) +
		['-DG_LOG_DOMAIN="ObRender"'],
)
obrender = declare_dependency(
	link_with: render_lib,
	include_directories: [root_inc, include_directories('src/render')],
	dependencies: [glib, cairo, pangocairo, obt],
)

wayland_server = dependency('wayland-server')
wayland_protos = dependency('wayland-protocols')
wlroots = dependency('wlroots-0.20', version: ['>=0.20.0', '<0.21.0'])
xkbcommon = dependency('xkbcommon')
libinput = dependency('libinput')
pixman = dependency('pixman-1')
libdrm = dependency('libdrm').partial_dependency(compile_args: true, includes: true)
gio = dependency('gio-2.0')
gio_unix = dependency('gio-unix-2.0')
sfdo_basedir = dependency('libsfdo-basedir')
sfdo_desktop = dependency('libsfdo-desktop')
sfdo_icon = dependency('libsfdo-icon')
# X applications through Xwayland, wlroots' xwm uses these for the properties
xcb = dependency('xcb')
xcb_ewmh = dependency('xcb-ewmh')
xcb_icccm = dependency('xcb-icccm')

wayland_scanner = find_program(
	dependency('wayland-scanner', native: true).get_variable('wayland_scanner'),
	native: true,
)
wl_protocol_dir = wayland_protos.get_variable('pkgdatadir')

protocols = [
	wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml',
	'protocols/wlr-layer-shell-unstable-v1.xml',
	'protocols/wlr-output-power-management-unstable-v1.xml',
]
protocols_src = []
foreach xml : protocols
	protocols_src += custom_target(
		xml.underscorify() + '_server_h',
		input: xml,
		output: '@BASENAME@-protocol.h',
		command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
	)
endforeach

owbox_sources = files(
	'src/actions.c',
	'src/app_menu.c',
	'src/share_menu.c',
	'src/actions/addremovedesktop.c',
	'src/actions/all.c',
	'src/actions/breakchroot.c',
	'src/actions/close.c',
	'src/actions/cyclewindows.c',
	'src/actions/debug.c',
	'src/actions/decorations.c',
	'src/actions/desktop.c',
	'src/actions/directionalwindows.c',
	'src/actions/dock.c',
	'src/actions/dockautohide.c',
	'src/actions/execute.c',
	'src/actions/exit.c',
	'src/actions/focus.c',
	'src/actions/focusfallback.c',
	'src/actions/focustobottom.c',
	'src/actions/fullscreen.c',
	'src/actions/growtoedge.c',
	'src/actions/iconify.c',
	'src/actions/if.c',
	'src/actions/kill.c',
	'src/actions/layer.c',
	'src/actions/lower.c',
	'src/actions/maximize.c',
	'src/actions/move.c',
	'src/actions/moverelative.c',
	'src/actions/moveresizeto.c',
	'src/actions/movetoedge.c',
	'src/actions/omnipresent.c',
	'src/actions/raise.c',
	'src/actions/raiselower.c',
	'src/actions/reconfigure.c',
	'src/actions/resize.c',
	'src/actions/resizerelative.c',
	'src/actions/restart.c',
	'src/actions/shade.c',
	'src/actions/shadelowerraise.c',
	'src/actions/showdesktop.c',
	'src/actions/showmenu.c',
	'src/actions/snap.c',
	'src/actions/togglepanels.c',
	'src/actions/unfocus.c',
	'src/buffer.c',
	'src/capture.c',
	'src/client.c',
	'src/client_list_combined_menu.c',
	'src/client_list_menu.c',
	'src/client_menu.c',
	'src/config.c',
	'src/debug.c',
	'src/event.c',
	'src/focus.c',
	'src/focus_cycle.c',
	'src/focus_cycle_indicator.c',
	'src/focus_cycle_popup.c',
	'src/frame.c',
	'src/framerender.c',
	'src/fwin.c',
	'src/grab.c',
	'src/icons.c',
	'src/ipc.c',
	'src/keyboard.c',
	'src/keytree.c',
	'src/layers.c',
	'src/menu.c',
	'src/menuframe.c',
	'src/mouse.c',
	'src/moveresize.c',
	'src/obt/keyboard.c',
	'src/openbox.c',
	'src/ping.c',
	'src/output.c',
	'src/place.c',
	'src/place_overlap.c',
	'src/popup.c',
	'src/prompt.c',
	'src/resist.c',
	'src/screen.c',
	'src/seat.c',
	'src/server.c',
	'src/session.c',
	'src/idle.c',
	'src/panels.c',
	'src/snap.c',
	'src/stacking.c',
	'src/toplevel.c',
	'src/translate.c',
	'src/window.c',
	'src/xdg.c',
	'src/xwayland.c',
)

executable(
	'owbox',
	owbox_sources + protocols_src,
	dependencies: [
		obrender, obt, wayland_server, wlroots, xkbcommon, libinput, pixman,
		libdrm, gio, gio_unix, libxml2, math, sfdo_basedir, sfdo_desktop,
		sfdo_icon, librsvg, xcb, xcb_ewmh, xcb_icccm,
	],
	include_directories: [root_inc, include_directories('src/render')],
	c_args: ['-DG_LOG_DOMAIN="owbox"'],
	install: true,
)

executable(
	'owbox-theme-render',
	files('tools/theme-render.c'),
	dependencies: [obrender],
	# owbox-settings shows theme previews with it
	install: true,
)

# the test tool for sending synthetic input
wayland_client = dependency('wayland-client')
input_protocols = []
foreach xml : ['protocols/virtual-keyboard-unstable-v1.xml',
               'protocols/wlr-virtual-pointer-unstable-v1.xml']
	input_protocols += custom_target(
		xml.underscorify() + '_client_h',
		input: xml,
		output: '@BASENAME@-client-protocol.h',
		command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
	)
	input_protocols += custom_target(
		xml.underscorify() + '_client_c',
		input: xml,
		output: '@BASENAME@-protocol.c',
		command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
	)
endforeach
executable(
	'owbox-input',
	files('tools/owbox-input.c') + input_protocols,
	dependencies: [wayland_client, xkbcommon],
	install: false,
)

# a PipeWire consumer for tools/screencast-test.py
pipewire = dependency('libpipewire-0.3', required: false)
if pipewire.found()
	executable('owbox-pwcount', 'tools/pwcount.c',
		dependencies: [pipewire], install: false)
endif

# ---- data ----

bindir = prefix / get_option('bindir')
data_conf = configuration_data()
data_conf.set('bindir', bindir)
data_conf.set('sysconfdir', sysconfdir)

# the default configuration, in the same place as Openbox's
install_data(
	'data/rc.xml', 'data/menu.xml', 'data/environment', 'data/autostart',
	install_dir: sysconfdir / 'xdg' / 'owbox',
)
install_data('data/owbox-terminal', 'data/owbox-screenshot',
	'data/owbox-volume', 'data/owbox-brightness', 'data/owbox-launcher',
	install_dir: bindir,
	install_mode: 'rwxr-xr-x')
configure_file(
	input: 'data/owbox-session',
	output: 'owbox-session',
	configuration: data_conf,
	install_dir: bindir,
	install_mode: 'rwxr-xr-x',
)
# a session for display managers
configure_file(
	input: 'data/owbox.desktop',
	output: 'owbox.desktop',
	configuration: data_conf,
	install_dir: datadir / 'wayland-sessions',
)
install_data('data/owbox-portals.conf',
	install_dir: datadir / 'xdg-desktop-portal')
# the session's systemd target (graphical-session.target)
systemd = dependency('systemd', required: false)
systemd_user_unit_dir = systemd.found() ? \
	systemd.get_variable('systemd_user_unit_dir',
		pkgconfig_define: ['prefix', get_option('prefix')]) : \
	get_option('prefix') / 'lib' / 'systemd' / 'user'
install_data('data/owbox-session.target', install_dir: systemd_user_unit_dir)
# owbox's menu for choosing what to share (xdg-desktop-portal-wlr reads its
# settings for each desktop from here)
install_data('data/xdg-desktop-portal-wlr/owbox',
	install_dir: sysconfdir / 'xdg' / 'xdg-desktop-portal-wlr')
# example configurations of companion programs (waybar, fuzzel)
install_subdir('data/examples', install_dir: owbox_datadir,
	strip_directory: false)
# Openbox's themes, for when Openbox isn't installed
install_subdir('data/themes', install_dir: owbox_datadir,
	strip_directory: false)
install_man('docs/owbox.1')

# owbox Settings (Python and GTK 4)
settings_dir = owbox_datadir / 'settings'
install_subdir('settings/owbox_settings', install_dir: settings_dir,
	exclude_directories: ['__pycache__'])
settings_conf = configuration_data()
settings_conf.set('settingsdir', settings_dir)
configure_file(
	input: 'settings/owbox-settings',
	output: 'owbox-settings',
	configuration: settings_conf,
	install_dir: bindir,
	install_mode: 'rwxr-xr-x',
)
install_data('data/owbox-settings.desktop',
	install_dir: datadir / 'applications')
