/*
 * libfyvterm-state.h - the state layer: cursor, pen and terminal modes
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


#ifndef LIBFYVTERM_STATE_H
#define LIBFYVTERM_STATE_H

#include <libfyvterm/libfyvterm-util.h>

#ifdef __cplusplus
extern "C" {
#endif

struct fyvt_state_callbacks {
  int (*putglyph)(struct fyvt_glyph_info *info, struct fyvt_pos pos, void *user);
  int (*movecursor)(struct fyvt_pos pos, struct fyvt_pos oldpos, int visible, void *user);
  int (*scrollrect)(struct fyvt_rect rect, int downward, int rightward, void *user);
  int (*moverect)(struct fyvt_rect dest, struct fyvt_rect src, void *user);
  int (*erase)(struct fyvt_rect rect, int selective, void *user);
  int (*initpen)(void *user);
  int (*setpenattr)(enum fyvt_attr attr, union fyvt_value *val, void *user);
  int (*settermprop)(enum fyvt_prop prop, union fyvt_value *val, void *user);
  int (*bell)(void *user);
  int (*resize)(int rows, int cols, struct fyvt_state_fields *fields, void *user);
  int (*theme)(bool *dark, void *user);
  int (*setlineinfo)(int row, const struct fyvt_line_info *newinfo, const struct fyvt_line_info *oldinfo, void *user);
  int (*sb_clear)(void *user);
  // ABI-compat only enabled if fyvt_state_callbacks_has_premove() is invoked
  int (*premove)(struct fyvt_rect dest, void *user);
};

struct fyvt_state_fallbacks {
  int (*control)(unsigned char control, void *user);
  int (*csi)(const char *leader, const long args[], int argcount, const char *intermed, char command, void *user);
  int (*osc)(int command, struct fyvt_string_fragment frag, void *user);
  int (*dcs)(const char *command, size_t commandlen, struct fyvt_string_fragment frag, void *user);
  int (*apc)(struct fyvt_string_fragment frag, void *user);
  int (*pm)(struct fyvt_string_fragment frag, void *user);
  int (*sos)(struct fyvt_string_fragment frag, void *user);
};

struct fyvt_selection_callbacks {
  int (*set)(enum fyvt_selection_mask mask, struct fyvt_string_fragment frag, void *user);
  int (*query)(enum fyvt_selection_mask mask, void *user);
};

struct fyvt_state *fyvt_obtain_state(struct fyvt *vt) FYVT_EXPORT;

void  fyvt_state_set_callbacks(struct fyvt_state *state, const struct fyvt_state_callbacks *callbacks, void *user) FYVT_EXPORT;
void *fyvt_state_get_cbdata(struct fyvt_state *state) FYVT_EXPORT;

void fyvt_state_callbacks_has_premove(struct fyvt_state *state) FYVT_EXPORT;

void  fyvt_state_set_unrecognised_fallbacks(struct fyvt_state *state, const struct fyvt_state_fallbacks *fallbacks, void *user) FYVT_EXPORT;
void *fyvt_state_get_unrecognised_fbdata(struct fyvt_state *state) FYVT_EXPORT;

void fyvt_state_reset(struct fyvt_state *state, int hard) FYVT_EXPORT;
void fyvt_state_get_cursorpos(const struct fyvt_state *state, struct fyvt_pos *cursorpos) FYVT_EXPORT;
void fyvt_state_get_default_colors(const struct fyvt_state *state, union fyvt_color *default_fg, union fyvt_color *default_bg) FYVT_EXPORT;
void fyvt_state_get_palette_color(const struct fyvt_state *state, int index, union fyvt_color *col) FYVT_EXPORT;
void fyvt_state_set_default_colors(struct fyvt_state *state, const union fyvt_color *default_fg, const union fyvt_color *default_bg) FYVT_EXPORT;
void fyvt_state_set_palette_color(struct fyvt_state *state, int index, const union fyvt_color *col) FYVT_EXPORT;
void fyvt_state_set_bold_highbright(struct fyvt_state *state, int bold_is_highbright) FYVT_EXPORT;
int  fyvt_state_get_penattr(const struct fyvt_state *state, enum fyvt_attr attr, union fyvt_value *val) FYVT_EXPORT;
int  fyvt_state_set_termprop(struct fyvt_state *state, enum fyvt_prop prop, union fyvt_value *val) FYVT_EXPORT;
void fyvt_state_focus_in(struct fyvt_state *state) FYVT_EXPORT;
void fyvt_state_focus_out(struct fyvt_state *state) FYVT_EXPORT;
const struct fyvt_line_info *fyvt_state_get_lineinfo(const struct fyvt_state *state, int row) FYVT_EXPORT;

/**
 * Makes sure that the given color `col` is indeed an RGB colour. After this
 * function returns, FYVT_COLOR_IS_RGB(col) will return true, while all other
 * flags stored in `col->type` will have been reset.
 *
 * @param state is the struct fyvt_state instance from which the colour palette should
 * be extracted.
 * @param col is a pointer at the union fyvt_color instance that should be converted
 * to an RGB colour.
 */
void fyvt_state_convert_color_to_rgb(const struct fyvt_state *state, union fyvt_color *col) FYVT_EXPORT;

void fyvt_state_set_selection_callbacks(struct fyvt_state *state, const struct fyvt_selection_callbacks *callbacks, void *user,
    char *buffer, size_t buflen) FYVT_EXPORT;

void fyvt_state_send_selection(struct fyvt_state *state, enum fyvt_selection_mask mask, struct fyvt_string_fragment frag) FYVT_EXPORT;

#ifdef __cplusplus
}
#endif

#endif /* LIBFYVTERM_STATE_H */
