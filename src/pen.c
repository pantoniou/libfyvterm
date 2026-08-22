#include "fyvt_internal.h"

#include <stdio.h>

/**
 * Structure used to store RGB triples without the additional metadata stored in
 * union fyvt_color.
 */
struct fyvt_rgb {
  uint8_t red, green, blue;
};

static const struct fyvt_rgb ansi_colors[] = {
  /* R    G    B */
  {   0,   0,   0 }, // black
  { 224,   0,   0 }, // red
  {   0, 224,   0 }, // green
  { 224, 224,   0 }, // yellow
  {   0,   0, 224 }, // blue
  { 224,   0, 224 }, // magenta
  {   0, 224, 224 }, // cyan
  { 224, 224, 224 }, // white == light grey

  // high intensity
  { 128, 128, 128 }, // black
  { 255,  64,  64 }, // red
  {  64, 255,  64 }, // green
  { 255, 255,  64 }, // yellow
  {  64,  64, 255 }, // blue
  { 255,  64, 255 }, // magenta
  {  64, 255, 255 }, // cyan
  { 255, 255, 255 }, // white for real
};

static int ramp6[] = {
  0x00, 0x33, 0x66, 0x99, 0xCC, 0xFF,
};

static int ramp24[] = {
  0x00, 0x0B, 0x16, 0x21, 0x2C, 0x37, 0x42, 0x4D, 0x58, 0x63, 0x6E, 0x79,
  0x85, 0x90, 0x9B, 0xA6, 0xB1, 0xBC, 0xC7, 0xD2, 0xDD, 0xE8, 0xF3, 0xFF,
};

static void lookup_default_colour_ansi(long idx, union fyvt_color *col)
{
  if (idx >= 0 && idx < 16) {
    fyvt_color_rgb(
        col,
        ansi_colors[idx].red, ansi_colors[idx].green, ansi_colors[idx].blue);
  }
}

static bool lookup_colour_ansi(const struct fyvt_state *state, long index, union fyvt_color *col)
{
  if(index >= 0 && index < 16) {
    *col = state->colors[index];
    return true;
  }

  return false;
}

static bool lookup_colour_palette(const struct fyvt_state *state, long index, union fyvt_color *col)
{
  if(index >= 0 && index < 16) {
    // Normal 8 colours or high intensity - parse as palette 0
    return lookup_colour_ansi(state, index, col);
  }
  else if(index >= 16 && index < 232) {
    // 216-colour cube
    index -= 16;

    fyvt_color_rgb(col, ramp6[index/6/6 % 6],
                         ramp6[index/6   % 6],
                         ramp6[index     % 6]);

    return true;
  }
  else if(index >= 232 && index < 256) {
    // 24 greyscales
    index -= 232;

    fyvt_color_rgb(col, ramp24[index], ramp24[index], ramp24[index]);

    return true;
  }

  return false;
}

static int lookup_colour(const struct fyvt_state *state UNUSED, int palette, const long args[], int argcount, union fyvt_color *col)
{
  switch(palette) {
  case 2: // RGB mode - 3 args contain colour values directly
    if(argcount < 3)
      return argcount;

    fyvt_color_rgb(col, CSI_ARG(args[0]), CSI_ARG(args[1]), CSI_ARG(args[2]));

    return 3;

  case 5: // XTerm 256-colour mode
    if (!argcount || CSI_ARG_IS_MISSING(args[0])) {
      return argcount ? 1 : 0;
    }

    fyvt_color_indexed(col, args[0]);

    return argcount ? 1 : 0;

  default:
    DEBUG_LOG("Unrecognised colour palette %d\n", palette);
    return 0;
  }
}

// Some conveniences

static void setpenattr(struct fyvt_state *state, enum fyvt_attr attr, enum fyvt_value_type type UNUSED, union fyvt_value *val)
{
#ifdef DEBUG
  if(type != fyvt_get_attr_type(attr)) {
    DEBUG_LOG("Cannot set attr %d as it has type %d, not type %d\n",
        attr, fyvt_get_attr_type(attr), type);
    return;
  }
#endif
  if(state->callbacks && state->callbacks->setpenattr)
    (*state->callbacks->setpenattr)(attr, val, state->cbdata);
}

