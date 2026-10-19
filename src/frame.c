/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   frame.c for owbox (ported from the Openbox window manager)
   Copyright (c) 2006        Mikael Magnusson
   Copyright (c) 2003-2007   Dana Jansens

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   See the COPYING file for a copy of the GNU General Public License.
*/

#include "frame.h"
#include "client.h"
#include "openbox.h"
#include "debug.h"
#include "config.h"
#include "framerender.h"
#include "focus_cycle.h"
#include "focus_cycle_indicator.h"
#include "moveresize.h"
#include "screen.h"
#include "server.h"
#include "gravity.h"
#include "buffer.h"
#include "fwin.h"
#include "render/theme.h"

#include <wlr/types/wlr_scene.h>

#define FRAME_ANIMATE_ICONIFY_TIME 150000 /* .15 seconds */
#define FRAME_ANIMATE_ICONIFY_STEP_TIME (1000 / 60) /* 60 Hz */

#define FRAME_HANDLE_Y(f) (f->size.top + f->client->area.height + f->cbwidth_b)

static void flash_done(gpointer data);
static gboolean flash_timeout(gpointer data);

static void layout_title(ObFrame *self);
static void set_theme_statics(ObFrame *self);
static void free_theme_statics(ObFrame *self);
static gboolean frame_animate_iconify(gpointer self);
static void frame_adjust_cursors(ObFrame *self);

/* The equivalent of XCreateWindow for the frame's element windows */
static ObFWin* createWindow(ObFrame *self, ObFWin *parent)
{
    return fwin_new(parent, server.stack_tree[OB_STACKING_LAYER_NORMAL],
                    parent ? OB_NODE_FRAME_PART : OB_NODE_WINDOW,
                    CLIENT_AS_WINDOW(self->client));
}

static void destroyWindow(ObFWin *w)
{
    fwin_destroy(w);
}

ObFrame *frame_new(ObClient *client)
{
    ObFrame *self;

    self = g_slice_new0(ObFrame);
    self->client = client;

    /* create the non-visible decor windows */
    self->window = createWindow(self, NULL);

    /* create the visible decor windows */

    self->backback = createWindow(self, self->window);
    self->backfront = createWindow(self, self->backback);

    self->innerleft = createWindow(self, self->window);
    self->innertop = createWindow(self, self->window);
    self->innerright = createWindow(self, self->window);
    self->innerbottom = createWindow(self, self->window);

    self->innerblb = createWindow(self, self->innerbottom);
    self->innerbrb = createWindow(self, self->innerbottom);
    self->innerbll = createWindow(self, self->innerleft);
    self->innerbrr = createWindow(self, self->innerright);

    self->title = createWindow(self, self->window);
    self->titleleft = createWindow(self, self->window);
    self->titletop = createWindow(self, self->window);
    self->titletopleft = createWindow(self, self->window);
    self->titletopright = createWindow(self, self->window);
    self->titleright = createWindow(self, self->window);
    self->titlebottom = createWindow(self, self->window);

    self->topresize = createWindow(self, self->title);
    self->tltresize = createWindow(self, self->title);
    self->tllresize = createWindow(self, self->title);
    self->trtresize = createWindow(self, self->title);
    self->trrresize = createWindow(self, self->title);
    fwin_input_only(self->topresize);
    fwin_input_only(self->tltresize);
    fwin_input_only(self->tllresize);
    fwin_input_only(self->trtresize);
    fwin_input_only(self->trrresize);

    self->left = createWindow(self, self->window);
    self->right = createWindow(self, self->window);

    self->label = createWindow(self, self->title);
    self->max = createWindow(self, self->title);
    self->close = createWindow(self, self->title);
    self->desk = createWindow(self, self->title);
    self->shade = createWindow(self, self->title);
    self->icon = createWindow(self, self->title);
    self->iconify = createWindow(self, self->title);

    self->handle = createWindow(self, self->window);
    self->lgrip = createWindow(self, self->handle);
    self->rgrip = createWindow(self, self->handle);

    self->handleleft = createWindow(self, self->handle);
    self->handleright = createWindow(self, self->handle);

    self->handletop = createWindow(self, self->window);
    self->handlebottom = createWindow(self, self->window);
    self->lgripleft = createWindow(self, self->window);
    self->lgriptop = createWindow(self, self->window);
    self->lgripbottom = createWindow(self, self->window);
    self->rgripright = createWindow(self, self->window);
    self->rgriptop = createWindow(self, self->window);
    self->rgripbottom = createWindow(self, self->window);

    self->focused = FALSE;

    /* the other stuff is shown based on decor settings */
    fwin_map(self->label);
    /* the backback window would show through clients with an alpha channel,
       which X never did (the client covered it), so it's never mapped */

    self->max_press = self->close_press = self->desk_press =
        self->iconify_press = self->shade_press = FALSE;
    self->max_hover = self->close_hover = self->desk_hover =
        self->iconify_hover = self->shade_hover = FALSE;

    /* make sure the size will be different the first time, so the extent hints
       will be set */
    STRUT_SET(self->oldsize, -1, -1, -1, -1);

    set_theme_statics(self);

    return self;
}

static void set_theme_statics(ObFrame *self)
{
    /* set colors/appearance/sizes for stuff that doesn't change */
    fwin_resize(self->max,
                  ob_rr_theme->button_size, ob_rr_theme->button_size);
    fwin_resize(self->iconify,
                  ob_rr_theme->button_size, ob_rr_theme->button_size);
    fwin_resize(self->icon,
                  ob_rr_theme->button_size + 2, ob_rr_theme->button_size + 2);
    fwin_resize(self->close,
                  ob_rr_theme->button_size, ob_rr_theme->button_size);
    fwin_resize(self->desk,
                  ob_rr_theme->button_size, ob_rr_theme->button_size);
    fwin_resize(self->shade,
                  ob_rr_theme->button_size, ob_rr_theme->button_size);
    fwin_resize(self->tltresize,
                  ob_rr_theme->grip_width, ob_rr_theme->paddingy + 1);
    fwin_resize(self->trtresize,
                  ob_rr_theme->grip_width, ob_rr_theme->paddingy + 1);
    fwin_resize(self->tllresize,
                  ob_rr_theme->paddingx + 1, ob_rr_theme->title_height);
    fwin_resize(self->trrresize,
                  ob_rr_theme->paddingx + 1, ob_rr_theme->title_height);
}

static void free_theme_statics(ObFrame *self)
{
}

void frame_free(ObFrame *self)
{
    free_theme_statics(self);

    if (self->iconify_animation_buffer)
        wlr_scene_node_destroy(&self->iconify_animation_buffer->node);

    /* destroying the frame's tree destroys all the element windows' trees,
       free their structs */
#define FREE_FWIN(w) fwin_free(self->w)
    FREE_FWIN(backback); FREE_FWIN(backfront);
    FREE_FWIN(innerleft); FREE_FWIN(innertop); FREE_FWIN(innerright);
    FREE_FWIN(innerbottom); FREE_FWIN(innerblb); FREE_FWIN(innerbrb);
    FREE_FWIN(innerbll); FREE_FWIN(innerbrr);
    FREE_FWIN(title); FREE_FWIN(titleleft); FREE_FWIN(titletop);
    FREE_FWIN(titletopleft); FREE_FWIN(titletopright); FREE_FWIN(titleright);
    FREE_FWIN(titlebottom);
    FREE_FWIN(topresize); FREE_FWIN(tltresize); FREE_FWIN(tllresize);
    FREE_FWIN(trtresize); FREE_FWIN(trrresize);
    FREE_FWIN(left); FREE_FWIN(right);
    FREE_FWIN(label); FREE_FWIN(max); FREE_FWIN(close); FREE_FWIN(desk);
    FREE_FWIN(shade); FREE_FWIN(icon); FREE_FWIN(iconify);
    FREE_FWIN(handle); FREE_FWIN(lgrip); FREE_FWIN(rgrip);
    FREE_FWIN(handleleft); FREE_FWIN(handleright); FREE_FWIN(handletop);
    FREE_FWIN(handlebottom);
    FREE_FWIN(lgripleft); FREE_FWIN(lgriptop); FREE_FWIN(lgripbottom);
    FREE_FWIN(rgripright); FREE_FWIN(rgriptop); FREE_FWIN(rgripbottom);
#undef FREE_FWIN
    destroyWindow(self->window);

    g_slice_free(ObFrame, self);
}

