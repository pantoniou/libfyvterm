/*
 * libfyvterm-util.h - shared types, constants and the export macro
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


#ifndef LIBFYVTERM_UTIL_H
#define LIBFYVTERM_UTIL_H

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * FYVT_EXPORT - mark a symbol as part of the shared-library public ABI.
 *
 * On GCC/Clang overrides -fvisibility=hidden for the annotated symbol; on
 * Windows selects the dllexport/dllimport side; elsewhere expands to nothing.
 */
#ifndef FYVT_EXPORT
  #if defined(_WIN32)
    #if defined(FYVT_BUILDING_DLL)
      #define FYVT_EXPORT __declspec(dllexport)
    #elif defined(FYVT_USING_DLL)
      #define FYVT_EXPORT __declspec(dllimport)
    #else
      #define FYVT_EXPORT
    #endif
  #elif defined(__GNUC__) || defined(__clang__)
    #define FYVT_EXPORT __attribute__((visibility("default")))
  #else
    #define FYVT_EXPORT
  #endif
#endif

/* Kept in step with the CMake project version; the build asserts it. */
#define FYVT_VERSION_MAJOR 0
#define FYVT_VERSION_MINOR 0
#define FYVT_VERSION_PATCH 1

#define FYVT_CHECK_VERSION \
        fyvt_check_version(FYVT_VERSION_MAJOR, FYVT_VERSION_MINOR)

/*
 * fyvt_version_string() - the library version this binary was built as
 *
 * Returns a static string; the caller does not own it.
 */
const char *fyvt_version_string(void) FYVT_EXPORT;

/* Any cell can contain at most one basic printing character and 5 combining
 * characters. This number could be changed but will be ABI-incompatible if
 * you do */
#define FYVT_MAX_CHARS_PER_CELL 6


struct fyvt;
struct fyvt_state;
struct fyvt_screen;

struct fyvt_pos {
  int row;
  int col;
};

/* some small utility functions; we can just keep these static here */

/* order points by on-screen flow order */
static inline int fyvt_pos_cmp(struct fyvt_pos a, struct fyvt_pos b)
{
  return (a.row == b.row) ? a.col - b.col : a.row - b.row;
}

struct fyvt_rect {
  int start_row;
  int end_row;
  int start_col;
  int end_col;
};

/* true if the rect contains the point */
static inline int fyvt_rect_contains(struct fyvt_rect r, struct fyvt_pos p)
{
  return p.row >= r.start_row && p.row < r.end_row &&
         p.col >= r.start_col && p.col < r.end_col;
}

/* move a rect */
static inline void fyvt_rect_move(struct fyvt_rect *rect, int row_delta, int col_delta)
{
  rect->start_row += row_delta; rect->end_row += row_delta;
  rect->start_col += col_delta; rect->end_col += col_delta;
}

/**
 * Bit-field describing the content of the tagged union `union fyvt_color`.
 */
enum fyvt_color_type {
  /**
   * If the lower bit of `type` is not set, the colour is 24-bit RGB.
   */
  FYVT_COLOR_RGB = 0x00,

  /**
   * The colour is an index into a palette of 256 colours.
   */
  FYVT_COLOR_INDEXED = 0x01,

  /**
   * Mask that can be used to extract the RGB/Indexed bit.
   */
  FYVT_COLOR_TYPE_MASK = 0x01,

  /**
   * If set, indicates that this colour should be the default foreground
   * color, i.e. there was no SGR request for another colour. When
   * rendering this colour it is possible to ignore "idx" and just use a
   * colour that is not in the palette.
   */
  FYVT_COLOR_DEFAULT_FG = 0x02,

  /**
   * If set, indicates that this colour should be the default background
   * color, i.e. there was no SGR request for another colour. A common
   * option when rendering this colour is to not render a background at
   * all, for example by rendering the window transparently at this spot.
   */
  FYVT_COLOR_DEFAULT_BG = 0x04,

  /**
   * Mask that can be used to extract the default foreground/background bit.
   */
  FYVT_COLOR_DEFAULT_MASK = 0x06
};

/**
 * Returns true if the FYVT_COLOR_RGB `type` flag is set, indicating that the
 * given union fyvt_color instance is an indexed colour.
 */