static void setpenattr_bool(struct fyvt_state *state, enum fyvt_attr attr, int boolean)
{
  union fyvt_value val = { .boolean = boolean };
  setpenattr(state, attr, FYVT_VALUETYPE_BOOL, &val);
}

static void setpenattr_int(struct fyvt_state *state, enum fyvt_attr attr, int number)
{
  union fyvt_value val = { .number = number };
  setpenattr(state, attr, FYVT_VALUETYPE_INT, &val);
}

static void setpenattr_col(struct fyvt_state *state, enum fyvt_attr attr, union fyvt_color color)
{
  union fyvt_value val = { .color = color };
  setpenattr(state, attr, FYVT_VALUETYPE_COLOR, &val);
}

static void set_pen_col_ansi(struct fyvt_state *state, enum fyvt_attr attr, long col)
{
  union fyvt_color *colp = (attr == FYVT_ATTR_BACKGROUND) ? &state->pen.bg : &state->pen.fg;

  fyvt_color_indexed(colp, col);

  setpenattr_col(state, attr, *colp);
}

void fyvt_state_newpen(struct fyvt_state *state)
{
  // 90% grey so that pure white is brighter
  fyvt_color_rgb(&state->default_fg, 240, 240, 240);
  fyvt_color_rgb(&state->default_bg, 0, 0, 0);
  fyvt_state_set_default_colors(state, &state->default_fg, &state->default_bg);

  for(int col = 0; col < 16; col++)
    lookup_default_colour_ansi(col, &state->colors[col]);
}

void fyvt_state_resetpen(struct fyvt_state *state)
{
  state->pen.bold = 0;      setpenattr_bool(state, FYVT_ATTR_BOLD, 0);
  state->pen.underline = 0; setpenattr_int (state, FYVT_ATTR_UNDERLINE, 0);
  state->pen.italic = 0;    setpenattr_bool(state, FYVT_ATTR_ITALIC, 0);
  state->pen.blink = 0;     setpenattr_bool(state, FYVT_ATTR_BLINK, 0);
  state->pen.reverse = 0;   setpenattr_bool(state, FYVT_ATTR_REVERSE, 0);
  state->pen.conceal = 0;   setpenattr_bool(state, FYVT_ATTR_CONCEAL, 0);
  state->pen.strike = 0;    setpenattr_bool(state, FYVT_ATTR_STRIKE, 0);
  state->pen.font = 0;      setpenattr_int (state, FYVT_ATTR_FONT, 0);
  state->pen.small = 0;     setpenattr_bool(state, FYVT_ATTR_SMALL, 0);
  state->pen.baseline = 0;  setpenattr_int (state, FYVT_ATTR_BASELINE, 0);
  state->pen.dim = 0;       setpenattr_bool(state, FYVT_ATTR_DIM, 0);
  state->pen.overline = 0;  setpenattr_bool(state, FYVT_ATTR_OVERLINE, 0);

  state->pen.fg = state->default_fg;  setpenattr_col(state, FYVT_ATTR_FOREGROUND, state->default_fg);
  state->pen.bg = state->default_bg;  setpenattr_col(state, FYVT_ATTR_BACKGROUND, state->default_bg);
}

