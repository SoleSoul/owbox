/* owbox-theme-render: render pieces of an Openbox theme to a PNG, used to
   verify the obrender port without a running compositor.

   usage: owbox-theme-render THEME OUT.png [WIDTH] */

#include "render.h"
#include "theme.h"

#include <stdio.h>
#include <stdlib.h>

static void blit(cairo_t *cr, cairo_surface_t *s, int x, int y)
{
    if (!s) return;
    cairo_set_source_surface(cr, s, x, y);
    cairo_paint(cr);
    cairo_surface_destroy(s);
}

/* draw a titlebar like Openbox's frame does: title, then the label and
   buttons as (possibly ParentRelative) children */
static int titlebar(cairo_t *cr, RrTheme *t, int focused, int y, int width)
{
    RrAppearance *title = focused ? t->a_focused_title : t->a_unfocused_title;
    RrAppearance *label = focused ? t->a_focused_label : t->a_unfocused_label;
    RrButton *btns[] = { t->btn_iconify, t->btn_max, t->btn_close };
    int i, x, bw = t->button_size;
    int lw = width - 2 * t->paddingx - 3 * (bw + t->paddingx);

    blit(cr, RrPaintSurface(title, width, t->title_height), 0, y);

    label->surface.parent = title;
    label->surface.parentx = t->paddingx;
    label->surface.parenty = t->paddingy;
    label->texture[0].data.text.string = focused ? "Focused window" :
        "Unfocused window";
    blit(cr, RrPaintSurface(label, lw, t->label_height),
         t->paddingx, y + t->paddingy);

    x = t->paddingx + lw + t->paddingx;
    for (i = 0; i < 3; ++i) {
        RrAppearance *a = focused ? btns[i]->a_focused_unpressed :
            btns[i]->a_unfocused_unpressed;
        a->surface.parent = title;
        a->surface.parentx = x;
        a->surface.parenty = t->paddingy + 1;
        blit(cr, RrPaintSurface(a, bw, bw), x, y + t->paddingy + 1);
        x += bw + t->paddingx;
    }
    return t->title_height;
}

int main(int argc, char **argv)
{
    RrInstance *inst;
    RrTheme *t;
    cairo_surface_t *out;
    cairo_t *cr;
    int width = argc > 3 ? atoi(argv[3]) : 320, y = 0, h;

    if (argc < 3) {
        fprintf(stderr, "usage: %s THEME OUT.png [WIDTH]\n", argv[0]);
        return 1;
    }
    inst = RrInstanceNew();
    t = RrThemeNew(inst, argv[1], FALSE, NULL, NULL, NULL, NULL, NULL, NULL);
    if (!t) {
        fprintf(stderr, "cannot load theme %s\n", argv[1]);
        return 1;
    }

    h = 2 * t->title_height + 2 * t->handle_height + 40 +
        t->menu_title_height + 3 * (t->menu_font_height + 4);
    out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, h);
    cr = cairo_create(out);
    cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
    cairo_paint(cr);

    y += titlebar(cr, t, 1, y, width);
    blit(cr, RrPaintSurface(t->a_focused_handle, width, t->handle_height), 0, y);
    y += t->handle_height + 10;
    y += titlebar(cr, t, 0, y, width);
    blit(cr, RrPaintSurface(t->a_unfocused_handle, width, t->handle_height), 0, y);
    y += t->handle_height + 10;

    /* a menu title and items */
    t->a_menu_text_title->texture[0].data.text.string = "Openbox 3";
    blit(cr, RrPaintSurface(t->a_menu_title, width / 2, t->menu_title_height),
         0, y);
    {
        RrAppearance *items = t->a_menu;
        int ih = t->menu_font_height + 4;
        cairo_surface_t *bg;
        RrPaintBackground(items, width / 2, 3 * ih);
        bg = RrPaintSurface(items, width / 2, 3 * ih);
        blit(cr, bg, 0, y + t->menu_title_height);
        t->a_menu_selected->surface.parent = items;
        t->a_menu_selected->surface.parentx = 0;
        t->a_menu_selected->surface.parenty = ih;
        t->a_menu_text_normal->surface.parent = items;
        t->a_menu_text_normal->surface.parentx = 0;
        t->a_menu_text_normal->surface.parenty = 0;
        t->a_menu_text_normal->texture[0].data.text.string = "Terminal";
        blit(cr, RrPaintSurface(t->a_menu_text_normal, width / 2, ih),
             0, y + t->menu_title_height);
        blit(cr, RrPaintSurface(t->a_menu_selected, width / 2, ih),
             0, y + t->menu_title_height + ih);
        t->a_menu_text_selected->surface.parent = t->a_menu_selected;
        t->a_menu_text_selected->surface.parentx = 0;
        t->a_menu_text_selected->surface.parenty = 0;
        t->a_menu_text_selected->texture[0].data.text.string = "Web browser";
        blit(cr, RrPaintSurface(t->a_menu_text_selected, width / 2, ih),
             0, y + t->menu_title_height + ih);
    }

    cairo_destroy(cr);
    cairo_surface_write_to_png(out, argv[2]);
    cairo_surface_destroy(out);
    RrThemeFree(t);
    RrInstanceFree(inst);
    return 0;
}