#define FYVT_COLOR_IS_INDEXED(col) \
  (((col)->type & FYVT_COLOR_TYPE_MASK) == FYVT_COLOR_INDEXED)

/**
 * Returns true if the FYVT_COLOR_INDEXED `type` flag is set, indicating that
 * the given union fyvt_color instance is an rgb colour.
 */
#define FYVT_COLOR_IS_RGB(col) \
  (((col)->type & FYVT_COLOR_TYPE_MASK) == FYVT_COLOR_RGB)

/**
 * Returns true if the FYVT_COLOR_DEFAULT_FG `type` flag is set, indicating
 * that the given union fyvt_color instance corresponds to the default foreground
 * color.
 */
#define FYVT_COLOR_IS_DEFAULT_FG(col) \
  (!!((col)->type & FYVT_COLOR_DEFAULT_FG))

/**
 * Returns true if the FYVT_COLOR_DEFAULT_BG `type` flag is set, indicating
 * that the given union fyvt_color instance corresponds to the default background
 * color.
 */
#define FYVT_COLOR_IS_DEFAULT_BG(col) \
  (!!((col)->type & FYVT_COLOR_DEFAULT_BG))

/**
 * Tagged union storing either an RGB color or an index into a colour palette.
 * In order to convert indexed colours to RGB, you may use the
 * fyvt_state_convert_color_to_rgb() or fyvt_screen_convert_color_to_rgb()
 * functions which lookup the RGB colour from the palette maintained by a
 * struct fyvt_state or struct fyvt_screen instance.
 */
union fyvt_color {
  /**
   * Tag indicating which union member is actually valid. This variable
   * coincides with the `type` member of the `rgb` and the `indexed` struct
   * in memory. Please use the `FYVT_COLOR_IS_*` test macros to check whether
   * a particular type flag is set.
   */
  uint8_t type;

  /**
   * Valid if `FYVT_COLOR_IS_RGB(type)` is true. Holds the RGB colour values.
   */
  struct {
    /**
     * Same as the top-level `type` member stored in union fyvt_color.
     */
    uint8_t type;

    /**
     * The actual 8-bit red, green, blue colour values.
     */
    uint8_t red, green, blue;
  } rgb;

  /**
   * If `FYVT_COLOR_IS_INDEXED(type)` is true, this member holds the index into
   * the colour palette.
   */
  struct {
    /**
     * Same as the top-level `type` member stored in union fyvt_color.
     */
    uint8_t type;

    /**
     * Index into the colour map.
     */
    uint8_t idx;
  } indexed;
};

/**
 * Constructs a new union fyvt_color instance representing the given RGB values.
 */
static inline void fyvt_color_rgb(union fyvt_color *col, uint8_t red, uint8_t green,
                                   uint8_t blue)
{
  col->type = FYVT_COLOR_RGB;
  col->rgb.red   = red;
  col->rgb.green = green;
  col->rgb.blue  = blue;
}

/**
 * Construct a new union fyvt_color instance representing an indexed color with the
 * given index.
 */
static inline void fyvt_color_indexed(union fyvt_color *col, uint8_t idx)
{
  col->type = FYVT_COLOR_INDEXED;
  col->indexed.idx = idx;
}

/**
 * Compares two colours. Returns true if the colors are equal, false otherwise.
 */
int fyvt_color_is_equal(const union fyvt_color *a, const union fyvt_color *b) FYVT_EXPORT;

enum fyvt_value_type {
  /* FYVT_VALUETYPE_NONE = 0 */
  FYVT_VALUETYPE_BOOL = 1,
  FYVT_VALUETYPE_INT,
  FYVT_VALUETYPE_STRING,
  FYVT_VALUETYPE_COLOR,

  FYVT_N_VALUETYPES
};

enum fyvt_terminator {
  FYVT_TERMINATOR_BEL, /* \x07 */
  FYVT_TERMINATOR_ST,  /* \x1b\x5c */
};

struct fyvt_string_fragment {
  const char *str;
  size_t      len : 30;
  bool        initial : 1;
  bool        final : 1;
  enum fyvt_terminator terminator;
};

union fyvt_value {
  int boolean;
  int number;
  struct fyvt_string_fragment string;
  union fyvt_color color;
};