void fyvt_state_savepen(struct fyvt_state *state, int save)
{
  if(save) {
    state->saved.pen = state->pen;
  }
  else {
    state->pen = state->saved.pen;

    setpenattr_bool(state, FYVT_ATTR_BOLD,      state->pen.bold);
    setpenattr_int (state, FYVT_ATTR_UNDERLINE, state->pen.underline);
    setpenattr_bool(state, FYVT_ATTR_ITALIC,    state->pen.italic);
    setpenattr_bool(state, FYVT_ATTR_BLINK,     state->pen.blink);
    setpenattr_bool(state, FYVT_ATTR_REVERSE,   state->pen.reverse);
    setpenattr_bool(state, FYVT_ATTR_CONCEAL,   state->pen.conceal);
    setpenattr_bool(state, FYVT_ATTR_STRIKE,    state->pen.strike);
    setpenattr_int (state, FYVT_ATTR_FONT,      state->pen.font);
    setpenattr_bool(state, FYVT_ATTR_SMALL,     state->pen.small);
    setpenattr_int (state, FYVT_ATTR_BASELINE,  state->pen.baseline);
    setpenattr_bool(state, FYVT_ATTR_DIM,       state->pen.dim);
    setpenattr_bool(state, FYVT_ATTR_OVERLINE,  state->pen.overline);

    setpenattr_col( state, FYVT_ATTR_FOREGROUND, state->pen.fg);
    setpenattr_col( state, FYVT_ATTR_BACKGROUND, state->pen.bg);
  }
}

int fyvt_color_is_equal(const union fyvt_color *a, const union fyvt_color *b)
{
  /* First make sure that the two colours are of the same type (RGB/Indexed) */
  if (a->type != b->type) {
    return false;
  }

  /* Depending on the type inspect the corresponding members */
  if (FYVT_COLOR_IS_INDEXED(a)) {
    return a->indexed.idx == b->indexed.idx;
  }
  else if (FYVT_COLOR_IS_RGB(a)) {
    return    (a->rgb.red   == b->rgb.red)
           && (a->rgb.green == b->rgb.green)
           && (a->rgb.blue  == b->rgb.blue);
  }

  return 0;
}

void fyvt_state_get_default_colors(const struct fyvt_state *state, union fyvt_color *default_fg, union fyvt_color *default_bg)
{
  *default_fg = state->default_fg;
  *default_bg = state->default_bg;
}

void fyvt_state_get_palette_color(const struct fyvt_state *state, int index, union fyvt_color *col)
{
  lookup_colour_palette(state, index, col);
}

void fyvt_state_set_default_colors(struct fyvt_state *state, const union fyvt_color *default_fg, const union fyvt_color *default_bg)
{
  if(default_fg) {
    state->default_fg = *default_fg;
    state->default_fg.type = (state->default_fg.type & ~FYVT_COLOR_DEFAULT_MASK)
                           | FYVT_COLOR_DEFAULT_FG;
  }

  if(default_bg) {
    state->default_bg = *default_bg;
    state->default_bg.type = (state->default_bg.type & ~FYVT_COLOR_DEFAULT_MASK)
                           | FYVT_COLOR_DEFAULT_BG;
  }
}

void fyvt_state_set_palette_color(struct fyvt_state *state, int index, const union fyvt_color *col)
{
  if(index >= 0 && index < 16)
    state->colors[index] = *col;
}

void fyvt_state_convert_color_to_rgb(const struct fyvt_state *state, union fyvt_color *col)
{
  if (FYVT_COLOR_IS_INDEXED(col)) { /* Convert indexed colors to RGB */
    lookup_colour_palette(state, col->indexed.idx, col);
  }
  col->type &= FYVT_COLOR_TYPE_MASK; /* Reset any metadata but the type */
}

void fyvt_state_set_bold_highbright(struct fyvt_state *state, int bold_is_highbright)
{
  state->bold_is_highbright = bold_is_highbright;
}

