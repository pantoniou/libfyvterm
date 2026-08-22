/*
 * libfyvterm-keys.h - keyboard and mouse input
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


#ifndef LIBFYVTERM_KEYS_H
#define LIBFYVTERM_KEYS_H

#include <libfyvterm/libfyvterm-util.h>

#ifdef __cplusplus
extern "C" {
#endif

enum fyvt_modifier {
  FYVT_MOD_NONE  = 0x00,
  FYVT_MOD_SHIFT = 0x01,
  FYVT_MOD_ALT   = 0x02,
  FYVT_MOD_CTRL  = 0x04,

  FYVT_ALL_MODS_MASK = 0x07 
};

enum fyvt_key {
  FYVT_KEY_NONE,

  FYVT_KEY_ENTER,
  FYVT_KEY_TAB,
  FYVT_KEY_BACKSPACE,
  FYVT_KEY_ESCAPE,

  FYVT_KEY_UP,
  FYVT_KEY_DOWN,
  FYVT_KEY_LEFT,
  FYVT_KEY_RIGHT,

  FYVT_KEY_INS,
  FYVT_KEY_DEL,
  FYVT_KEY_HOME,
  FYVT_KEY_END,
  FYVT_KEY_PAGEUP,
  FYVT_KEY_PAGEDOWN,

  FYVT_KEY_FUNCTION_0   = 256,
  FYVT_KEY_FUNCTION_MAX = FYVT_KEY_FUNCTION_0 + 255,

  FYVT_KEY_KP_0,
  FYVT_KEY_KP_1,
  FYVT_KEY_KP_2,
  FYVT_KEY_KP_3,
  FYVT_KEY_KP_4,
  FYVT_KEY_KP_5,
  FYVT_KEY_KP_6,
  FYVT_KEY_KP_7,
  FYVT_KEY_KP_8,
  FYVT_KEY_KP_9,
  FYVT_KEY_KP_MULT,
  FYVT_KEY_KP_PLUS,
  FYVT_KEY_KP_COMMA,
  FYVT_KEY_KP_MINUS,
  FYVT_KEY_KP_PERIOD,
  FYVT_KEY_KP_DIVIDE,
  FYVT_KEY_KP_ENTER,
  FYVT_KEY_KP_EQUAL,

  FYVT_KEY_MAX, // Must be last
  FYVT_N_KEYS = FYVT_KEY_MAX
};

#define FYVT_KEY_FUNCTION(n) (FYVT_KEY_FUNCTION_0+(n))

void fyvt_keyboard_unichar(struct fyvt *vt, uint32_t c, enum fyvt_modifier mod) FYVT_EXPORT;
void fyvt_keyboard_key(struct fyvt *vt, enum fyvt_key key, enum fyvt_modifier mod) FYVT_EXPORT;

void fyvt_keyboard_start_paste(struct fyvt *vt) FYVT_EXPORT;
void fyvt_keyboard_end_paste(struct fyvt *vt) FYVT_EXPORT;

void fyvt_mouse_move(struct fyvt *vt, int row, int col, enum fyvt_modifier mod) FYVT_EXPORT;
void fyvt_mouse_button(struct fyvt *vt, int button, bool pressed, enum fyvt_modifier mod) FYVT_EXPORT;

#ifdef __cplusplus
}
#endif

#endif /* LIBFYVTERM_KEYS_H */