void frame_show(ObFrame *self)
{
    if (!self->visible) {
        self->visible = TRUE;
        framerender_frame(self);
        client_set_surface_visible(self->client, TRUE);
        fwin_map(self->window);
    }
}

void frame_hide(ObFrame *self)
{
    if (self->visible) {
        self->visible = FALSE;
        if (!frame_iconify_animating(self))
            fwin_unmap(self->window);
        client_set_surface_visible(self->client, FALSE);
    }
}

void frame_adjust_theme(ObFrame *self)
{
    free_theme_statics(self);
    set_theme_statics(self);
}

void frame_adjust_shape(ObFrame *self)
{
    /* Wayland surfaces carry their own alpha, there is no shape to copy */
}

void frame_adjust_area(ObFrame *self, gboolean moved,
                       gboolean resized, gboolean fake)
{
    if (resized) {
        /* do this before changing the frame's status like max_horz max_vert */
        frame_adjust_cursors(self);

        self->functions = self->client->functions;
        self->decorations = self->client->decorations;
        self->max_horz = self->client->max_horz;
        self->max_vert = self->client->max_vert;
        self->shaded = self->client->shaded;

        if (self->decorations & OB_FRAME_DECOR_BORDER)
            self->bwidth = self->client->undecorated ?
                ob_rr_theme->ubwidth : ob_rr_theme->fbwidth;
        else
            self->bwidth = 0;

        if (self->decorations & OB_FRAME_DECOR_BORDER &&
            !self->client->undecorated)
        {
            self->cbwidth_l = self->cbwidth_r = ob_rr_theme->cbwidthx;
            self->cbwidth_t = self->cbwidth_b = ob_rr_theme->cbwidthy;
        } else
            self->cbwidth_l = self->cbwidth_t =
                self->cbwidth_r = self->cbwidth_b = 0;

        if (self->max_horz) {
            self->cbwidth_l = self->cbwidth_r = 0;
            self->width = self->client->area.width;
            if (self->max_vert)
                self->cbwidth_b = 0;
        } else
            self->width = self->client->area.width +
                self->cbwidth_l + self->cbwidth_r;

        /* some elements are sized based of the width, so don't let them have
           negative values */
        self->width = MAX(self->width,
                          (ob_rr_theme->grip_width + self->bwidth) * 2 + 1);

        STRUT_SET(self->size,
                  self->cbwidth_l + (!self->max_horz ? self->bwidth : 0),
                  self->cbwidth_t +
                  (!self->max_horz || !self->max_vert ? self->bwidth : 0),
                  self->cbwidth_r + (!self->max_horz ? self->bwidth : 0),
                  self->cbwidth_b +
                  (!self->max_horz || !self->max_vert ? self->bwidth : 0));

        if (self->decorations & OB_FRAME_DECOR_TITLEBAR)
            self->size.top += ob_rr_theme->title_height + self->bwidth;
        else if (self->max_horz && self->max_vert) {
            /* A maximized and undecorated window needs a border on the
               top of the window to let the user still undecorate/unmaximize the
               window via the client menu. */
            self->size.top += self->bwidth;
        }

        if (self->decorations & OB_FRAME_DECOR_HANDLE &&
            ob_rr_theme->handle_height > 0)
        {
            self->size.bottom += ob_rr_theme->handle_height + self->bwidth;
        }

        /* position/size and map/unmap all the windows */

        if (!fake) {
            gint innercornerheight =
                ob_rr_theme->grip_width - self->size.bottom;

            if (self->cbwidth_l) {
                fwin_move_resize(self->innerleft,
                                  self->size.left - self->cbwidth_l,
                                  self->size.top,
                                  self->cbwidth_l, self->client->area.height);

                fwin_map(self->innerleft);
            } else
                fwin_unmap(self->innerleft);

            if (self->cbwidth_l && innercornerheight > 0) {
                fwin_move_resize(self->innerbll,
                                  0,
                                  self->client->area.height - 
                                  (ob_rr_theme->grip_width -
                                   self->size.bottom),
                                  self->cbwidth_l,
                                  ob_rr_theme->grip_width - self->size.bottom);

                fwin_map(self->innerbll);
            } else
                fwin_unmap(self->innerbll);

            if (self->cbwidth_r) {
                fwin_move_resize(self->innerright,
                                  self->size.left + self->client->area.width,
                                  self->size.top,
                                  self->cbwidth_r, self->client->area.height);

                fwin_map(self->innerright);
            } else
                fwin_unmap(self->innerright);

            if (self->cbwidth_r && innercornerheight > 0) {
                fwin_move_resize(self->innerbrr,
                                  0,
                                  self->client->area.height - 
                                  (ob_rr_theme->grip_width -
                                   self->size.bottom),
                                  self->cbwidth_r,
                                  ob_rr_theme->grip_width - self->size.bottom);

                fwin_map(self->innerbrr);
            } else
                fwin_unmap(self->innerbrr);

            if (self->cbwidth_t) {
                fwin_move_resize(self->innertop,
                                  self->size.left - self->cbwidth_l,
                                  self->size.top - self->cbwidth_t,
                                  self->client->area.width +
                                  self->cbwidth_l + self->cbwidth_r,
                                  self->cbwidth_t);

                fwin_map(self->innertop);
            } else
                fwin_unmap(self->innertop);

            if (self->cbwidth_b) {
                fwin_move_resize(self->innerbottom,
                                  self->size.left - self->cbwidth_l,
                                  self->size.top + self->client->area.height,
                                  self->client->area.width +
                                  self->cbwidth_l + self->cbwidth_r,
                                  self->cbwidth_b);

                fwin_move_resize(self->innerblb,
                                  0, 0,
                                  ob_rr_theme->grip_width + self->bwidth,
                                  self->cbwidth_b);
                fwin_move_resize(self->innerbrb,
                                  self->client->area.width +
                                  self->cbwidth_l + self->cbwidth_r -
                                  (ob_rr_theme->grip_width + self->bwidth),
                                  0,
                                  ob_rr_theme->grip_width + self->bwidth,
                                  self->cbwidth_b);

                fwin_map(self->innerbottom);
                fwin_map(self->innerblb);
                fwin_map(self->innerbrb);
            } else {
                fwin_unmap(self->innerbottom);
                fwin_unmap(self->innerblb);
                fwin_unmap(self->innerbrb);
            }

            if (self->bwidth) {
                gint titlesides;

                /* height of titleleft and titleright */
                titlesides = (!self->max_horz ? ob_rr_theme->grip_width : 0);

                fwin_move_resize(self->titletop,
                                  ob_rr_theme->grip_width + self->bwidth, 0,
                                  /* width + bwidth*2 - bwidth*2 - grips*2 */
                                  self->width - ob_rr_theme->grip_width * 2,
                                  self->bwidth);
                fwin_move_resize(self->titletopleft,
                                  0, 0,
                                  ob_rr_theme->grip_width + self->bwidth,
                                  self->bwidth);
                fwin_move_resize(self->titletopright,
                                  self->client->area.width +
                                  self->size.left + self->size.right -
                                  ob_rr_theme->grip_width - self->bwidth,
                                  0,
                                  ob_rr_theme->grip_width + self->bwidth,
                                  self->bwidth);

                if (titlesides > 0) {
                    fwin_move_resize(self->titleleft,
                                      0, self->bwidth,
                                      self->bwidth,
                                      titlesides);
                    fwin_move_resize(self->titleright,
                                      self->client->area.width +
                                      self->size.left + self->size.right -
                                      self->bwidth,
                                      self->bwidth,
                                      self->bwidth,
                                      titlesides);

                    fwin_map(self->titleleft);
                    fwin_map(self->titleright);
                } else {
                    fwin_unmap(self->titleleft);
                    fwin_unmap(self->titleright);
                }

                fwin_map(self->titletop);
                fwin_map(self->titletopleft);
                fwin_map(self->titletopright);

                if (self->decorations & OB_FRAME_DECOR_TITLEBAR) {
                    fwin_move_resize(self->titlebottom,
                                      (self->max_horz ? 0 : self->bwidth),
                                      ob_rr_theme->title_height + self->bwidth,
                                      self->width,
                                      self->bwidth);

                    fwin_map(self->titlebottom);
                } else
                    fwin_unmap(self->titlebottom);
            } else {
                fwin_unmap(self->titlebottom);

                fwin_unmap(self->titletop);
                fwin_unmap(self->titletopleft);
                fwin_unmap(self->titletopright);
                fwin_unmap(self->titleleft);
                fwin_unmap(self->titleright);
            }

            if (self->decorations & OB_FRAME_DECOR_TITLEBAR) {
                fwin_move_resize(self->title,
                                  (self->max_horz ? 0 : self->bwidth),
                                  self->bwidth,
                                  self->width, ob_rr_theme->title_height);

                fwin_map(self->title);

                if (self->decorations & OB_FRAME_DECOR_GRIPS) {
                    fwin_move_resize(self->topresize,
                                      ob_rr_theme->grip_width,
                                      0,
                                      self->width - ob_rr_theme->grip_width *2,
                                      ob_rr_theme->paddingy + 1);

                    fwin_move(self->tltresize, 0, 0);
                    fwin_move(self->tllresize, 0, 0);
                    fwin_move(self->trtresize,
                                self->width - ob_rr_theme->grip_width, 0);
                    fwin_move(self->trrresize,
                                self->width - ob_rr_theme->paddingx - 1, 0);

                    fwin_map(self->topresize);
                    fwin_map(self->tltresize);
                    fwin_map(self->tllresize);
                    fwin_map(self->trtresize);
                    fwin_map(self->trrresize);
                } else {
                    fwin_unmap(self->topresize);
                    fwin_unmap(self->tltresize);
                    fwin_unmap(self->tllresize);
                    fwin_unmap(self->trtresize);
                    fwin_unmap(self->trrresize);
                }
            } else
                fwin_unmap(self->title);
        }

        if ((self->decorations & OB_FRAME_DECOR_TITLEBAR))
            /* layout the title bar elements */
            layout_title(self);

        if (!fake) {
            gint sidebwidth = self->max_horz ? 0 : self->bwidth;

            if (self->bwidth && self->size.bottom) {
                fwin_move_resize(self->handlebottom,
                                  ob_rr_theme->grip_width +
                                  self->bwidth + sidebwidth,
                                  self->size.top + self->client->area.height +
                                  self->size.bottom - self->bwidth,
                                  self->width - (ob_rr_theme->grip_width +
                                                 sidebwidth) * 2,
                                  self->bwidth);


                if (sidebwidth) {
                    fwin_move_resize(self->lgripleft,
                                      0,
                                      self->size.top +
                                      self->client->area.height +
                                      self->size.bottom -
                                      (!self->max_horz ?
                                       ob_rr_theme->grip_width :
                                       self->size.bottom - self->cbwidth_b),
                                      self->bwidth,
                                      (!self->max_horz ?
                                       ob_rr_theme->grip_width :
                                       self->size.bottom - self->cbwidth_b));
                    fwin_move_resize(self->rgripright,
                                  self->size.left +
                                      self->client->area.width +
                                      self->size.right - self->bwidth,
                                      self->size.top +
                                      self->client->area.height +
                                      self->size.bottom -
                                      (!self->max_horz ?
                                       ob_rr_theme->grip_width :
                                       self->size.bottom - self->cbwidth_b),
                                      self->bwidth,
                                      (!self->max_horz ?
                                       ob_rr_theme->grip_width :
                                       self->size.bottom - self->cbwidth_b));

                    fwin_map(self->lgripleft);
                    fwin_map(self->rgripright);
                } else {
                    fwin_unmap(self->lgripleft);
                    fwin_unmap(self->rgripright);
                }

                fwin_move_resize(self->lgripbottom,
                                  sidebwidth,
                                  self->size.top + self->client->area.height +
                                  self->size.bottom - self->bwidth,
                                  ob_rr_theme->grip_width + self->bwidth,
                                  self->bwidth);
                fwin_move_resize(self->rgripbottom,
                                  self->size.left + self->client->area.width +
                                  self->size.right - self->bwidth - sidebwidth-
                                  ob_rr_theme->grip_width,
                                  self->size.top + self->client->area.height +
                                  self->size.bottom - self->bwidth,
                                  ob_rr_theme->grip_width + self->bwidth,
                                  self->bwidth);

                fwin_map(self->handlebottom);
                fwin_map(self->lgripbottom);
                fwin_map(self->rgripbottom);

                if (self->decorations & OB_FRAME_DECOR_HANDLE &&
                    ob_rr_theme->handle_height > 0)
                {
                    fwin_move_resize(self->handletop,
                                      ob_rr_theme->grip_width +
                                      self->bwidth + sidebwidth,
                                      FRAME_HANDLE_Y(self),
                                      self->width - (ob_rr_theme->grip_width +
                                                     sidebwidth) * 2,
                                      self->bwidth);
                    fwin_map(self->handletop);

                    if (self->decorations & OB_FRAME_DECOR_GRIPS) {
                        fwin_move_resize(self->handleleft,
                                          ob_rr_theme->grip_width,
                                          0,
                                          self->bwidth,
                                          ob_rr_theme->handle_height);
                        fwin_move_resize(self->handleright,
                                          self->width -
                                          ob_rr_theme->grip_width -
                                          self->bwidth,
                                          0,
                                          self->bwidth,
                                          ob_rr_theme->handle_height);

                        fwin_move_resize(self->lgriptop,
                                          sidebwidth,
                                          FRAME_HANDLE_Y(self),
                                          ob_rr_theme->grip_width +
                                          self->bwidth,
                                          self->bwidth);
                        fwin_move_resize(self->rgriptop,
                                          self->size.left +
                                          self->client->area.width +
                                          self->size.right - self->bwidth -
                                          sidebwidth - ob_rr_theme->grip_width,
                                          FRAME_HANDLE_Y(self),
                                          ob_rr_theme->grip_width +
                                          self->bwidth,
                                          self->bwidth);

                        fwin_map(self->handleleft);
                        fwin_map(self->handleright);
                        fwin_map(self->lgriptop);
                        fwin_map(self->rgriptop);
                    } else {
                        fwin_unmap(self->handleleft);
                        fwin_unmap(self->handleright);
                        fwin_unmap(self->lgriptop);
                        fwin_unmap(self->rgriptop);
                    }
                } else {
                    fwin_unmap(self->handleleft);
                    fwin_unmap(self->handleright);
                    fwin_unmap(self->lgriptop);
                    fwin_unmap(self->rgriptop);

                    fwin_unmap(self->handletop);
                }
            } else {
                fwin_unmap(self->handleleft);
                fwin_unmap(self->handleright);
                fwin_unmap(self->lgriptop);
                fwin_unmap(self->rgriptop);

                fwin_unmap(self->handletop);

                fwin_unmap(self->handlebottom);
                fwin_unmap(self->lgripleft);
                fwin_unmap(self->rgripright);
                fwin_unmap(self->lgripbottom);
                fwin_unmap(self->rgripbottom);
            }

            if (self->decorations & OB_FRAME_DECOR_HANDLE &&
                ob_rr_theme->handle_height > 0)
            {
                fwin_move_resize(self->handle,
                                  sidebwidth,
                                  FRAME_HANDLE_Y(self) + self->bwidth,
                                  self->width, ob_rr_theme->handle_height);
                fwin_map(self->handle);

                if (self->decorations & OB_FRAME_DECOR_GRIPS) {
                    fwin_move_resize(self->lgrip,
                                      0, 0,
                                      ob_rr_theme->grip_width,
                                      ob_rr_theme->handle_height);
                    fwin_move_resize(self->rgrip,
                                      self->width - ob_rr_theme->grip_width,
                                      0,
                                      ob_rr_theme->grip_width,
                                      ob_rr_theme->handle_height);

                    fwin_map(self->lgrip);
                    fwin_map(self->rgrip);
                } else {
                    fwin_unmap(self->lgrip);
                    fwin_unmap(self->rgrip);
                }
            } else {
                fwin_unmap(self->lgrip);
                fwin_unmap(self->rgrip);

                fwin_unmap(self->handle);
            }

            if (self->bwidth && !self->max_horz &&
                (self->client->area.height + self->size.top +
                 self->size.bottom) > ob_rr_theme->grip_width * 2)
            {
                fwin_move_resize(self->left,
                                  0,
                                  self->bwidth + ob_rr_theme->grip_width,
                                  self->bwidth,
                                  self->client->area.height +
                                  self->size.top + self->size.bottom -
                                  ob_rr_theme->grip_width * 2);

                fwin_map(self->left);
            } else
                fwin_unmap(self->left);

            if (self->bwidth && !self->max_horz &&
                (self->client->area.height + self->size.top +
                 self->size.bottom) > ob_rr_theme->grip_width * 2)
            {
                fwin_move_resize(self->right,
                                  self->client->area.width + self->cbwidth_l +
                                  self->cbwidth_r + self->bwidth,
                                  self->bwidth + ob_rr_theme->grip_width,
                                  self->bwidth,
                                  self->client->area.height +
                                  self->size.top + self->size.bottom -
                                  ob_rr_theme->grip_width * 2);

                fwin_map(self->right);
            } else
                fwin_unmap(self->right);

            fwin_move_resize(self->backback,
                              self->size.left, self->size.top,
                              self->client->area.width,
                              self->client->area.height);
        }
    }

    /* shading can change without being moved or resized */
    RECT_SET_SIZE(self->area,
                  self->client->area.width +
                  self->size.left + self->size.right,
                  (self->client->shaded ?
                   ob_rr_theme->title_height + self->bwidth * 2:
                   self->client->area.height +
                   self->size.top + self->size.bottom));

    if ((moved || resized) && !fake) {
        /* find the new coordinates, done after setting the frame.size, for
           frame_client_gravity. */
        self->area.x = self->client->area.x;
        self->area.y = self->client->area.y;
        frame_client_gravity(self, &self->area.x, &self->area.y);
    }

    if (!fake) {
        if (!frame_iconify_animating(self))
            /* move and resize the top level frame.
               shading can change without being moved or resized.

               but don't do this during an iconify animation. it will be
               reflected afterwards.
            */
            fwin_move_resize(self->window, self->area.x, self->area.y,
                             self->area.width, self->area.height);

        /* when the client has StaticGravity, it likes to move around.
           also this correctly positions the client when it maps.
           this also needs to be run when the frame's decorations sizes change!
        */
        frame_adjust_client_position(self);

        if (resized) {
            self->need_render = TRUE;
            framerender_frame(self);
            frame_adjust_shape(self);
        }

        if (!STRUT_EQUAL(self->size, self->oldsize)) {
            client_frame_extents_changed(self->client);
            self->oldsize = self->size;
        }

        /* if this occurs while we are focus cycling, the indicator needs to
           match the changes */
        if (focus_cycle_target == self->client)
            focus_cycle_update_indicator(self->client);
    }
    if (resized && (self->decorations & OB_FRAME_DECOR_TITLEBAR) &&
        self->label_width)
    {
        fwin_resize(self->label, self->label_width,
                      ob_rr_theme->label_height);
    }
}