void fyvt_state_setpen(struct fyvt_state *state, const long args[], int argcount)
{
  // SGR - ECMA-48 8.3.117

  int argi = 0;
  int value;

  while(argi < argcount) {
    // This logic is easier to do 'done' backwards; set it true, and make it
    // false again in the 'default' case
    int done = 1;

    long arg;
    switch(arg = CSI_ARG(args[argi])) {
    case CSI_ARG_MISSING:
    case 0: // Reset
      fyvt_state_resetpen(state);
      break;

    case 1: { // Bold on
      const union fyvt_color *fg = &state->pen.fg;
      state->pen.bold = 1;
      setpenattr_bool(state, FYVT_ATTR_BOLD, 1);
      if(!FYVT_COLOR_IS_DEFAULT_FG(fg) && FYVT_COLOR_IS_INDEXED(fg) && fg->indexed.idx < 8 && state->bold_is_highbright)
        set_pen_col_ansi(state, FYVT_ATTR_FOREGROUND, fg->indexed.idx + (state->pen.bold ? 8 : 0));
      break;
    }

    case 2: // Dim/faint on
      state->pen.dim = 1;
      setpenattr_bool(state, FYVT_ATTR_DIM, 1);
      break;

    case 3: // Italic on
      state->pen.italic = 1;
      setpenattr_bool(state, FYVT_ATTR_ITALIC, 1);
      break;

    case 4: // Underline
      state->pen.underline = FYVT_UNDERLINE_SINGLE;
      if(CSI_ARG_HAS_MORE(args[argi])) {
        argi++;
        switch(CSI_ARG(args[argi])) {
          case 0:
            state->pen.underline = 0;
            break;
          case 1:
            state->pen.underline = FYVT_UNDERLINE_SINGLE;
            break;
          case 2:
            state->pen.underline = FYVT_UNDERLINE_DOUBLE;
            break;
          case 3:
            state->pen.underline = FYVT_UNDERLINE_CURLY;
            break;
        }
      }
      setpenattr_int(state, FYVT_ATTR_UNDERLINE, state->pen.underline);
      break;

    case 5: // Blink
      state->pen.blink = 1;
      setpenattr_bool(state, FYVT_ATTR_BLINK, 1);
      break;

    case 7: // Reverse on
      state->pen.reverse = 1;
      setpenattr_bool(state, FYVT_ATTR_REVERSE, 1);
      break;

    case 8: // Conceal on
      state->pen.conceal = 1;
      setpenattr_bool(state, FYVT_ATTR_CONCEAL, 1);
      break;

    case 9: // Strikethrough on
      state->pen.strike = 1;
      setpenattr_bool(state, FYVT_ATTR_STRIKE, 1);
      break;

    case 10: case 11: case 12: case 13: case 14:
    case 15: case 16: case 17: case 18: case 19: // Select font
      state->pen.font = CSI_ARG(args[argi]) - 10;
      setpenattr_int(state, FYVT_ATTR_FONT, state->pen.font);
      break;

    case 21: // Underline double
      state->pen.underline = FYVT_UNDERLINE_DOUBLE;
      setpenattr_int(state, FYVT_ATTR_UNDERLINE, state->pen.underline);
      break;

    case 22: // Normal intensity (bold and dim off)
      state->pen.bold = 0;
      setpenattr_bool(state, FYVT_ATTR_BOLD, 0);
      state->pen.dim = 0;
      setpenattr_bool(state, FYVT_ATTR_DIM, 0);
      break;

    case 23: // Italic and Gothic (currently unsupported) off
      state->pen.italic = 0;
      setpenattr_bool(state, FYVT_ATTR_ITALIC, 0);
      break;

    case 24: // Underline off
      state->pen.underline = 0;
      setpenattr_int(state, FYVT_ATTR_UNDERLINE, 0);
      break;

    case 25: // Blink off
      state->pen.blink = 0;
      setpenattr_bool(state, FYVT_ATTR_BLINK, 0);
      break;

    case 27: // Reverse off
      state->pen.reverse = 0;
      setpenattr_bool(state, FYVT_ATTR_REVERSE, 0);
      break;

    case 28: // Conceal off (Reveal)
      state->pen.conceal = 0;
      setpenattr_bool(state, FYVT_ATTR_CONCEAL, 0);
      break;

    case 29: // Strikethrough off
      state->pen.strike = 0;
      setpenattr_bool(state, FYVT_ATTR_STRIKE, 0);
      break;

    case 30: case 31: case 32: case 33:
    case 34: case 35: case 36: case 37: // Foreground colour palette
      value = CSI_ARG(args[argi]) - 30;
      if(state->pen.bold && state->bold_is_highbright)
        value += 8;
      set_pen_col_ansi(state, FYVT_ATTR_FOREGROUND, value);
      break;

    case 38: // Foreground colour alternative palette
      if(argcount - argi < 1)
        return;
      argi += 1 + lookup_colour(state, CSI_ARG(args[argi+1]), args+argi+2, argcount-argi-2, &state->pen.fg);
      setpenattr_col(state, FYVT_ATTR_FOREGROUND, state->pen.fg);
      break;

    case 39: // Foreground colour default
      state->pen.fg = state->default_fg;
      setpenattr_col(state, FYVT_ATTR_FOREGROUND, state->pen.fg);
      break;

    case 40: case 41: case 42: case 43:
    case 44: case 45: case 46: case 47: // Background colour palette
      value = CSI_ARG(args[argi]) - 40;
      set_pen_col_ansi(state, FYVT_ATTR_BACKGROUND, value);
      break;

    case 48: // Background colour alternative palette
      if(argcount - argi < 1)
        return;
      argi += 1 + lookup_colour(state, CSI_ARG(args[argi+1]), args+argi+2, argcount-argi-2, &state->pen.bg);
      setpenattr_col(state, FYVT_ATTR_BACKGROUND, state->pen.bg);
      break;

    case 49: // Default background
      state->pen.bg = state->default_bg;
      setpenattr_col(state, FYVT_ATTR_BACKGROUND, state->pen.bg);
      break;

    case 53: // Overline on
      state->pen.overline = 1;
      setpenattr_bool(state, FYVT_ATTR_OVERLINE, 1);
      break;

    case 55: // Overline off
      state->pen.overline = 0;
      setpenattr_bool(state, FYVT_ATTR_OVERLINE, 0);
      break;

    case 73: // Superscript
    case 74: // Subscript
    case 75: // Superscript/subscript off
      state->pen.small = (arg != 75);
      state->pen.baseline =
        (arg == 73) ? FYVT_BASELINE_RAISE :
        (arg == 74) ? FYVT_BASELINE_LOWER :
                      FYVT_BASELINE_NORMAL;
      setpenattr_bool(state, FYVT_ATTR_SMALL,    state->pen.small);
      setpenattr_int (state, FYVT_ATTR_BASELINE, state->pen.baseline);
      break;

    case 90: case 91: case 92: case 93:
    case 94: case 95: case 96: case 97: // Foreground colour high-intensity palette
      value = CSI_ARG(args[argi]) - 90 + 8;
      set_pen_col_ansi(state, FYVT_ATTR_FOREGROUND, value);
      break;

    case 100: case 101: case 102: case 103:
    case 104: case 105: case 106: case 107: // Background colour high-intensity palette
      value = CSI_ARG(args[argi]) - 100 + 8;
      set_pen_col_ansi(state, FYVT_ATTR_BACKGROUND, value);
      break;

    default:
      done = 0;
      break;
    }

    if(!done)
      DEBUG_LOG("libvterm: Unhandled CSI SGR %ld\n", arg);

    while(CSI_ARG_HAS_MORE(args[argi++]));
  }
}