enum fyvt_attr {
  /* FYVT_ATTR_NONE = 0 */
  FYVT_ATTR_BOLD = 1,   // bool:   1, 22
  FYVT_ATTR_UNDERLINE,  // number: 4, 21, 24
  FYVT_ATTR_ITALIC,     // bool:   3, 23
  FYVT_ATTR_BLINK,      // bool:   5, 25
  FYVT_ATTR_REVERSE,    // bool:   7, 27
  FYVT_ATTR_CONCEAL,    // bool:   8, 28
  FYVT_ATTR_STRIKE,     // bool:   9, 29
  FYVT_ATTR_FONT,       // number: 10-19
  FYVT_ATTR_FOREGROUND, // color:  30-39 90-97
  FYVT_ATTR_BACKGROUND, // color:  40-49 100-107
  FYVT_ATTR_SMALL,      // bool:   73, 74, 75
  FYVT_ATTR_BASELINE,   // number: 73, 74, 75
  FYVT_ATTR_DIM,        // bool:   2, 22
  FYVT_ATTR_OVERLINE,   // bool:   53, 55

  FYVT_N_ATTRS
};

enum fyvt_prop {
  /* FYVT_PROP_NONE = 0 */
  FYVT_PROP_CURSORVISIBLE = 1, // bool
  FYVT_PROP_CURSORBLINK,       // bool
  FYVT_PROP_ALTSCREEN,         // bool
  FYVT_PROP_TITLE,             // string
  FYVT_PROP_ICONNAME,          // string
  FYVT_PROP_REVERSE,           // bool
  FYVT_PROP_CURSORSHAPE,       // number
  FYVT_PROP_MOUSE,             // number
  FYVT_PROP_FOCUSREPORT,       // bool
  FYVT_PROP_THEMEUPDATES,      // bool
  FYVT_PROP_SYNCOUTPUT,        // bool

  FYVT_N_PROPS
};

enum {
  FYVT_PROP_CURSORSHAPE_BLOCK = 1,
  FYVT_PROP_CURSORSHAPE_UNDERLINE,
  FYVT_PROP_CURSORSHAPE_BAR_LEFT,

  FYVT_N_PROP_CURSORSHAPES
};

enum {
  FYVT_PROP_MOUSE_NONE = 0,
  FYVT_PROP_MOUSE_CLICK,
  FYVT_PROP_MOUSE_DRAG,
  FYVT_PROP_MOUSE_MOVE,

  FYVT_N_PROP_MOUSES
};

enum fyvt_selection_mask {
  FYVT_SELECTION_CLIPBOARD = (1<<0),
  FYVT_SELECTION_PRIMARY   = (1<<1),
  FYVT_SELECTION_SECONDARY = (1<<2),
  FYVT_SELECTION_SELECT    = (1<<3),
  FYVT_SELECTION_CUT0      = (1<<4), /* also CUT1 .. CUT7 by bitshifting */
};

struct fyvt_glyph_info {
  const uint32_t *chars;
  int             width;
  unsigned int    protected_cell:1;  /* DECSCA-protected against DECSEL/DECSED */
  unsigned int    dwl:1;             /* DECDWL or DECDHL double-width line */
  unsigned int    dhl:2;             /* DECDHL double-height line (1=top 2=bottom) */
};

struct fyvt_line_info {
  unsigned int    doublewidth:1;     /* DECDWL or DECDHL line */
  unsigned int    doubleheight:2;    /* DECDHL line (1=top 2=bottom) */
  unsigned int    continuation:1;    /* Line is a flow continuation of the previous */
};

/* Copies of struct fyvt_state fields that the 'resize' callback might have reason to
 * edit. 'resize' callback gets total control of these fields and may
 * free-and-reallocate them if required. They will be copied back from the
 * struct after the callback has returned.
 */
struct fyvt_state_fields {
  struct fyvt_pos pos;                /* current cursor position */
  struct fyvt_line_info *lineinfos[2]; /* [1] may be NULL */
};

struct fyvt_allocator {
  /* libvterm relies on this memory to be zeroed out before it is returned
   * by the allocator. */
  void *(*malloc)(size_t size, void *allocdata);
  void  (*free)(void *ptr, void *allocdata);
};


#ifdef __cplusplus
}
#endif

#endif /* LIBFYVTERM_UTIL_H */