static void frame_adjust_cursors(ObFrame *self)
{
    if ((self->functions & OB_CLIENT_FUNC_RESIZE) !=
        (self->client->functions & OB_CLIENT_FUNC_RESIZE) ||
        self->max_horz != self->client->max_horz ||
        self->max_vert != self->client->max_vert ||
        self->shaded != self->client->shaded)
    {
        gboolean r = (self->client->functions & OB_CLIENT_FUNC_RESIZE) &&
            !(self->client->max_horz && self->client->max_vert);
        gboolean topbot = !self->client->max_vert;
        gboolean sh = self->client->shaded;
        ObCursor a;

        /* these ones turn off when max vert, and some when shaded */
        a = (r && topbot && !sh ?
                             OB_CURSOR_NORTH : OB_CURSOR_NONE);
        self->topresize->cursor = a;
        self->titletop->cursor = a;
        a = (r && topbot ? OB_CURSOR_SOUTH : OB_CURSOR_NONE);
        self->handle->cursor = a;
        self->handletop->cursor = a;
        self->handlebottom->cursor = a;
        self->innerbottom->cursor = a;

        /* these ones change when shaded */
        a = (r ? (sh ? OB_CURSOR_WEST : OB_CURSOR_NORTHWEST) :
                             OB_CURSOR_NONE);
        self->titleleft->cursor = a;
        self->tltresize->cursor = a;
        self->tllresize->cursor = a;
        self->titletopleft->cursor = a;
        a = (r ? (sh ? OB_CURSOR_EAST : OB_CURSOR_NORTHEAST) :
                             OB_CURSOR_NONE);
        self->titleright->cursor = a;
        self->trtresize->cursor = a;
        self->trrresize->cursor = a;
        self->titletopright->cursor = a;

        /* these ones are pretty static */
        a = (r ? OB_CURSOR_WEST : OB_CURSOR_NONE);
        self->left->cursor = a;
        self->innerleft->cursor = a;
        a = (r ? OB_CURSOR_EAST : OB_CURSOR_NONE);
        self->right->cursor = a;
        self->innerright->cursor = a;
        a = (r ? OB_CURSOR_SOUTHWEST : OB_CURSOR_NONE);
        self->lgrip->cursor = a;
        self->handleleft->cursor = a;
        self->lgripleft->cursor = a;
        self->lgriptop->cursor = a;
        self->lgripbottom->cursor = a;
        self->innerbll->cursor = a;
        self->innerblb->cursor = a;
        a = (r ? OB_CURSOR_SOUTHEAST : OB_CURSOR_NONE);
        self->rgrip->cursor = a;
        self->handleright->cursor = a;
        self->rgripright->cursor = a;
        self->rgriptop->cursor = a;
        self->rgripbottom->cursor = a;
        self->innerbrr->cursor = a;
        self->innerbrb->cursor = a;
    }
}