static int fyvt_state_getpen_color(const union fyvt_color *col, int argi, long args[], int fg)
{
    /* Do nothing if the given color is the default color */
    if (( fg && FYVT_COLOR_IS_DEFAULT_FG(col)) ||
        (!fg && FYVT_COLOR_IS_DEFAULT_BG(col))) {
        return argi;
    }

    /* Decide whether to send an indexed color or an RGB color */
    if (FYVT_COLOR_IS_INDEXED(col)) {
        const uint8_t idx = col->indexed.idx;
        if (idx < 8) {
            args[argi++] = (idx + (fg ? 30 : 40));
        }
        else if (idx < 16) {
            args[argi++] = (idx - 8 + (fg ? 90 : 100));
        }
        else {
            args[argi++] = CSI_ARG_FLAG_MORE | (fg ? 38 : 48);
            args[argi++] = CSI_ARG_FLAG_MORE | 5;
            args[argi++] = idx;
        }
    }
    else if (FYVT_COLOR_IS_RGB(col)) {
        args[argi++] = CSI_ARG_FLAG_MORE | (fg ? 38 : 48);
        args[argi++] = CSI_ARG_FLAG_MORE | 2;
        args[argi++] = CSI_ARG_FLAG_MORE | col->rgb.red;
        args[argi++] = CSI_ARG_FLAG_MORE | col->rgb.green;
        args[argi++] = col->rgb.blue;
    }
    return argi;
}

