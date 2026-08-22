/*
 * libfyvterm-screen.h - the screen layer: the cell buffer and damage
 *
 * Copyright (c) 2026 Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */


#ifndef LIBFYVTERM_SCREEN_H
#define LIBFYVTERM_SCREEN_H

#include <libfyvterm/libfyvterm-util.h>
#include <libfyvterm/libfyvterm-state.h>

#ifdef __cplusplus
extern "C" {
#endif

struct fyvt_screen_cell_attrs {
    unsigned int bold      : 1;
    unsigned int underline : 2;
    unsigned int italic    : 1;
    unsigned int blink     : 1;
    unsigned int reverse   : 1;
    unsigned int conceal   : 1;
    unsigned int strike    : 1;
    unsigned int font      : 4; /* 0 to 9 */
    unsigned int dwl       : 1; /* On a DECDWL or DECDHL line */
    unsigned int dhl       : 2; /* On a DECDHL line (1=top 2=bottom) */
    unsigned int small     : 1;
    unsigned int baseline  : 2;
    unsigned int dim       : 1;
    unsigned int overline  : 1;
};

enum {
  FYVT_UNDERLINE_OFF,
  FYVT_UNDERLINE_SINGLE,
  FYVT_UNDERLINE_DOUBLE,
  FYVT_UNDERLINE_CURLY,
};

enum {
  FYVT_BASELINE_NORMAL,
  FYVT_BASELINE_RAISE,
  FYVT_BASELINE_LOWER,
};

struct fyvt_screen_cell {
  uint32_t chars[FYVT_MAX_CHARS_PER_CELL];
  char     width;
  struct fyvt_screen_cell_attrs attrs;
  union fyvt_color fg, bg;
};

struct fyvt_screen_callbacks {
  int (*damage)(struct fyvt_rect rect, void *user);
  int (*moverect)(struct fyvt_rect dest, struct fyvt_rect src, void *user);
  int (*movecursor)(struct fyvt_pos pos, struct fyvt_pos oldpos, int visible, void *user);
  int (*settermprop)(enum fyvt_prop prop, union fyvt_value *val, void *user);
  int (*bell)(void *user);
  int (*resize)(int rows, int cols, void *user);
  int (*theme)(bool *dark, void *user);
  int (*sb_pushline)(int cols, const struct fyvt_screen_cell *cells, void *user);
  int (*sb_popline)(int cols, struct fyvt_screen_cell *cells, void *user);
  int (*sb_clear)(void* user);
  /* ABI-compat this is only used if fyvt_screen_callbacks_has_pushline4() is called */
  int (*sb_pushline4)(int cols, const struct fyvt_screen_cell *cells, bool continuation, void *user);
};

struct fyvt_screen *fyvt_obtain_screen(struct fyvt *vt) FYVT_EXPORT;

void  fyvt_screen_set_callbacks(struct fyvt_screen *screen, const struct fyvt_screen_callbacks *callbacks, void *user) FYVT_EXPORT;
void *fyvt_screen_get_cbdata(struct fyvt_screen *screen) FYVT_EXPORT;

void fyvt_screen_callbacks_has_pushline4(struct fyvt_screen *screen) FYVT_EXPORT;

void  fyvt_screen_set_unrecognised_fallbacks(struct fyvt_screen *screen, const struct fyvt_state_fallbacks *fallbacks, void *user) FYVT_EXPORT;
void *fyvt_screen_get_unrecognised_fbdata(struct fyvt_screen *screen) FYVT_EXPORT;

void fyvt_screen_enable_reflow(struct fyvt_screen *screen, bool reflow) FYVT_EXPORT;


void fyvt_screen_enable_altscreen(struct fyvt_screen *screen, int altscreen) FYVT_EXPORT;

enum fyvt_damage_size {
  FYVT_DAMAGE_CELL,    /* every cell */
  FYVT_DAMAGE_ROW,     /* entire rows */
  FYVT_DAMAGE_SCREEN,  /* entire screen */
  FYVT_DAMAGE_SCROLL,  /* entire screen + scrollrect */

  FYVT_N_DAMAGES
};

void fyvt_screen_flush_damage(struct fyvt_screen *screen) FYVT_EXPORT;
void fyvt_screen_set_damage_merge(struct fyvt_screen *screen, enum fyvt_damage_size size) FYVT_EXPORT;

void   fyvt_screen_reset(struct fyvt_screen *screen, int hard) FYVT_EXPORT;

/* Neither of these functions NUL-terminate the buffer */
size_t fyvt_screen_get_chars(const struct fyvt_screen *screen, uint32_t *chars, size_t len, const struct fyvt_rect rect) FYVT_EXPORT;
size_t fyvt_screen_get_text(const struct fyvt_screen *screen, char *str, size_t len, const struct fyvt_rect rect) FYVT_EXPORT;

enum fyvt_attr_mask {
  FYVT_ATTR_BOLD_MASK       = 1 << 0,
  FYVT_ATTR_UNDERLINE_MASK  = 1 << 1,
  FYVT_ATTR_ITALIC_MASK     = 1 << 2,
  FYVT_ATTR_BLINK_MASK      = 1 << 3,
  FYVT_ATTR_REVERSE_MASK    = 1 << 4,
  FYVT_ATTR_STRIKE_MASK     = 1 << 5,
  FYVT_ATTR_FONT_MASK       = 1 << 6,
  FYVT_ATTR_FOREGROUND_MASK = 1 << 7,
  FYVT_ATTR_BACKGROUND_MASK = 1 << 8,
  FYVT_ATTR_CONCEAL_MASK    = 1 << 9,
  FYVT_ATTR_SMALL_MASK      = 1 << 10,
  FYVT_ATTR_BASELINE_MASK   = 1 << 11,
  FYVT_ATTR_DIM_MASK        = 1 << 12,
  FYVT_ATTR_OVERLINE_MASK   = 1 << 13,

  FYVT_ALL_ATTRS_MASK = (1 << 14) - 1
};

int fyvt_screen_get_attrs_extent(const struct fyvt_screen *screen, struct fyvt_rect *extent, struct fyvt_pos pos, enum fyvt_attr_mask attrs) FYVT_EXPORT;

int fyvt_screen_get_cell(const struct fyvt_screen *screen, struct fyvt_pos pos, struct fyvt_screen_cell *cell) FYVT_EXPORT;

int fyvt_screen_is_eol(const struct fyvt_screen *screen, struct fyvt_pos pos) FYVT_EXPORT;

/**
 * Same as fyvt_state_convert_color_to_rgb(), but takes a `screen` instead of a `state`
 * instance.
 */
void fyvt_screen_convert_color_to_rgb(const struct fyvt_screen *screen, union fyvt_color *col) FYVT_EXPORT;

/**
 * Similar to fyvt_state_set_default_colors(), but also resets colours in the
 * screen buffer(s)
 */
void fyvt_screen_set_default_colors(struct fyvt_screen *screen, const union fyvt_color *default_fg, const union fyvt_color *default_bg) FYVT_EXPORT;

enum fyvt_value_type fyvt_get_attr_type(enum fyvt_attr attr) FYVT_EXPORT;
enum fyvt_value_type fyvt_get_prop_type(enum fyvt_prop prop) FYVT_EXPORT;

void fyvt_scroll_rect(struct fyvt_rect rect,
                       int downward,
                       int rightward,
                       int (*moverect)(struct fyvt_rect src, struct fyvt_rect dest, void *user),
                       int (*eraserect)(struct fyvt_rect rect, int selective, void *user),
                       void *user) FYVT_EXPORT;

void fyvt_copy_cells(struct fyvt_rect dest,
                      struct fyvt_rect src,
                      void (*copycell)(struct fyvt_pos dest, struct fyvt_pos src, void *user),
                      void *user) FYVT_EXPORT;

#ifdef __cplusplus
}
#endif

#endif /* LIBFYVTERM_SCREEN_H */