void frame_adjust_client_area(ObFrame *self)
{
    /* adjust the window which is there to prevent flashing on unmap */
    fwin_move_resize(self->backfront, 0, 0,
                      self->client->area.width,
                      self->client->area.height);
}

void frame_adjust_client_position(ObFrame *self)
{
    client_set_surface_position(self->client, self->size.left,
                                self->size.top);
}

void frame_adjust_state(ObFrame *self)
{
    self->need_render = TRUE;
    framerender_frame(self);
}

void frame_adjust_focus(ObFrame *self, gboolean hilite)
{
    ob_debug_type(OB_DEBUG_FOCUS,
                  "Frame for %s has focus: %d",
                  self->client->title, hilite);
    self->focused = hilite;
    self->need_render = TRUE;
    framerender_frame(self);
}

void frame_adjust_title(ObFrame *self)
{
    self->need_render = TRUE;
    framerender_frame(self);
}

void frame_adjust_icon(ObFrame *self)
{
    self->need_render = TRUE;
    framerender_frame(self);
}

void frame_grab_client(ObFrame *self)
{
    /* DO NOT map the client window here. we used to do that, but it is bogus.
       we need to set up the client's dimensions and everything before we
       send a mapnotify or we create race conditions.
    */

    /* reparent the client to the frame, its surfaces go on top of all the
       decorations */
    client_reparent_surfaces(self->client, self->window->tree);
}