int fyvt_state_getpen(struct fyvt_state *state, long args[], int argcount UNUSED)
{
  int argi = 0;

  if(state->pen.bold)
    args[argi++] = 1;

  if(state->pen.dim)
    args[argi++] = 2;

  if(state->pen.italic)
    args[argi++] = 3;

  if(state->pen.underline == FYVT_UNDERLINE_SINGLE)
    args[argi++] = 4;
  if(state->pen.underline == FYVT_UNDERLINE_CURLY)
    args[argi++] = 4 | CSI_ARG_FLAG_MORE, args[argi++] = 3;

  if(state->pen.blink)
    args[argi++] = 5;

  if(state->pen.reverse)
    args[argi++] = 7;

  if(state->pen.conceal)
    args[argi++] = 8;

  if(state->pen.strike)
    args[argi++] = 9;

  if(state->pen.font)
    args[argi++] = 10 + state->pen.font;

  if(state->pen.underline == FYVT_UNDERLINE_DOUBLE)
    args[argi++] = 21;

  argi = fyvt_state_getpen_color(&state->pen.fg, argi, args, true);

  argi = fyvt_state_getpen_color(&state->pen.bg, argi, args, false);

  if(state->pen.overline)
    args[argi++] = 53;

  if(state->pen.small) {
    if(state->pen.baseline == FYVT_BASELINE_RAISE)
      args[argi++] = 73;
    else if(state->pen.baseline == FYVT_BASELINE_LOWER)
      args[argi++] = 74;
  }

  return argi;
}

int fyvt_state_get_penattr(const struct fyvt_state *state, enum fyvt_attr attr, union fyvt_value *val)
{
  switch(attr) {
  case FYVT_ATTR_BOLD:
    val->boolean = state->pen.bold;
    return 1;

  case FYVT_ATTR_UNDERLINE:
    val->number = state->pen.underline;
    return 1;

  case FYVT_ATTR_ITALIC:
    val->boolean = state->pen.italic;
    return 1;

  case FYVT_ATTR_BLINK:
    val->boolean = state->pen.blink;
    return 1;

  case FYVT_ATTR_REVERSE:
    val->boolean = state->pen.reverse;
    return 1;

  case FYVT_ATTR_CONCEAL:
    val->boolean = state->pen.conceal;
    return 1;

  case FYVT_ATTR_STRIKE:
    val->boolean = state->pen.strike;
    return 1;

  case FYVT_ATTR_FONT:
    val->number = state->pen.font;
    return 1;

  case FYVT_ATTR_FOREGROUND:
    val->color = state->pen.fg;
    return 1;

  case FYVT_ATTR_BACKGROUND:
    val->color = state->pen.bg;
    return 1;

  case FYVT_ATTR_SMALL:
    val->boolean = state->pen.small;
    return 1;

  case FYVT_ATTR_BASELINE:
    val->number = state->pen.baseline;
    return 1;

  case FYVT_ATTR_DIM:
    val->boolean = state->pen.dim;
    return 1;

  case FYVT_ATTR_OVERLINE:
    val->boolean = state->pen.overline;
    return 1;

  case FYVT_N_ATTRS:
    return 0;
  }

  return 0;
}
