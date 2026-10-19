/* owbox-input: send synthetic pointer and keyboard input to a Wayland
   compositor with the virtual pointer and virtual keyboard protocols, for
   testing owbox without input devices.

   usage: owbox-input COMMAND...
     extent W H        the size of the layout for "move" (default 1280 720)
     move X Y          put the pointer at X,Y
     down BUTTON       press left|middle|right
     up BUTTON         release it
     click BUTTON      press and release
     dclick BUTTON     double click
     drag X Y          move to X,Y in steps (with buttons held)
     scroll up|down|left|right
     keydown KEY       press a key (an xkb keysym name, eg Alt_L, a, Return)
     keyup KEY         release it
     key COMBO         press and release a combo, eg alt+Tab, ctrl+alt+Left
     type TEXT         type text (letters, digits, space)
     sleep MS          wait
     run COMMAND       run a shell command (eg a screenshot) and wait for it */

#define _GNU_SOURCE
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

static struct wl_seat *seat;
static struct zwlr_virtual_pointer_manager_v1 *vp_manager;
static struct zwp_virtual_keyboard_manager_v1 *vk_manager;
static struct zwlr_virtual_pointer_v1 *pointer;
static struct zwp_virtual_keyboard_v1 *keyboard;
static struct wl_display *display;
static struct xkb_keymap *keymap;
static struct xkb_state *state;
static int ext_w = 1280, ext_h = 720;
static int cur_x, cur_y;

static void global(void *data, struct wl_registry *r, uint32_t name,
                   const char *iface, uint32_t version)
{
    if (!strcmp(iface, wl_seat_interface.name) && !seat)
        seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
    else if (!strcmp(iface, zwlr_virtual_pointer_manager_v1_interface.name))
        vp_manager = wl_registry_bind(
            r, name, &zwlr_virtual_pointer_manager_v1_interface, 1);
    else if (!strcmp(iface, zwp_virtual_keyboard_manager_v1_interface.name))
        vk_manager = wl_registry_bind(
            r, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
}

static void global_remove(void *data, struct wl_registry *r, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
    global, global_remove
};

static uint32_t now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void msleep(int ms)
{
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
    wl_display_roundtrip(display);
    nanosleep(&t, NULL);
}

static uint32_t button_code(const char *s)
{
    if (!strcmp(s, "left")) return BTN_LEFT;
    if (!strcmp(s, "middle")) return BTN_MIDDLE;
    if (!strcmp(s, "right")) return BTN_RIGHT;
    fprintf(stderr, "unknown button %s\n", s);
    exit(1);
}

static void move_to(int x, int y)
{
    cur_x = x;
    cur_y = y;
    zwlr_virtual_pointer_v1_motion_absolute(pointer, now(), x, y,
                                            ext_w, ext_h);
    zwlr_virtual_pointer_v1_frame(pointer);
    wl_display_roundtrip(display);
}

static void button(uint32_t b, int pressed)
{
    zwlr_virtual_pointer_v1_button(pointer, now(), b, pressed ?
                                   WL_POINTER_BUTTON_STATE_PRESSED :
                                   WL_POINTER_BUTTON_STATE_RELEASED);
    zwlr_virtual_pointer_v1_frame(pointer);
    wl_display_roundtrip(display);
}

/* find the keycode that produces a keysym at shift level 0 or 1 */
static xkb_keycode_t find_keycode(xkb_keysym_t sym, int *shift)
{
    xkb_keycode_t kc, min = xkb_keymap_min_keycode(keymap),
        max = xkb_keymap_max_keycode(keymap);
    int level;

    for (level = 0; level < 2; ++level)
        for (kc = min; kc <= max; ++kc) {
            const xkb_keysym_t *syms;
            int n = xkb_keymap_key_get_syms_by_level(keymap, kc, 0, level,
                                                     &syms);
            if (n > 0 && syms[0] == sym) {
                *shift = level;
                return kc;
            }
        }
    return 0;
}

static void send_mods(void)
{
    zwp_virtual_keyboard_v1_modifiers(
        keyboard,
        xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED),
        xkb_state_serialize_mods(state, XKB_STATE_MODS_LATCHED),
        xkb_state_serialize_mods(state, XKB_STATE_MODS_LOCKED),
        xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE));
}

static void keycode_event(xkb_keycode_t kc, int pressed)
{
    zwp_virtual_keyboard_v1_key(keyboard, now(), kc - 8, pressed ?
                                WL_KEYBOARD_KEY_STATE_PRESSED :
                                WL_KEYBOARD_KEY_STATE_RELEASED);
    xkb_state_update_key(state, kc, pressed ? XKB_KEY_DOWN : XKB_KEY_UP);
    send_mods();
    wl_display_roundtrip(display);
}

static const char* alias(const char *s)
{
    if (!strcasecmp(s, "alt")) return "Alt_L";
    if (!strcasecmp(s, "ctrl") || !strcasecmp(s, "control"))
        return "Control_L";
    if (!strcasecmp(s, "shift")) return "Shift_L";
    if (!strcasecmp(s, "super") || !strcasecmp(s, "win")) return "Super_L";
    return s;
}