void frame_release_client(ObFrame *self)
{
    /* if there was any animation going on, kill it */
    if (self->iconify_animation_timer)
        g_source_remove(self->iconify_animation_timer);

    client_reparent_surfaces(self->client, NULL);

    if (self->flash_timer) g_source_remove(self->flash_timer);
}

/* is there anything present between us and the label? */
static gboolean is_button_present(ObFrame *self, const gchar *lc, gint dir) {
    for (; *lc != '\0' && lc >= config_title_layout; lc += dir) {
        if (*lc == ' ') continue; /* it was invalid */
        if (*lc == 'N' && self->decorations & OB_FRAME_DECOR_ICON)
            return TRUE;
        if (*lc == 'D' && self->decorations & OB_FRAME_DECOR_ALLDESKTOPS)
            return TRUE;
        if (*lc == 'S' && self->decorations & OB_FRAME_DECOR_SHADE)
            return TRUE;
        if (*lc == 'I' && self->decorations & OB_FRAME_DECOR_ICONIFY)
            return TRUE;
        if (*lc == 'M' && self->decorations & OB_FRAME_DECOR_MAXIMIZE)
            return TRUE;
        if (*lc == 'C' && self->decorations & OB_FRAME_DECOR_CLOSE)
            return TRUE;
        if (*lc == 'L') return FALSE;
    }
    return FALSE;
}

static void place_button(ObFrame *self, const char *lc, gint bwidth,
                         gint left, gint i,
                         gint *x, gint *button_on, gint *button_x)
{
  if (!(*button_on = is_button_present(self, lc, i)))
    return;

  self->label_width -= bwidth;
  if (i > 0)
    *button_x = *x;
  *x += i * bwidth;
  if (i < 0) {
    if (self->label_x <= left || *x > self->label_x) {
      *button_x = *x;
    } else {
      /* the button would have been drawn on top of another button */
      *button_on = FALSE;
      self->label_width += bwidth;
    }
  }
}

static void layout_title(ObFrame *self)
{
    gchar *lc;
    gint i;

    const gint bwidth = ob_rr_theme->button_size + ob_rr_theme->paddingx + 1;
    /* position of the leftmost button */
    const gint left = ob_rr_theme->paddingx + 1;
    /* position of the rightmost button */
    const gint right = self->width;

    /* turn them all off */
    self->icon_on = self->desk_on = self->shade_on = self->iconify_on =
        self->max_on = self->close_on = self->label_on = FALSE;
    self->label_width = self->width - (ob_rr_theme->paddingx + 1) * 2;
    self->leftmost = self->rightmost = OB_FRAME_CONTEXT_NONE;

    /* figure out what's being shown, find each element's position, and the
       width of the label

       do the ones before the label, then after the label,
       i will be +1 the first time through when working to the left,
       and -1 the second time through when working to the right */
    for (i = 1; i >= -1; i-=2) {
        gint x;
        ObFrameContext *firstcon;

        if (i > 0) {
            x = left;
            lc = config_title_layout;
            firstcon = &self->leftmost;
        } else {
            x = right;
            lc = config_title_layout + strlen(config_title_layout)-1;
            firstcon = &self->rightmost;
        }

        /* stop at the end of the string (or the label, which calls break) */
        for (; *lc != '\0' && lc >= config_title_layout; lc+=i) {
            if (*lc == 'L') {
                if (i > 0) {
                    self->label_on = TRUE;
                    self->label_x = x;
                }
                break; /* break the for loop, do other side of label */
            } else if (*lc == 'N') {
                if (firstcon) *firstcon = OB_FRAME_CONTEXT_ICON;
                /* icon is bigger than buttons */
                place_button(self, lc, bwidth + 2, left, i, &x, &self->icon_on, &self->icon_x);
            } else if (*lc == 'D') {
                if (firstcon) *firstcon = OB_FRAME_CONTEXT_ALLDESKTOPS;
                place_button(self, lc, bwidth, left, i, &x, &self->desk_on, &self->desk_x);
            } else if (*lc == 'S') {
                if (firstcon) *firstcon = OB_FRAME_CONTEXT_SHADE;
                place_button(self, lc, bwidth, left, i, &x, &self->shade_on, &self->shade_x);
            } else if (*lc == 'I') {
                if (firstcon) *firstcon = OB_FRAME_CONTEXT_ICONIFY;
                place_button(self, lc, bwidth, left, i, &x, &self->iconify_on, &self->iconify_x);
            } else if (*lc == 'M') {
                if (firstcon) *firstcon = OB_FRAME_CONTEXT_MAXIMIZE;
                place_button(self, lc, bwidth, left, i, &x, &self->max_on, &self->max_x);
            } else if (*lc == 'C') {
                if (firstcon) *firstcon = OB_FRAME_CONTEXT_CLOSE;
                place_button(self, lc, bwidth, left, i, &x, &self->close_on, &self->close_x);
            } else
                continue; /* don't set firstcon */
            firstcon = NULL;
        }
    }

    /* position and map the elements */
    if (self->icon_on) {
        fwin_map(self->icon);
        fwin_move(self->icon, self->icon_x,
                    ob_rr_theme->paddingy);
    } else
        fwin_unmap(self->icon);

    if (self->desk_on) {
        fwin_map(self->desk);
        fwin_move(self->desk, self->desk_x,
                    ob_rr_theme->paddingy + 1);
    } else
        fwin_unmap(self->desk);

    if (self->shade_on) {
        fwin_map(self->shade);
        fwin_move(self->shade, self->shade_x,
                    ob_rr_theme->paddingy + 1);
    } else
        fwin_unmap(self->shade);

    if (self->iconify_on) {
        fwin_map(self->iconify);
        fwin_move(self->iconify, self->iconify_x,
                    ob_rr_theme->paddingy + 1);
    } else
        fwin_unmap(self->iconify);

    if (self->max_on) {
        fwin_map(self->max);
        fwin_move(self->max, self->max_x,
                    ob_rr_theme->paddingy + 1);
    } else
        fwin_unmap(self->max);

    if (self->close_on) {
        fwin_map(self->close);
        fwin_move(self->close, self->close_x,
                    ob_rr_theme->paddingy + 1);
    } else
        fwin_unmap(self->close);

    if (self->label_on && self->label_width > 0) {
        fwin_map(self->label);
        fwin_move(self->label, self->label_x,
                    ob_rr_theme->paddingy);
    } else
        fwin_unmap(self->label);
}

gboolean frame_next_context_from_string(gchar *names, ObFrameContext *cx)
{
    gchar *p, *n;

    if (!*names) /* empty string */
        return FALSE;

    /* find the first space */
    for (p = names; *p; p = g_utf8_next_char(p)) {
        const gunichar c = g_utf8_get_char(p);
        if (g_unichar_isspace(c)) break;
    }

    if (p == names) {
        /* leading spaces in the string */
        n = g_utf8_next_char(names);
        if (!frame_next_context_from_string(n, cx))
            return FALSE;
    } else {
        n = p;
        if (*p) {
            /* delete the space with null zero(s) */
            while (n < g_utf8_next_char(p))
                *(n++) = '\0';
        }

        *cx = frame_context_from_string(names);

        /* find the next non-space */
        for (; *n; n = g_utf8_next_char(n)) {
            const gunichar c = g_utf8_get_char(n);
            if (!g_unichar_isspace(c)) break;
        }
    }

    /* delete everything we just read (copy everything at n to the start of
       the string */
    for (p = names; *n; ++p, ++n)
        *p = *n;
    *p = *n;

    return TRUE;
}

ObFrameContext frame_context_from_string(const gchar *name)
{
    if (!g_ascii_strcasecmp("Desktop", name))
        return OB_FRAME_CONTEXT_DESKTOP;
    else if (!g_ascii_strcasecmp("Root", name))
        return OB_FRAME_CONTEXT_ROOT;
    else if (!g_ascii_strcasecmp("Client", name))
        return OB_FRAME_CONTEXT_CLIENT;
    else if (!g_ascii_strcasecmp("Titlebar", name))
        return OB_FRAME_CONTEXT_TITLEBAR;
    else if (!g_ascii_strcasecmp("Frame", name))
        return OB_FRAME_CONTEXT_FRAME;
    else if (!g_ascii_strcasecmp("TLCorner", name))
        return OB_FRAME_CONTEXT_TLCORNER;
    else if (!g_ascii_strcasecmp("TRCorner", name))
        return OB_FRAME_CONTEXT_TRCORNER;
    else if (!g_ascii_strcasecmp("BLCorner", name))
        return OB_FRAME_CONTEXT_BLCORNER;
    else if (!g_ascii_strcasecmp("BRCorner", name))
        return OB_FRAME_CONTEXT_BRCORNER;
    else if (!g_ascii_strcasecmp("Top", name))
        return OB_FRAME_CONTEXT_TOP;
    else if (!g_ascii_strcasecmp("Bottom", name))
        return OB_FRAME_CONTEXT_BOTTOM;
    else if (!g_ascii_strcasecmp("Left", name))
        return OB_FRAME_CONTEXT_LEFT;
    else if (!g_ascii_strcasecmp("Right", name))
        return OB_FRAME_CONTEXT_RIGHT;
    else if (!g_ascii_strcasecmp("Maximize", name))
        return OB_FRAME_CONTEXT_MAXIMIZE;
    else if (!g_ascii_strcasecmp("AllDesktops", name))
        return OB_FRAME_CONTEXT_ALLDESKTOPS;
    else if (!g_ascii_strcasecmp("Shade", name))
        return OB_FRAME_CONTEXT_SHADE;
    else if (!g_ascii_strcasecmp("Iconify", name))
        return OB_FRAME_CONTEXT_ICONIFY;
    else if (!g_ascii_strcasecmp("Icon", name))
        return OB_FRAME_CONTEXT_ICON;
    else if (!g_ascii_strcasecmp("Close", name))
        return OB_FRAME_CONTEXT_CLOSE;
    else if (!g_ascii_strcasecmp("MoveResize", name))
        return OB_FRAME_CONTEXT_MOVE_RESIZE;
    else if (!g_ascii_strcasecmp("Dock", name))
        return OB_FRAME_CONTEXT_DOCK;

    return OB_FRAME_CONTEXT_NONE;
}