static xkb_keycode_t name_keycode(const char *name, int *shift)
{
    xkb_keysym_t sym = xkb_keysym_from_name(alias(name), 0);
    xkb_keycode_t kc;
    if (sym == XKB_KEY_NoSymbol)
        sym = xkb_keysym_from_name(alias(name), XKB_KEYSYM_CASE_INSENSITIVE);
    kc = sym ? find_keycode(sym, shift) : 0;
    if (!kc) {
        fprintf(stderr, "unknown key %s\n", name);
        exit(1);
    }
    return kc;
}

static void key_combo(const char *combo)
{
    char *s = strdup(combo), *tok, *save = NULL;
    xkb_keycode_t kcs[8];
    int n = 0, i, shift;

    for (tok = strtok_r(s, "+", &save); tok && n < 8;
         tok = strtok_r(NULL, "+", &save))
        kcs[n++] = name_keycode(tok, &shift);
    for (i = 0; i < n; ++i)
        keycode_event(kcs[i], 1);
    for (i = n - 1; i >= 0; --i)
        keycode_event(kcs[i], 0);
    free(s);
}

static void type_text(const char *text)
{
    xkb_keycode_t shift_kc = name_keycode("Shift_L", &(int){0});
    for (; *text; ++text) {
        char name[2] = { *text, 0 };
        int shift = 0;
        xkb_keycode_t kc = name_keycode(*text == ' ' ? "space" : name,
                                        &shift);
        if (shift) keycode_event(shift_kc, 1);
        keycode_event(kc, 1);
        keycode_event(kc, 0);
        if (shift) keycode_event(shift_kc, 0);
    }
}

static void setup_keyboard(void)
{
    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    char *str;
    size_t size;
    int fd;

    keymap = xkb_keymap_new_from_names(ctx, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    state = xkb_state_new(keymap);
    str = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    size = strlen(str) + 1;
    fd = memfd_create("keymap", 0);
    if (write(fd, str, size) != (ssize_t)size) {
        perror("write");
        exit(1);
    }
    zwp_virtual_keyboard_v1_keymap(keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                                   fd, size);
    close(fd);
    free(str);
}

int main(int argc, char **argv)
{
    struct wl_registry *registry;
    int i;

    if (!(display = wl_display_connect(NULL))) {
        fprintf(stderr, "cannot connect to the Wayland display\n");
        return 1;
    }
    registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);
    if (!seat || !vp_manager || !vk_manager) {
        fprintf(stderr, "the compositor lacks virtual input support\n");
        return 1;
    }
    pointer = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(
        vp_manager, seat);
    keyboard = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(
        vk_manager, seat);
    setup_keyboard();
    wl_display_roundtrip(display);

    for (i = 1; i < argc; ++i) {
        const char *c = argv[i];
#define ARG() (++i < argc ? argv[i] : (fprintf(stderr, "%s needs an argument\n", c), exit(1), ""))
        if (!strcmp(c, "extent")) {
            ext_w = atoi(ARG());
            ext_h = atoi(ARG());
        } else if (!strcmp(c, "move")) {
            int x = atoi(ARG());
            move_to(x, atoi(ARG()));
        } else if (!strcmp(c, "down"))
            button(button_code(ARG()), 1);
        else if (!strcmp(c, "up"))
            button(button_code(ARG()), 0);
        else if (!strcmp(c, "click")) {
            uint32_t b = button_code(ARG());
            button(b, 1);
            button(b, 0);
        } else if (!strcmp(c, "dclick")) {
            uint32_t b = button_code(ARG());
            button(b, 1);
            button(b, 0);
            button(b, 1);
            button(b, 0);
        } else if (!strcmp(c, "drag")) {
            int x = atoi(ARG()), y = atoi(ARG()), s;
            int sx = cur_x, sy = cur_y;
            for (s = 1; s <= 20; ++s) {
                move_to(sx + (x - sx) * s / 20, sy + (y - sy) * s / 20);
                msleep(10);
            }
        } else if (!strcmp(c, "scroll")) {
            const char *d = ARG();
            int vert = !strcmp(d, "up") || !strcmp(d, "down");
            int neg = !strcmp(d, "up") || !strcmp(d, "left");
            zwlr_virtual_pointer_v1_axis_source(pointer,
                                                WL_POINTER_AXIS_SOURCE_WHEEL);
            zwlr_virtual_pointer_v1_axis_discrete(
                pointer, now(), vert ? WL_POINTER_AXIS_VERTICAL_SCROLL :
                WL_POINTER_AXIS_HORIZONTAL_SCROLL,
                wl_fixed_from_int(neg ? -15 : 15), neg ? -1 : 1);
            zwlr_virtual_pointer_v1_frame(pointer);
            wl_display_roundtrip(display);
        } else if (!strcmp(c, "keydown")) {
            int shift;
            keycode_event(name_keycode(ARG(), &shift), 1);
        } else if (!strcmp(c, "keyup")) {
            int shift;
            keycode_event(name_keycode(ARG(), &shift), 0);
        } else if (!strcmp(c, "key"))
            key_combo(ARG());
        else if (!strcmp(c, "type"))
            type_text(ARG());
        else if (!strcmp(c, "sleep"))
            msleep(atoi(ARG()));
        else if (!strcmp(c, "run")) {
            wl_display_roundtrip(display);
            if (system(ARG()) != 0)
                fprintf(stderr, "command failed\n");
        }
        else {
            fprintf(stderr, "unknown command %s\n", c);
            return 1;
        }
        msleep(30);
    }
    wl_display_roundtrip(display);
    return 0;
}