ObFrameContext frame_context(ObClient *client, ObFWin *win, gint x, gint y)
{
    ObFrame *self;

    if (moveresize_in_progress)
        return OB_FRAME_CONTEXT_MOVE_RESIZE;

    if (client == NULL) return OB_FRAME_CONTEXT_ROOT;
    if (win == NULL) {
        /* conceptually, this is the desktop, as far as users are
           concerned */
        if (client->type == OB_CLIENT_TYPE_DESKTOP)
            return OB_FRAME_CONTEXT_DESKTOP;
        return OB_FRAME_CONTEXT_CLIENT;
    }

    self = client->frame;

    /* when the user clicks in the corners of the titlebar and the client
       is fully maximized, then treat it like they clicked in the
       button that is there */
    if (self->max_horz && self->max_vert &&
        (win == self->title || win == self->titletop ||
         win == self->titleleft || win == self->titletopleft ||
         win == self->titleright || win == self->titletopright))
    {
        /* get the mouse coords in reference to the whole frame */
        gint fx = x;
        gint fy = y;

        /* these windows are down a border width from the top of the frame */
        if (win == self->title ||
            win == self->titleleft || win == self->titleright)
            fy += self->bwidth;

        /* title is a border width in from the edge */
        if (win == self->title)
            fx += self->bwidth;
        /* titletop is a bit to the right */
        else if (win == self->titletop)
            fx += ob_rr_theme->grip_width + self->bwidth;
        /* titletopright is way to the right edge */
        else if (win == self->titletopright)
            fx += self->area.width - (ob_rr_theme->grip_width + self->bwidth);
        /* titleright is even more way to the right edge */
        else if (win == self->titleright)
            fx += self->area.width - self->bwidth;

        /* figure out if we're over the area that should be considered a
           button */
        if (fy < self->bwidth + ob_rr_theme->paddingy + 1 +
            ob_rr_theme->button_size)
        {
            if (fx < (self->bwidth + ob_rr_theme->paddingx + 1 +
                      ob_rr_theme->button_size))
            {
                if (self->leftmost != OB_FRAME_CONTEXT_NONE)
                    return self->leftmost;
            }
            else if (fx >= (self->area.width -
                            (self->bwidth + ob_rr_theme->paddingx + 1 +
                             ob_rr_theme->button_size)))
            {
                if (self->rightmost != OB_FRAME_CONTEXT_NONE)
                    return self->rightmost;
            }
        }

        /* there is no resizing maximized windows so make them the titlebar
           context */
        return OB_FRAME_CONTEXT_TITLEBAR;
    }
    else if (self->max_vert &&
             (win == self->titletop || win == self->topresize))
        /* can't resize vertically when max vert */
        return OB_FRAME_CONTEXT_TITLEBAR;
    else if (self->shaded &&
             (win == self->titletop || win == self->topresize))
        /* can't resize vertically when shaded */
        return OB_FRAME_CONTEXT_TITLEBAR;

    if (win == self->window)            return OB_FRAME_CONTEXT_FRAME;
    if (win == self->label)             return OB_FRAME_CONTEXT_TITLEBAR;
    if (win == self->handle)            return OB_FRAME_CONTEXT_BOTTOM;
    if (win == self->handletop)         return OB_FRAME_CONTEXT_BOTTOM;
    if (win == self->handlebottom)      return OB_FRAME_CONTEXT_BOTTOM;
    if (win == self->handleleft)        return OB_FRAME_CONTEXT_BLCORNER;
    if (win == self->lgrip)             return OB_FRAME_CONTEXT_BLCORNER;
    if (win == self->lgripleft)         return OB_FRAME_CONTEXT_BLCORNER;
    if (win == self->lgriptop)          return OB_FRAME_CONTEXT_BLCORNER;
    if (win == self->lgripbottom)       return OB_FRAME_CONTEXT_BLCORNER;
    if (win == self->handleright)       return OB_FRAME_CONTEXT_BRCORNER;
    if (win == self->rgrip)             return OB_FRAME_CONTEXT_BRCORNER;
    if (win == self->rgripright)        return OB_FRAME_CONTEXT_BRCORNER;
    if (win == self->rgriptop)          return OB_FRAME_CONTEXT_BRCORNER;
    if (win == self->rgripbottom)       return OB_FRAME_CONTEXT_BRCORNER;
    if (win == self->title)             return OB_FRAME_CONTEXT_TITLEBAR;
    if (win == self->titlebottom)       return OB_FRAME_CONTEXT_TITLEBAR;
    if (win == self->titleleft)         return OB_FRAME_CONTEXT_TLCORNER;
    if (win == self->titletopleft)      return OB_FRAME_CONTEXT_TLCORNER;
    if (win == self->titleright)        return OB_FRAME_CONTEXT_TRCORNER;
    if (win == self->titletopright)     return OB_FRAME_CONTEXT_TRCORNER;
    if (win == self->titletop)          return OB_FRAME_CONTEXT_TOP;
    if (win == self->topresize)         return OB_FRAME_CONTEXT_TOP;
    if (win == self->tltresize)         return OB_FRAME_CONTEXT_TLCORNER;
    if (win == self->tllresize)         return OB_FRAME_CONTEXT_TLCORNER;
    if (win == self->trtresize)         return OB_FRAME_CONTEXT_TRCORNER;
    if (win == self->trrresize)         return OB_FRAME_CONTEXT_TRCORNER;
    if (win == self->left)              return OB_FRAME_CONTEXT_LEFT;
    if (win == self->right)             return OB_FRAME_CONTEXT_RIGHT;
    if (win == self->innertop)          return OB_FRAME_CONTEXT_TITLEBAR;
    if (win == self->innerleft)         return OB_FRAME_CONTEXT_LEFT;
    if (win == self->innerbottom)       return OB_FRAME_CONTEXT_BOTTOM;
    if (win == self->innerright)        return OB_FRAME_CONTEXT_RIGHT;
    if (win == self->innerbll)          return OB_FRAME_CONTEXT_BLCORNER;
    if (win == self->innerblb)          return OB_FRAME_CONTEXT_BLCORNER;
    if (win == self->innerbrr)          return OB_FRAME_CONTEXT_BRCORNER;
    if (win == self->innerbrb)          return OB_FRAME_CONTEXT_BRCORNER;
    if (win == self->max)               return OB_FRAME_CONTEXT_MAXIMIZE;
    if (win == self->iconify)           return OB_FRAME_CONTEXT_ICONIFY;
    if (win == self->close)             return OB_FRAME_CONTEXT_CLOSE;
    if (win == self->icon)              return OB_FRAME_CONTEXT_ICON;
    if (win == self->desk)              return OB_FRAME_CONTEXT_ALLDESKTOPS;
    if (win == self->shade)             return OB_FRAME_CONTEXT_SHADE;

    return OB_FRAME_CONTEXT_NONE;
}

void frame_client_gravity(ObFrame *self, gint *x, gint *y)
{
    /* horizontal */
    switch (self->client->gravity) {
    default:
    case NorthWestGravity:
    case SouthWestGravity:
    case WestGravity:
        break;

    case NorthGravity:
    case SouthGravity:
    case CenterGravity:
        /* the middle of the client will be the middle of the frame */
        *x -= (self->size.right - self->size.left) / 2;
        break;

    case NorthEastGravity:
    case SouthEastGravity:
    case EastGravity:
        /* the right side of the client will be the right side of the frame */
        *x -= self->size.right + self->size.left -
            self->client->border_width * 2;
        break;

    case ForgetGravity:
    case StaticGravity:
        /* the client's position won't move */
        *x -= self->size.left - self->client->border_width;
        break;
    }

    /* vertical */
    switch (self->client->gravity) {
    default:
    case NorthWestGravity:
    case NorthEastGravity:
    case NorthGravity:
        break;

    case CenterGravity:
    case EastGravity:
    case WestGravity:
        /* the middle of the client will be the middle of the frame */
        *y -= (self->size.bottom - self->size.top) / 2;
        break;

    case SouthWestGravity:
    case SouthEastGravity:
    case SouthGravity:
        /* the bottom of the client will be the bottom of the frame */
        *y -= self->size.bottom + self->size.top -
            self->client->border_width * 2;
        break;

    case ForgetGravity:
    case StaticGravity:
        /* the client's position won't move */
        *y -= self->size.top - self->client->border_width;
        break;
    }
}

void frame_frame_gravity(ObFrame *self, gint *x, gint *y)
{
    /* horizontal */
    switch (self->client->gravity) {
    default:
    case NorthWestGravity:
    case WestGravity:
    case SouthWestGravity:
        break;
    case NorthGravity:
    case CenterGravity:
    case SouthGravity:
        /* the middle of the client will be the middle of the frame */
        *x += (self->size.right - self->size.left) / 2;
        break;
    case NorthEastGravity:
    case EastGravity:
    case SouthEastGravity:
        /* the right side of the client will be the right side of the frame */
        *x += self->size.right + self->size.left -
            self->client->border_width * 2;
        break;
    case StaticGravity:
    case ForgetGravity:
        /* the client's position won't move */
        *x += self->size.left - self->client->border_width;
        break;
    }

    /* vertical */
    switch (self->client->gravity) {
    default:
    case NorthWestGravity:
    case NorthGravity:
    case NorthEastGravity:
        break;
    case WestGravity:
    case CenterGravity:
    case EastGravity:
        /* the middle of the client will be the middle of the frame */
        *y += (self->size.bottom - self->size.top) / 2;
        break;
    case SouthWestGravity:
    case SouthGravity:
    case SouthEastGravity:
        /* the bottom of the client will be the bottom of the frame */
        *y += self->size.bottom + self->size.top -
            self->client->border_width * 2;
        break;
    case StaticGravity:
    case ForgetGravity:
        /* the client's position won't move */
        *y += self->size.top - self->client->border_width;
        break;
    }
}

void frame_rect_to_frame(ObFrame *self, Rect *r)
{
    r->width += self->size.left + self->size.right;
    r->height += self->size.top + self->size.bottom;
    frame_client_gravity(self, &r->x, &r->y);
}

void frame_rect_to_client(ObFrame *self, Rect *r)
{
    r->width -= self->size.left + self->size.right;
    r->height -= self->size.top + self->size.bottom;
    frame_frame_gravity(self, &r->x, &r->y);
}

static void flash_done(gpointer data)
{
    ObFrame *self = data;

    self->flash_timer = 0;
}

static gboolean flash_timeout(gpointer data)
{
    ObFrame *self = data;
    gint64 now = g_get_monotonic_time();

    if (now > self->flash_end)
        self->flashing = FALSE;

    if (!self->flashing) {
        if (self->focused != self->flash_on)
            frame_adjust_focus(self, self->focused);

        return FALSE; /* we are done */
    }

    self->flash_on = !self->flash_on;
    if (!self->focused) {
        frame_adjust_focus(self, self->flash_on);
        self->focused = FALSE;
    }

    return TRUE; /* go again */
}

void frame_flash_start(ObFrame *self)
{
    self->flash_on = self->focused;

    if (!self->flashing)
        self->flash_timer = g_timeout_add_full(G_PRIORITY_DEFAULT,
                                               600, flash_timeout, self,
                                               flash_done);
    self->flash_end = g_get_monotonic_time() + G_USEC_PER_SEC * 5;

    self->flashing = TRUE;
}

void frame_flash_stop(ObFrame *self)
{
    self->flashing = FALSE;
}

/* X clips the frame's children to the shrinking frame window, the scene
   graph doesn't clip, so the animation is drawn as a separate titlebar
   painted at the animated size while the frame itself is hidden.  w == 0
   ends the animation. */
static void frame_animate_iconify_draw(ObFrame *self, gint x, gint y,
                                       gint w, gint h)
{
    struct wlr_scene_buffer **anim = &self->iconify_animation_buffer;
    RrAppearance *t;
    cairo_surface_t *s;
    struct wlr_buffer *buf;

    if (*anim) {
        wlr_scene_node_destroy(&(*anim)->node);
        *anim = NULL;
    }
    if (w <= 0 || h <= 0) {
        wlr_scene_node_set_enabled(&self->window->tree->node, self->visible);
        return;
    }

    wlr_scene_node_set_enabled(&self->window->tree->node, FALSE);
    t = self->focused ? ob_rr_theme->a_focused_title :
        ob_rr_theme->a_unfocused_title;
    if (!(s = RrPaintSurface(t, w, MIN(h, ob_rr_theme->title_height))))
        return;
    buf = ob_buffer_from_cairo(s);
    *anim = wlr_scene_buffer_create(
        server.stack_tree[OB_STACKING_LAYER_INTERNAL], buf);
    wlr_buffer_drop(buf);
    wlr_scene_node_set_position(&(*anim)->node, x, y);
}

static gulong frame_animate_iconify_time_left(ObFrame *self,
                                              gint64 now)
{
    gint64 time = self->iconify_animation_end - now;
    /* no negative values */
    return MAX(time, 0);
}

static gboolean frame_animate_iconify(gpointer p)
{
    ObFrame *self = p;
    gint x, y, w, h;
    gint iconx, icony, iconw;
    gint64 now;
    gulong time;
    gboolean iconifying;

    if (self->client->icon_geometry.width == 0) {
        /* there is no icon geometry set so just go straight down */
        const Rect *a;

        a = screen_physical_area_monitor(screen_find_monitor(&self->area));
        iconx = self->area.x + self->area.width / 2 + 32;
        icony = a->y + a->width;
        iconw = 64;
    } else {
        iconx = self->client->icon_geometry.x;
        icony = self->client->icon_geometry.y;
        iconw = self->client->icon_geometry.width;
    }

    iconifying = self->iconify_animation_going > 0;

    /* how far do we have left to go ? */
    now = g_get_monotonic_time();
    time = frame_animate_iconify_time_left(self, now);

    if ((time > 0 && iconifying) || (time == 0 && !iconifying)) {
        /* start where the frame is supposed to be */
        x = self->area.x;
        y = self->area.y;
        w = self->area.width;
        h = self->area.height;
    } else {
        /* start at the icon */
        x = iconx;
        y = icony;
        w = iconw;
        h = self->size.top; /* just the titlebar */
    }

    if (time > 0) {
        glong dx, dy, dw;
        glong elapsed;

        dx = self->area.x - iconx;
        dy = self->area.y - icony;
        dw = self->area.width - self->bwidth * 2 - iconw;
         /* if restoring, we move in the opposite direction */
        if (!iconifying) { dx = -dx; dy = -dy; dw = -dw; }

        elapsed = FRAME_ANIMATE_ICONIFY_TIME - time;
        x = x - (dx * elapsed) / FRAME_ANIMATE_ICONIFY_TIME;
        y = y - (dy * elapsed) / FRAME_ANIMATE_ICONIFY_TIME;
        w = w - (dw * elapsed) / FRAME_ANIMATE_ICONIFY_TIME;
        h = self->size.top; /* just the titlebar */
    }

    frame_animate_iconify_draw(self, x, y, w, h);

    return time > 0; /* repeat until we're out of time */
}

void frame_end_iconify_animation(gpointer data)
{
    ObFrame *self = data;
    /* see if there is an animation going */
    if (self->iconify_animation_going == 0) return;

    frame_animate_iconify_draw(self, 0, 0, 0, 0);
    if (!self->visible)
        fwin_unmap(self->window);
    else {
        /* Send a ConfigureNotify when the animation is done, this fixes
           KDE's pager showing the window in the wrong place.  since the
           window is mapped at a different location and is then moved, we
           need to send the synthetic configurenotify, since apps may have
           read the position when the client mapped, apparently. */
        client_reconfigure(self->client, TRUE);
    }

    /* we're not animating any more ! */
    self->iconify_animation_going = 0;
    self->iconify_animation_timer = 0;

    fwin_move_resize(self->window,
                      self->area.x, self->area.y,
                      self->area.width, self->area.height);
    /* we delay re-rendering until after we're done animating */
    framerender_frame(self);
}

void frame_begin_iconify_animation(ObFrame *self, gboolean iconifying)
{
    gulong time;
    gboolean new_anim = FALSE;
    gboolean set_end = TRUE;
    gint64 now;

    /* if there is no titlebar, just don't animate for now
       XXX it would be nice tho.. */
    if (!(self->decorations & OB_FRAME_DECOR_TITLEBAR))
        return;

    /* get the current time */
    now = g_get_monotonic_time();

    /* get how long until the end */
    time = FRAME_ANIMATE_ICONIFY_TIME;
    if (self->iconify_animation_going) {
        if (!!iconifying != (self->iconify_animation_going > 0)) {
            /* animation was already going on in the opposite direction */
            time = time - frame_animate_iconify_time_left(self, now);
        } else
            /* animation was already going in the same direction */
            set_end = FALSE;
    } else
        new_anim = TRUE;
    self->iconify_animation_going = iconifying ? 1 : -1;

    /* set the ending time */
    if (set_end)
        self->iconify_animation_end = now + time;

    if (new_anim) {
        if (self->iconify_animation_timer)
            g_source_remove(self->iconify_animation_timer);
        self->iconify_animation_timer =
            g_timeout_add_full(G_PRIORITY_DEFAULT,
                               FRAME_ANIMATE_ICONIFY_STEP_TIME,
                               frame_animate_iconify, self,
                               frame_end_iconify_animation);
                               

        /* do the first step */
        frame_animate_iconify(self);

        /* show it during the animation even if it is not "visible" */
        if (!self->visible)
            fwin_map(self->window);
    }
}
