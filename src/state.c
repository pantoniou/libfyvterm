#include "fyvt_internal.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define strneq(a,b,n) (strncmp(a,b,n)==0)

#if defined(DEBUG) && DEBUG > 1
# define DEBUG_GLYPH_COMBINE
#endif

/* Some convenient wrappers to make callback functions easier */

static void putglyph(struct fyvt_state *state, const uint32_t chars[], int width, struct fyvt_pos pos)
{
  struct fyvt_glyph_info info = {
    .chars = chars,
    .width = width,
    .protected_cell = state->protected_cell,
    .dwl = state->lineinfo[pos.row].doublewidth,
    .dhl = state->lineinfo[pos.row].doubleheight,
  };

  if(state->callbacks && state->callbacks->putglyph)
    if((*state->callbacks->putglyph)(&info, pos, state->cbdata))
      return;

  DEBUG_LOG("libvterm: Unhandled putglyph U+%04x at (%d,%d)\n", chars[0], pos.col, pos.row);
}

static void updatecursor(struct fyvt_state *state, struct fyvt_pos *oldpos, int cancel_phantom)
{
  if(state->pos.col == oldpos->col && state->pos.row == oldpos->row)
    return;

  if(cancel_phantom)
    state->at_phantom = 0;

  if(state->callbacks && state->callbacks->movecursor)
    if((*state->callbacks->movecursor)(state->pos, *oldpos, state->mode.cursor_visible, state->cbdata))
      return;
}

static void erase(struct fyvt_state *state, struct fyvt_rect rect, int selective)
{
  if(rect.end_col == state->cols) {
    /* If we're erasing the final cells of any lines, cancel the continuation
     * marker on the subsequent line
     */
    for(int row = rect.start_row + 1; row < rect.end_row + 1 && row < state->rows; row++)
      state->lineinfo[row].continuation = 0;
  }

  if(state->callbacks && state->callbacks->erase)
    if((*state->callbacks->erase)(rect, selective, state->cbdata))
      return;
}

static struct fyvt_state *fyvt_state_new(struct fyvt *vt)
{
  struct fyvt_state *state = fyvt_allocator_malloc(vt, sizeof(struct fyvt_state));

  state->vt = vt;

  state->rows = vt->rows;
  state->cols = vt->cols;

  state->mouse_col     = 0;
  state->mouse_row     = 0;
  state->mouse_buttons = 0;

  state->mouse_protocol = MOUSE_X10;

  state->callbacks = NULL;
  state->cbdata    = NULL;
  state->callbacks_has_premove = false;

  state->selection.callbacks = NULL;
  state->selection.user      = NULL;
  state->selection.buffer    = NULL;

  fyvt_state_newpen(state);

  state->bold_is_highbright = 0;

  state->combine_chars_size = 16;
  state->combine_chars = fyvt_allocator_malloc(state->vt, state->combine_chars_size * sizeof(state->combine_chars[0]));

  state->tabstops = fyvt_allocator_malloc(state->vt, (state->cols + 7) / 8);

  state->lineinfos[BUFIDX_PRIMARY]   = fyvt_allocator_malloc(state->vt, state->rows * sizeof(struct fyvt_line_info));
  /* TODO: Make an 'enable' function */
  state->lineinfos[BUFIDX_ALTSCREEN] = fyvt_allocator_malloc(state->vt, state->rows * sizeof(struct fyvt_line_info));
  state->lineinfo = state->lineinfos[BUFIDX_PRIMARY];

  state->encoding_utf8.enc = fyvt_lookup_encoding(ENC_UTF8, 'u');
  if(*state->encoding_utf8.enc->init)
    (*state->encoding_utf8.enc->init)(state->encoding_utf8.enc, state->encoding_utf8.data);

  /* Kitty keyboard protocol: each screen starts with one all-zero flags
   * entry on its stack (see fyvt_state_get_key_encoding_flags()). The
   * allocator zeroes new memory, so only .size needs setting. */
  for(size_t i = 0; i < sizeof(state->key_encoding_stacks)/sizeof(state->key_encoding_stacks[0]); i++)
    state->key_encoding_stacks[i].size = 1;

  return state;
}

void fyvt_state_free(struct fyvt_state *state)
{
  if(state->selection.buffer_owned)
    fyvt_allocator_free(state->vt, state->selection.buffer);
  fyvt_allocator_free(state->vt, state->tabstops);
  fyvt_allocator_free(state->vt, state->lineinfos[BUFIDX_PRIMARY]);
  if(state->lineinfos[BUFIDX_ALTSCREEN])
    fyvt_allocator_free(state->vt, state->lineinfos[BUFIDX_ALTSCREEN]);
  fyvt_allocator_free(state->vt, state->combine_chars);
  fyvt_allocator_free(state->vt, state);
}

static void scroll(struct fyvt_state *state, struct fyvt_rect rect, int downward, int rightward)
{
  if(!downward && !rightward)
    return;

  int rows = rect.end_row - rect.start_row;
  if(downward > rows)
    downward = rows;
  else if(downward < -rows)
    downward = -rows;

  int cols = rect.end_col - rect.start_col;
  if(rightward > cols)
    rightward = cols;
  else if(rightward < -cols)
    rightward = -cols;

  if(state->callbacks_has_premove && state->callbacks && state->callbacks->premove) {
    // TODO: technically this logic is wrong if both downward != 0 and rightward != 0

    /* Work out what subsection of the destination area is about to be destroyed */
    if(downward > 0)
      /* about to destroy the top */
      (*state->callbacks->premove)((struct fyvt_rect){
          .start_row = rect.start_row, .end_row = rect.start_row + downward,
          .start_col = rect.start_col, .end_col = rect.end_col}, state->cbdata);
    else if(downward < 0)
      /* about to destroy the bottom */
      (*state->callbacks->premove)((struct fyvt_rect){
          .start_row = rect.end_row + downward, .end_row = rect.end_row,
          .start_col = rect.start_col,          .end_col = rect.end_col}, state->cbdata);

    if(rightward > 0)
      /* about to destroy the left */
      (*state->callbacks->premove)((struct fyvt_rect){
          .start_row = rect.start_row, .end_row = rect.end_row,
          .start_col = rect.start_col, .end_col = rect.start_col + rightward}, state->cbdata);
    else if(rightward < 0)
      /* about to destroy the right */
      (*state->callbacks->premove)((struct fyvt_rect){
          .start_row = rect.start_row,           .end_row = rect.end_row,
          .start_col = rect.end_col + rightward, .end_col = rect.end_col}, state->cbdata);
  }

  // Update lineinfo if full line
  if(rect.start_col == 0 && rect.end_col == state->cols && rightward == 0) {
    int height = rect.end_row - rect.start_row - abs(downward);

    if(downward > 0) {
      memmove(state->lineinfo + rect.start_row,
              state->lineinfo + rect.start_row + downward,
              height * sizeof(state->lineinfo[0]));
      for(int row = rect.end_row - downward; row < rect.end_row; row++)
        state->lineinfo[row] = (struct fyvt_line_info){ 0 };
    }
    else {
      memmove(state->lineinfo + rect.start_row - downward,
              state->lineinfo + rect.start_row,
              height * sizeof(state->lineinfo[0]));
      for(int row = rect.start_row; row < rect.start_row - downward; row++)
        state->lineinfo[row] = (struct fyvt_line_info){ 0 };
    }
  }

  if(state->callbacks && state->callbacks->scrollrect)
    if((*state->callbacks->scrollrect)(rect, downward, rightward, state->cbdata))
      return;

  if(state->callbacks)
    fyvt_scroll_rect(rect, downward, rightward,
        state->callbacks->moverect, state->callbacks->erase, state->cbdata);
}

static void linefeed(struct fyvt_state *state)
{
  if(state->pos.row == SCROLLREGION_BOTTOM(state) - 1) {
    struct fyvt_rect rect = {
      .start_row = state->scrollregion_top,
      .end_row   = SCROLLREGION_BOTTOM(state),
      .start_col = SCROLLREGION_LEFT(state),
      .end_col   = SCROLLREGION_RIGHT(state),
    };

    scroll(state, rect, 1, 0);
  }
  else if(state->pos.row < state->rows-1)
    state->pos.row++;
}

static void grow_combine_buffer(struct fyvt_state *state)
{
  size_t    new_size = state->combine_chars_size * 2;
  uint32_t *new_chars = fyvt_allocator_malloc(state->vt, new_size * sizeof(new_chars[0]));

  memcpy(new_chars, state->combine_chars, state->combine_chars_size * sizeof(new_chars[0]));

  fyvt_allocator_free(state->vt, state->combine_chars);

  state->combine_chars = new_chars;
  state->combine_chars_size = new_size;
}

static void set_col_tabstop(struct fyvt_state *state, int col)
{
  unsigned char mask = 1 << (col & 7);
  state->tabstops[col >> 3] |= mask;
}

static void clear_col_tabstop(struct fyvt_state *state, int col)
{
  unsigned char mask = 1 << (col & 7);
  state->tabstops[col >> 3] &= ~mask;
}

static int is_col_tabstop(struct fyvt_state *state, int col)
{
  unsigned char mask = 1 << (col & 7);
  return state->tabstops[col >> 3] & mask;
}

static int is_cursor_in_scrollregion(const struct fyvt_state *state)
{
  if(state->pos.row < state->scrollregion_top ||
     state->pos.row >= SCROLLREGION_BOTTOM(state))
    return 0;
  if(state->pos.col < SCROLLREGION_LEFT(state) ||
     state->pos.col >= SCROLLREGION_RIGHT(state))
    return 0;

  return 1;
}

static void tab(struct fyvt_state *state, int count, int direction)
{
  while(count > 0) {
    if(direction > 0) {
      if(state->pos.col >= THISROWWIDTH(state)-1)
        return;

      state->pos.col++;
    }
    else if(direction < 0) {
      if(state->pos.col < 1)
        return;

      state->pos.col--;
    }

    if(is_col_tabstop(state, state->pos.col))
      count--;
  }
}

#define NO_FORCE 0
#define FORCE    1

#define DWL_OFF 0
#define DWL_ON  1

#define DHL_OFF    0
#define DHL_TOP    1
#define DHL_BOTTOM 2

static void set_lineinfo(struct fyvt_state *state, int row, int force, int dwl, int dhl)
{
  struct fyvt_line_info info = state->lineinfo[row];

  if(dwl == DWL_OFF)
    info.doublewidth = DWL_OFF;
  else if(dwl == DWL_ON)
    info.doublewidth = DWL_ON;
  // else -1 to ignore

  if(dhl == DHL_OFF)
    info.doubleheight = DHL_OFF;
  else if(dhl == DHL_TOP)
    info.doubleheight = DHL_TOP;
  else if(dhl == DHL_BOTTOM)
    info.doubleheight = DHL_BOTTOM;

  if((state->callbacks &&
      state->callbacks->setlineinfo &&
      (*state->callbacks->setlineinfo)(row, &info, state->lineinfo + row, state->cbdata))
      || force)
    state->lineinfo[row] = info;
}

static int on_text(const char bytes[], size_t len, void *user)
{
  struct fyvt_state *state = user;

  struct fyvt_pos oldpos = state->pos;

  uint32_t *codepoints = (uint32_t *)(state->vt->tmpbuffer);
  size_t maxpoints = (state->vt->tmpbuffer_len) / sizeof(uint32_t);

  int npoints = 0;
  size_t eaten = 0;

  struct fyvt_encoding_instance *encoding =
    state->gsingle_set     ? &state->encoding[state->gsingle_set] :
    !(bytes[eaten] & 0x80) ? &state->encoding[state->gl_set] :
    state->vt->mode.utf8   ? &state->encoding_utf8 :
                             &state->encoding[state->gr_set];
  /* Neovim carries a fix here (upstream commit e40c5cb06d, "fix(vterm):
   * handle split UTF-8 after ASCII properly"): an ASCII byte picks the
   * gl_set encoding instance above even when it designates UTF-8, which
   * keeps a second, independent decode state for a split multi-byte
   * sequence that starts right after an ASCII byte. Route back to the one
   * persistent UTF-8 instance whenever the resolved encoding is UTF-8. */
  if(encoding->enc == state->encoding_utf8.enc)
    encoding = &state->encoding_utf8;

  (*encoding->enc->decode)(encoding->enc, encoding->data,
      codepoints, &npoints, state->gsingle_set ? 1 : maxpoints,
      bytes, &eaten, len);

  /* There's a chance an encoding (e.g. UTF-8) hasn't found enough bytes yet
   * for even a single codepoint
   */
  if(!npoints)
    return eaten;

  if(state->gsingle_set && npoints)
    state->gsingle_set = 0;

  int i = 0;

  /* This is a combining char. that needs to be merged with the previous
   * glyph output */
  if(fyvt_unicode_is_combining(codepoints[i])) {
    /* See if the cursor has moved since.
     *
     * Neovim carries a fix here (upstream commit 814f2629cb, "fix
     * (terminal): handle split composing chars at right edge"): at the
     * right edge of the screen, printing the previous glyph can leave the
     * cursor at combine_pos itself instead of combine_pos + combine_width,
     * because there was no room left to advance past it. Accept the whole
     * [combine_pos, combine_pos + combine_width] range, not just its end,
     * and drop any deferred wraparound the failed advance left behind. */
    if(state->pos.row == state->combine_pos.row &&
        state->pos.col >= state->combine_pos.col &&
        state->pos.col <= state->combine_pos.col + state->combine_width) {
      state->at_phantom = 0;
#ifdef DEBUG_GLYPH_COMBINE
      int printpos;
      printf("DEBUG: COMBINING SPLIT GLYPH of chars {");
      for(printpos = 0; state->combine_chars[printpos]; printpos++)
        printf("U+%04x ", state->combine_chars[printpos]);
      printf("} + {");
#endif

      /* Find where we need to append these combining chars */
      size_t saved_i = 0;
      while(state->combine_chars[saved_i])
        saved_i++;

      /* Add extra ones */
      while(i < npoints && fyvt_unicode_is_combining(codepoints[i])) {
        if(saved_i >= state->combine_chars_size)
          grow_combine_buffer(state);
        state->combine_chars[saved_i++] = codepoints[i++];
      }
      if(saved_i >= state->combine_chars_size)
        grow_combine_buffer(state);
      state->combine_chars[saved_i] = 0;

#ifdef DEBUG_GLYPH_COMBINE
      for(; state->combine_chars[printpos]; printpos++)
        printf("U+%04x ", state->combine_chars[printpos]);
      printf("}\n");
#endif

      /* A selector that arrives late can widen the glyph; at the right edge
       * there is no column for that, and it keeps its width. */
      int width = fyvt_unicode_glyph_width(state->combine_chars,
                                           state->combine_width);
      if(state->combine_pos.col + width > THISROWWIDTH(state))
        width = state->combine_width;

      /* Now render it */
      putglyph(state, state->combine_chars, width, state->combine_pos);

      /* The cursor goes past the wider glyph, as it would have gone had the
       * glyph arrived whole. */
      if(width != state->combine_width) {
        state->combine_width = width;
        if(state->combine_pos.col + width >= THISROWWIDTH(state)) {
          state->pos.col = state->combine_pos.col;
          if(state->mode.autowrap)
            state->at_phantom = 1;
        }
        else
          state->pos.col = state->combine_pos.col + width;
      }
    }
    else {
      DEBUG_LOG("libvterm: TODO: Skip over split char+combining\n");
    }
  }

  for(; i < npoints; i++) {
    // Try to find combining characters following this
    int glyph_starts = i;
    int glyph_ends;
    for(glyph_ends = i + 1;
        (glyph_ends < npoints) && (glyph_ends < glyph_starts + FYVT_MAX_CHARS_PER_CELL);
        glyph_ends++)
      if(!fyvt_unicode_is_combining(codepoints[glyph_ends]))
        break;

    /* A C0 control, DEL or a C1 control that arrived as text has no glyph and
     * no width. In UTF-8 mode the parser does not read 0x80..0x9F as C1
     * controls, so U+0080..U+009F reaches here from any program that emits
     * one. Drop the glyph rather than store a cell that cannot be drawn or
     * count a width that would move the cursor backwards. Only the base can
     * be negative: every codepoint after it is combining, and combining
     * starts well above the controls. */
    if(fyvt_unicode_width(codepoints[glyph_starts]) < 0) {
      /* The combining chars belong to the base, so they go with it. */
      for(i = glyph_ends;
          i < npoints && fyvt_unicode_is_combining(codepoints[i]); i++)
        ;
      i--;
      continue;
    }

    int width = 0;

    uint32_t chars[FYVT_MAX_CHARS_PER_CELL + 1];

    for( ; i < glyph_ends; i++) {
      chars[i - glyph_starts] = codepoints[i];
      width += fyvt_unicode_width(codepoints[i]);
    }

    while(i < npoints && fyvt_unicode_is_combining(codepoints[i]))
      i++;

    chars[glyph_ends - glyph_starts] = 0;
    i--;
    width = fyvt_unicode_glyph_width(chars, width);

#ifdef DEBUG_GLYPH_COMBINE
    int printpos;
    printf("DEBUG: COMBINED GLYPH of %d chars {", glyph_ends - glyph_starts);
    for(printpos = 0; printpos < glyph_ends - glyph_starts; printpos++)
      printf("U+%04x ", chars[printpos]);
    printf("}, onscreen width %d\n", width);
#endif

    if(state->at_phantom || state->pos.col + width > THISROWWIDTH(state)) {
      linefeed(state);
      state->pos.col = 0;
      state->at_phantom = 0;
      state->lineinfo[state->pos.row].continuation = 1;
    }

    if(state->mode.insert) {
      /* TODO: This will be a little inefficient for large bodies of text, as
       * it'll have to 'ICH' effectively before every glyph. We should scan
       * ahead and ICH as many times as required
       */
      struct fyvt_rect rect = {
        .start_row = state->pos.row,
        .end_row   = state->pos.row + 1,
        .start_col = state->pos.col,
        .end_col   = THISROWWIDTH(state),
      };
      scroll(state, rect, 0, -1);
    }

    putglyph(state, chars, width, state->pos);

    if(i == npoints - 1) {
      /* End of the buffer. Save the chars in case we have to combine with
       * more on the next call */
      size_t save_i;
      for(save_i = 0; chars[save_i]; save_i++) {
        if(save_i >= state->combine_chars_size)
          grow_combine_buffer(state);
        state->combine_chars[save_i] = chars[save_i];
      }
      if(save_i >= state->combine_chars_size)
        grow_combine_buffer(state);
      state->combine_chars[save_i] = 0;
      state->combine_width = width;
      state->combine_pos = state->pos;
    }

    if(state->pos.col + width >= THISROWWIDTH(state)) {
      if(state->mode.autowrap)
        state->at_phantom = 1;
    }
    else {
      state->pos.col += width;
    }
  }

  updatecursor(state, &oldpos, 0);

#ifdef DEBUG
  if(state->pos.row < 0 || state->pos.row >= state->rows ||
     state->pos.col < 0 || state->pos.col >= state->cols) {
    fprintf(stderr, "Position out of bounds after text: (%d,%d)\n",
        state->pos.row, state->pos.col);
    abort();
  }
#endif

  return eaten;
}

static int on_control(unsigned char control, void *user)
{
  struct fyvt_state *state = user;

  struct fyvt_pos oldpos = state->pos;

  switch(control) {
  case 0x07: // BEL - ECMA-48 8.3.3
    if(state->callbacks && state->callbacks->bell)
      (*state->callbacks->bell)(state->cbdata);
    break;

  case 0x08: // BS - ECMA-48 8.3.5
    if(state->pos.col > 0)
      state->pos.col--;
    break;

  case 0x09: // HT - ECMA-48 8.3.60
    tab(state, 1, +1);
    break;

  case 0x0a: // LF - ECMA-48 8.3.74
  case 0x0b: // VT
  case 0x0c: // FF
    linefeed(state);
    if(state->mode.newline)
      state->pos.col = 0;
    break;

  case 0x0d: // CR - ECMA-48 8.3.15
    state->pos.col = 0;
    break;

  case 0x0e: // LS1 - ECMA-48 8.3.76
    state->gl_set = 1;
    break;

  case 0x0f: // LS0 - ECMA-48 8.3.75
    state->gl_set = 0;
    break;

  case 0x84: // IND - DEPRECATED but implemented for completeness
    linefeed(state);
    break;

  case 0x85: // NEL - ECMA-48 8.3.86
    linefeed(state);
    state->pos.col = 0;
    break;

  case 0x88: // HTS - ECMA-48 8.3.62
    set_col_tabstop(state, state->pos.col);
    break;

  case 0x8d: // RI - ECMA-48 8.3.104
    if(state->pos.row == state->scrollregion_top) {
      struct fyvt_rect rect = {
        .start_row = state->scrollregion_top,
        .end_row   = SCROLLREGION_BOTTOM(state),
        .start_col = SCROLLREGION_LEFT(state),
        .end_col   = SCROLLREGION_RIGHT(state),
      };

      scroll(state, rect, -1, 0);
    }
    else if(state->pos.row > 0)
        state->pos.row--;
    break;

  case 0x8e: // SS2 - ECMA-48 8.3.141
    state->gsingle_set = 2;
    break;

  case 0x8f: // SS3 - ECMA-48 8.3.142
    state->gsingle_set = 3;
    break;

  default:
    if(state->fallbacks && state->fallbacks->control)
      if((*state->fallbacks->control)(control, state->fbdata))
        return 1;

    return 0;
  }

  updatecursor(state, &oldpos, 1);

#ifdef DEBUG
  if(state->pos.row < 0 || state->pos.row >= state->rows ||
     state->pos.col < 0 || state->pos.col >= state->cols) {
    fprintf(stderr, "Position out of bounds after Ctrl %02x: (%d,%d)\n",
        control, state->pos.row, state->pos.col);
    abort();
  }
#endif

  return 1;
}

static int settermprop_bool(struct fyvt_state *state, enum fyvt_prop prop, int v)
{
  union fyvt_value val = { .boolean = v };
  return fyvt_state_set_termprop(state, prop, &val);
}

static int settermprop_int(struct fyvt_state *state, enum fyvt_prop prop, int v)
{
  union fyvt_value val = { .number = v };
  return fyvt_state_set_termprop(state, prop, &val);
}

static int settermprop_string(struct fyvt_state *state, enum fyvt_prop prop, struct fyvt_string_fragment frag)
{
  union fyvt_value val = { .string = frag };
  return fyvt_state_set_termprop(state, prop, &val);
}

static void savecursor(struct fyvt_state *state, int save)
{
  if(save) {
    state->saved.pos = state->pos;
    state->saved.mode.cursor_visible = state->mode.cursor_visible;
    state->saved.mode.cursor_blink   = state->mode.cursor_blink;
    state->saved.mode.cursor_shape   = state->mode.cursor_shape;

    fyvt_state_savepen(state, 1);
  }
  else {
    struct fyvt_pos oldpos = state->pos;

    state->pos = state->saved.pos;

    settermprop_bool(state, FYVT_PROP_CURSORVISIBLE, state->saved.mode.cursor_visible);
    settermprop_bool(state, FYVT_PROP_CURSORBLINK,   state->saved.mode.cursor_blink);
    settermprop_int (state, FYVT_PROP_CURSORSHAPE,   state->saved.mode.cursor_shape);

    fyvt_state_savepen(state, 0);

    updatecursor(state, &oldpos, 1);
  }
}

static int on_escape(const char *bytes, size_t len, void *user)
{
  struct fyvt_state *state = user;

  /* Easier to decode this from the first byte, even though the final
   * byte terminates it
   */
  switch(bytes[0]) {
  case ' ':
    if(len != 2)
      return 0;

    switch(bytes[1]) {
      case 'F': // S7C1T
        state->vt->mode.ctrl8bit = 0;
        break;

      case 'G': // S8C1T
        state->vt->mode.ctrl8bit = 1;
        break;

      default:
        return 0;
    }
    return 2;

  case '#':
    if(len != 2)
      return 0;

    switch(bytes[1]) {
      case '3': // DECDHL top
        if(state->mode.leftrightmargin)
          break;
        set_lineinfo(state, state->pos.row, NO_FORCE, DWL_ON, DHL_TOP);
        break;

      case '4': // DECDHL bottom
        if(state->mode.leftrightmargin)
          break;
        set_lineinfo(state, state->pos.row, NO_FORCE, DWL_ON, DHL_BOTTOM);
        break;

      case '5': // DECSWL
        if(state->mode.leftrightmargin)
          break;
        set_lineinfo(state, state->pos.row, NO_FORCE, DWL_OFF, DHL_OFF);
        break;

      case '6': // DECDWL
        if(state->mode.leftrightmargin)
          break;
        set_lineinfo(state, state->pos.row, NO_FORCE, DWL_ON, DHL_OFF);
        break;

      case '8': // DECALN
      {
        struct fyvt_pos pos;
        uint32_t E[] = { 'E', 0 };
        for(pos.row = 0; pos.row < state->rows; pos.row++)
          for(pos.col = 0; pos.col < ROWWIDTH(state, pos.row); pos.col++)
            putglyph(state, E, 1, pos);
        break;
      }

      default:
        return 0;
    }
    return 2;

  case '(': case ')': case '*': case '+': // SCS
    if(len != 2)
      return 0;

    {
      int setnum = bytes[0] - 0x28;
      struct fyvt_encoding *newenc = fyvt_lookup_encoding(ENC_SINGLE_94, bytes[1]);

      if(newenc) {
        state->encoding[setnum].enc = newenc;

        if(newenc->init)
          (*newenc->init)(newenc, state->encoding[setnum].data);
      }
    }

    return 2;

  case '7': // DECSC
    savecursor(state, 1);
    return 1;

  case '8': // DECRC
    savecursor(state, 0);
    return 1;

  case '<': // Ignored by VT100. Used in VT52 mode to switch up to VT100
    return 1;

  case '=': // DECKPAM
    state->mode.keypad = 1;
    return 1;

  case '>': // DECKPNM
    state->mode.keypad = 0;
    return 1;

  case 'c': // RIS - ECMA-48 8.3.105
  {
    struct fyvt_pos oldpos = state->pos;
    fyvt_state_reset(state, 1);
    if(state->callbacks && state->callbacks->movecursor)
      (*state->callbacks->movecursor)(state->pos, oldpos, state->mode.cursor_visible, state->cbdata);
    return 1;
  }

  case 'n': // LS2 - ECMA-48 8.3.78
    state->gl_set = 2;
    return 1;

  case 'o': // LS3 - ECMA-48 8.3.80
    state->gl_set = 3;
    return 1;

  case '~': // LS1R - ECMA-48 8.3.77
    state->gr_set = 1;
    return 1;

  case '}': // LS2R - ECMA-48 8.3.79
    state->gr_set = 2;
    return 1;

  case '|': // LS3R - ECMA-48 8.3.81
    state->gr_set = 3;
    return 1;

  default:
    return 0;
  }
}

static void set_mode(struct fyvt_state *state, int num, int val)
{
  switch(num) {
  case 4: // IRM - ECMA-48 7.2.10
    state->mode.insert = val;
    break;

  case 20: // LNM - ANSI X3.4-1977
    state->mode.newline = val;
    break;

  default:
    DEBUG_LOG("libvterm: Unknown mode %d\n", num);
    return;
  }
}

static void set_dec_mode(struct fyvt_state *state, int num, int val)
{
  switch(num) {
  case 1:
    state->mode.cursor = val;
    break;

  case 5: // DECSCNM - screen mode
    settermprop_bool(state, FYVT_PROP_REVERSE, val);
    break;

  case 6: // DECOM - origin mode
    {
      struct fyvt_pos oldpos = state->pos;
      state->mode.origin = val;
      state->pos.row = state->mode.origin ? state->scrollregion_top : 0;
      state->pos.col = state->mode.origin ? SCROLLREGION_LEFT(state) : 0;
      updatecursor(state, &oldpos, 1);
    }
    break;

  case 7:
    state->mode.autowrap = val;
    break;

  case 12:
    settermprop_bool(state, FYVT_PROP_CURSORBLINK, val);
    break;

  case 25:
    settermprop_bool(state, FYVT_PROP_CURSORVISIBLE, val);
    break;

  case 69: // DECVSSM - vertical split screen mode
           // DECLRMM - left/right margin mode
    state->mode.leftrightmargin = val;
    if(val) {
      // Setting DECVSSM must clear doublewidth/doubleheight state of every line
      for(int row = 0; row < state->rows; row++)
        set_lineinfo(state, row, FORCE, DWL_OFF, DHL_OFF);
    }

    break;

  case 1000:
  case 1002:
  case 1003:
    settermprop_int(state, FYVT_PROP_MOUSE,
        !val          ? FYVT_PROP_MOUSE_NONE  :
        (num == 1000) ? FYVT_PROP_MOUSE_CLICK :
        (num == 1002) ? FYVT_PROP_MOUSE_DRAG  :
                        FYVT_PROP_MOUSE_MOVE);
    break;

  case 1004:
    settermprop_bool(state, FYVT_PROP_FOCUSREPORT, val);
    state->mode.report_focus = val;
    break;

  case 1005:
    state->mouse_protocol = val ? MOUSE_UTF8 : MOUSE_X10;
    break;

  case 1006:
    state->mouse_protocol = val ? MOUSE_SGR : MOUSE_X10;
    break;

  case 1015:
    state->mouse_protocol = val ? MOUSE_RXVT : MOUSE_X10;
    break;

  case 1047:
    settermprop_bool(state, FYVT_PROP_ALTSCREEN, val);
    break;

  case 1048:
    savecursor(state, val);
    break;

  case 1049:
    settermprop_bool(state, FYVT_PROP_ALTSCREEN, val);
    savecursor(state, val);
    break;

  case 2004:
    state->mode.bracketpaste = val;
    break;

  case 2026: // Synchronized output
    settermprop_bool(state, FYVT_PROP_SYNCOUTPUT, val);
    break;

  case 2031: // Theme update notifications
    settermprop_bool(state, FYVT_PROP_THEMEUPDATES, val);
    break;

  default:
    DEBUG_LOG("libvterm: Unknown DEC mode %d\n", num);
    return;
  }
}

static void request_dec_mode(struct fyvt_state *state, int num)
{
  int reply;

  switch(num) {
    case 1:
      reply = state->mode.cursor;
      break;

    case 5:
      reply = state->mode.screen;
      break;

    case 6:
      reply = state->mode.origin;
      break;

    case 7:
      reply = state->mode.autowrap;
      break;

    case 12:
      reply = state->mode.cursor_blink;
      break;

    case 25:
      reply = state->mode.cursor_visible;
      break;

    case 69:
      reply = state->mode.leftrightmargin;
      break;

    case 1000:
      reply = state->mouse_flags == MOUSE_WANT_CLICK;
      break;

    case 1002:
      reply = state->mouse_flags == (MOUSE_WANT_CLICK|MOUSE_WANT_DRAG);
      break;

    case 1003:
      reply = state->mouse_flags == (MOUSE_WANT_CLICK|MOUSE_WANT_MOVE);
      break;

    case 1004:
      reply = state->mode.report_focus;
      break;

    case 1005:
      reply = state->mouse_protocol == MOUSE_UTF8;
      break;

    case 1006:
      reply = state->mouse_protocol == MOUSE_SGR;
      break;

    case 1015:
      reply = state->mouse_protocol == MOUSE_RXVT;
      break;

    case 1047:
      reply = state->mode.alt_screen;
      break;

    case 2004:
      reply = state->mode.bracketpaste;
      break;

    case 2026:
      reply = state->mode.synchronized_output;
      break;

    case 2031:
      reply = state->mode.theme_updates;
      break;

    default:
      fyvt_push_output_sprintf_ctrl(state->vt, C1_CSI, "?%d;%d$y", num, 0);
      return;
  }

  fyvt_push_output_sprintf_ctrl(state->vt, C1_CSI, "?%d;%d$y", num, reply ? 1 : 2);
}

static void request_version_string(struct fyvt_state *state)
{
  fyvt_push_output_sprintf_str(state->vt, C1_DCS, true, ">|" FYVT_XTVERSION_NAME "(%d.%d)",
      FYVT_XTVERSION_MAJOR, FYVT_XTVERSION_MINOR);
}

/* Kitty keyboard protocol flag stack (upstream commit 6f0bde11cc,
 * "feat(terminal): add support for kitty keyboard protocol"). CSI ? u
 * queries the top of the stack, CSI > u pushes a new entry, CSI < u pops
 * entries, and CSI = u replaces the top entry. */

static void request_key_encoding_flags(struct fyvt_state *state)
{
  int screen = state->mode.alt_screen ? BUFIDX_ALTSCREEN : BUFIDX_PRIMARY;
  struct fyvt_key_encoding_stack *stack = &state->key_encoding_stacks[screen];

  int reply = 0;

  assert(stack->size > 0);
  struct fyvt_key_encoding_flags flags = stack->items[stack->size - 1];

  if(flags.disambiguate)
    reply |= KEY_ENCODING_DISAMBIGUATE;
  if(flags.report_events)
    reply |= KEY_ENCODING_REPORT_EVENTS;
  if(flags.report_alternate)
    reply |= KEY_ENCODING_REPORT_ALTERNATE;
  if(flags.report_all_keys)
    reply |= KEY_ENCODING_REPORT_ALL_KEYS;
  if(flags.report_associated)
    reply |= KEY_ENCODING_REPORT_ASSOCIATED;

  fyvt_push_output_sprintf_ctrl(state->vt, C1_CSI, "?%du", reply);
}

static void set_key_encoding_flags(struct fyvt_state *state, int arg, int mode)
{
  // When mode is 3, bits set in arg reset the corresponding mode
  bool set = mode != 3;

  // When mode is 1, unset bits are reset
  bool reset_unset = mode == 1;

  struct fyvt_key_encoding_flags flags = { 0 };
  if(arg & KEY_ENCODING_DISAMBIGUATE)
    flags.disambiguate = set;
  else if(reset_unset)
    flags.disambiguate = false;

  if(arg & KEY_ENCODING_REPORT_EVENTS)
    flags.report_events = set;
  else if(reset_unset)
    flags.report_events = false;

  if(arg & KEY_ENCODING_REPORT_ALTERNATE)
    flags.report_alternate = set;
  else if(reset_unset)
    flags.report_alternate = false;

  if(arg & KEY_ENCODING_REPORT_ALL_KEYS)
    flags.report_all_keys = set;
  else if(reset_unset)
    flags.report_all_keys = false;

  if(arg & KEY_ENCODING_REPORT_ASSOCIATED)
    flags.report_associated = set;
  else if(reset_unset)
    flags.report_associated = false;

  int screen = state->mode.alt_screen ? BUFIDX_ALTSCREEN : BUFIDX_PRIMARY;
  struct fyvt_key_encoding_stack *stack = &state->key_encoding_stacks[screen];
  assert(stack->size > 0);
  stack->items[stack->size - 1] = flags;
}

static void push_key_encoding_flags(struct fyvt_state *state, int arg)
{
  int screen = state->mode.alt_screen ? BUFIDX_ALTSCREEN : BUFIDX_PRIMARY;
  struct fyvt_key_encoding_stack *stack = &state->key_encoding_stacks[screen];
  assert(stack->size <= sizeof(stack->items)/sizeof(stack->items[0]));

  if(stack->size == sizeof(stack->items)/sizeof(stack->items[0])) {
    // Evict the oldest entry when the stack is full
    for(size_t i = 0; i < sizeof(stack->items)/sizeof(stack->items[0]) - 1; i++)
      stack->items[i] = stack->items[i + 1];
  }
  else {
    stack->size++;
  }

  set_key_encoding_flags(state, arg, 1);
}

static void pop_key_encoding_flags(struct fyvt_state *state, int arg)
{
  int screen = state->mode.alt_screen ? BUFIDX_ALTSCREEN : BUFIDX_PRIMARY;
  struct fyvt_key_encoding_stack *stack = &state->key_encoding_stacks[screen];
  if(arg >= stack->size) {
    stack->size = 1;

    // If a pop request empties the stack, all flags are reset.
    memset(&stack->items[0], 0, sizeof(stack->items[0]));
  }
  else if(arg > 0) {
    stack->size -= arg;
  }
}

static int on_csi(const char *leader, const long args[], int argcount, const char *intermed, char command, void *user)
{
  struct fyvt_state *state = user;
  int leader_byte = 0;
  int intermed_byte = 0;
  int cancel_phantom = 1;

  if(leader && leader[0]) {
    if(leader[1]) // longer than 1 char
      return 0;

    switch(leader[0]) {
    case '?':
    case '>':
    case '<': // Kitty keyboard protocol: pop flags
    case '=': // Kitty keyboard protocol: set flags
      leader_byte = leader[0];
      break;
    default:
      return 0;
    }
  }

  if(intermed && intermed[0]) {
    if(intermed[1]) // longer than 1 char
      return 0;

    switch(intermed[0]) {
    case ' ':
    case '!':
    case '"':
    case '$':
    case '\'':
      intermed_byte = intermed[0];
      break;
    default:
      return 0;
    }
  }

  struct fyvt_pos oldpos = state->pos;

  // Some temporaries for later code
  int count, val;
  int row, col;
  struct fyvt_rect rect;
  int selective;

#define LBOUND(v,min) if((v) < (min)) (v) = (min)
#define UBOUND(v,max) if((v) > (max)) (v) = (max)

#define LEADER(l,b) ((l << 8) | b)
#define INTERMED(i,b) ((i << 16) | b)

  switch(intermed_byte << 16 | leader_byte << 8 | command) {
  case 0x40: // ICH - ECMA-48 8.3.64
    count = CSI_ARG_COUNT(args[0]);

    if(!is_cursor_in_scrollregion(state))
      break;

    rect.start_row = state->pos.row;
    rect.end_row   = state->pos.row + 1;
    rect.start_col = state->pos.col;
    if(state->mode.leftrightmargin)
      rect.end_col = SCROLLREGION_RIGHT(state);
    else
      rect.end_col = THISROWWIDTH(state);

    scroll(state, rect, 0, -count);

    break;

  case 0x41: // CUU - ECMA-48 8.3.22
    count = CSI_ARG_COUNT(args[0]);
    state->pos.row -= count;
    state->at_phantom = 0;
    break;

  case 0x42: // CUD - ECMA-48 8.3.19
    count = CSI_ARG_COUNT(args[0]);
    state->pos.row += count;
    state->at_phantom = 0;
    break;

  case 0x43: // CUF - ECMA-48 8.3.20
    count = CSI_ARG_COUNT(args[0]);
    state->pos.col += count;
    state->at_phantom = 0;
    break;

  case 0x44: // CUB - ECMA-48 8.3.18
    count = CSI_ARG_COUNT(args[0]);
    state->pos.col -= count;
    state->at_phantom = 0;
    break;

  case 0x45: // CNL - ECMA-48 8.3.12
    count = CSI_ARG_COUNT(args[0]);
    state->pos.col = 0;
    state->pos.row += count;
    state->at_phantom = 0;
    break;

  case 0x46: // CPL - ECMA-48 8.3.13
    count = CSI_ARG_COUNT(args[0]);
    state->pos.col = 0;
    state->pos.row -= count;
    state->at_phantom = 0;
    break;

  case 0x47: // CHA - ECMA-48 8.3.9
    val = CSI_ARG_OR(args[0], 1);
    state->pos.col = val-1;
    state->at_phantom = 0;
    break;

  case 0x48: // CUP - ECMA-48 8.3.21
    row = CSI_ARG_OR(args[0], 1);
    col = argcount < 2 || CSI_ARG_IS_MISSING(args[1]) ? 1 : CSI_ARG(args[1]);
    // zero-based
    state->pos.row = row-1;
    state->pos.col = col-1;
    if(state->mode.origin) {
      state->pos.row += state->scrollregion_top;
      state->pos.col += SCROLLREGION_LEFT(state);
    }
    state->at_phantom = 0;
    break;

  case 0x49: // CHT - ECMA-48 8.3.10
    count = CSI_ARG_COUNT(args[0]);
    tab(state, count, +1);
    break;

  case 0x4a: // ED - ECMA-48 8.3.39
  case LEADER('?', 0x4a): // DECSED - Selective Erase in Display
    selective = (leader_byte == '?');
    switch(CSI_ARG(args[0])) {
    case CSI_ARG_MISSING:
    case 0:
      rect.start_row = state->pos.row; rect.end_row = state->pos.row + 1;
      rect.start_col = state->pos.col; rect.end_col = state->cols;
      if(rect.end_col > rect.start_col)
        erase(state, rect, selective);

      rect.start_row = state->pos.row + 1; rect.end_row = state->rows;
      rect.start_col = 0;
      for(int row = rect.start_row; row < rect.end_row; row++)
        set_lineinfo(state, row, FORCE, DWL_OFF, DHL_OFF);
      if(rect.end_row > rect.start_row)
        erase(state, rect, selective);
      break;

    case 1:
      rect.start_row = 0; rect.end_row = state->pos.row;
      rect.start_col = 0; rect.end_col = state->cols;
      for(int row = rect.start_row; row < rect.end_row; row++)
        set_lineinfo(state, row, FORCE, DWL_OFF, DHL_OFF);
      if(rect.end_col > rect.start_col)
        erase(state, rect, selective);

      rect.start_row = state->pos.row; rect.end_row = state->pos.row + 1;
                          rect.end_col = state->pos.col + 1;
      if(rect.end_row > rect.start_row)
        erase(state, rect, selective);
      break;

    case 2:
      rect.start_row = 0; rect.end_row = state->rows;
      rect.start_col = 0; rect.end_col = state->cols;
      for(int row = rect.start_row; row < rect.end_row; row++)
        set_lineinfo(state, row, FORCE, DWL_OFF, DHL_OFF);
      erase(state, rect, selective);
      break;

    case 3:
      if(state->callbacks && state->callbacks->sb_clear)
        if((*state->callbacks->sb_clear)(state->cbdata))
          return 1;
      break;
    }
    break;

  case 0x4b: // EL - ECMA-48 8.3.41
  case LEADER('?', 0x4b): // DECSEL - Selective Erase in Line
    selective = (leader_byte == '?');
    rect.start_row = state->pos.row;
    rect.end_row   = state->pos.row + 1;

    switch(CSI_ARG(args[0])) {
    case CSI_ARG_MISSING:
    case 0:
      rect.start_col = state->pos.col; rect.end_col = THISROWWIDTH(state); break;
    case 1:
      rect.start_col = 0; rect.end_col = state->pos.col + 1; break;
    case 2:
      rect.start_col = 0; rect.end_col = THISROWWIDTH(state); break;
    default:
      return 0;
    }

    if(rect.end_col > rect.start_col)
      erase(state, rect, selective);

    break;

  case 0x4c: // IL - ECMA-48 8.3.67
    count = CSI_ARG_COUNT(args[0]);

    if(!is_cursor_in_scrollregion(state))
      break;

    rect.start_row = state->pos.row;
    rect.end_row   = SCROLLREGION_BOTTOM(state);
    rect.start_col = SCROLLREGION_LEFT(state);
    rect.end_col   = SCROLLREGION_RIGHT(state);

    scroll(state, rect, -count, 0);

    break;

  case 0x4d: // DL - ECMA-48 8.3.32
    count = CSI_ARG_COUNT(args[0]);

    if(!is_cursor_in_scrollregion(state))
      break;

    rect.start_row = state->pos.row;
    rect.end_row   = SCROLLREGION_BOTTOM(state);
    rect.start_col = SCROLLREGION_LEFT(state);
    rect.end_col   = SCROLLREGION_RIGHT(state);

    scroll(state, rect, count, 0);

    break;

  case 0x50: // DCH - ECMA-48 8.3.26
    count = CSI_ARG_COUNT(args[0]);

    if(!is_cursor_in_scrollregion(state))
      break;

    rect.start_row = state->pos.row;
    rect.end_row   = state->pos.row + 1;
    rect.start_col = state->pos.col;
    if(state->mode.leftrightmargin)
      rect.end_col = SCROLLREGION_RIGHT(state);
    else
      rect.end_col = THISROWWIDTH(state);

    scroll(state, rect, 0, count);

    break;

  case 0x53: // SU - ECMA-48 8.3.147
    count = CSI_ARG_COUNT(args[0]);

    rect.start_row = state->scrollregion_top;
    rect.end_row   = SCROLLREGION_BOTTOM(state);
    rect.start_col = SCROLLREGION_LEFT(state);
    rect.end_col   = SCROLLREGION_RIGHT(state);

    scroll(state, rect, count, 0);

    break;

  case 0x54: // SD - ECMA-48 8.3.113
    count = CSI_ARG_COUNT(args[0]);

    rect.start_row = state->scrollregion_top;
    rect.end_row   = SCROLLREGION_BOTTOM(state);
    rect.start_col = SCROLLREGION_LEFT(state);
    rect.end_col   = SCROLLREGION_RIGHT(state);

    scroll(state, rect, -count, 0);

    break;

  case 0x58: // ECH - ECMA-48 8.3.38
    count = CSI_ARG_COUNT(args[0]);

    rect.start_row = state->pos.row;
    rect.end_row   = state->pos.row + 1;
    rect.start_col = state->pos.col;
    rect.end_col   = state->pos.col + count;
    UBOUND(rect.end_col, THISROWWIDTH(state));

    erase(state, rect, 0);
    break;

  case 0x5a: // CBT - ECMA-48 8.3.7
    count = CSI_ARG_COUNT(args[0]);
    tab(state, count, -1);
    break;

  case 0x60: // HPA - ECMA-48 8.3.57
    col = CSI_ARG_OR(args[0], 1);
    state->pos.col = col-1;
    state->at_phantom = 0;
    break;

  case 0x61: // HPR - ECMA-48 8.3.59
    count = CSI_ARG_COUNT(args[0]);
    state->pos.col += count;
    state->at_phantom = 0;
    break;

  case 0x62: { // REP - ECMA-48 8.3.103
    const int row_width = THISROWWIDTH(state);
    count = CSI_ARG_COUNT(args[0]);
    col = state->pos.col + count;
    UBOUND(col, row_width);
    while (state->pos.col < col) {
      putglyph(state, state->combine_chars, state->combine_width, state->pos);
      state->pos.col += state->combine_width;
    }
    if (state->pos.col + state->combine_width >= row_width) {
      if (state->mode.autowrap) {
        state->at_phantom = 1;
        cancel_phantom = 0;
      }
    }
    break;
  }

  case 0x63: // DA - ECMA-48 8.3.24
    val = CSI_ARG_OR(args[0], 0);
    if(val == 0)
      /* DEC VT100 response. Neovim carries a modernized reply here
       * (upstream commits 977e91b424 and 112092271b): level 1 VT100
       * emulation (61) plus ANSI color support (22), instead of the
       * original "?1;2c". */
      fyvt_push_output_sprintf_ctrl(state->vt, C1_CSI, "?61;22;52c");
    break;

  case LEADER('>', 0x63): // DEC secondary Device Attributes
    fyvt_push_output_sprintf_ctrl(state->vt, C1_CSI, ">%d;%d;%dc", 0, 100, 0);
    break;

  case 0x64: // VPA - ECMA-48 8.3.158
    row = CSI_ARG_OR(args[0], 1);
    state->pos.row = row-1;
    if(state->mode.origin)
      state->pos.row += state->scrollregion_top;
    state->at_phantom = 0;
    break;

  case 0x65: // VPR - ECMA-48 8.3.160
    count = CSI_ARG_COUNT(args[0]);
    state->pos.row += count;
    state->at_phantom = 0;
    break;

  case 0x66: // HVP - ECMA-48 8.3.63
    row = CSI_ARG_OR(args[0], 1);
    col = argcount < 2 || CSI_ARG_IS_MISSING(args[1]) ? 1 : CSI_ARG(args[1]);
    // zero-based
    state->pos.row = row-1;
    state->pos.col = col-1;
    if(state->mode.origin) {
      state->pos.row += state->scrollregion_top;
      state->pos.col += SCROLLREGION_LEFT(state);
    }
    state->at_phantom = 0;
    break;

  case 0x67: // TBC - ECMA-48 8.3.154
    val = CSI_ARG_OR(args[0], 0);

    switch(val) {
    case 0:
      clear_col_tabstop(state, state->pos.col);
      break;
    case 3:
    case 5:
      for(col = 0; col < state->cols; col++)
        clear_col_tabstop(state, col);
      break;
    case 1:
    case 2:
    case 4:
      break;
    /* TODO: 1, 2 and 4 aren't meaningful yet without line tab stops */
    default:
      return 0;
    }
    break;

  case 0x68: // SM - ECMA-48 8.3.125
    if(!CSI_ARG_IS_MISSING(args[0]))
      set_mode(state, CSI_ARG(args[0]), 1);
    break;

  case LEADER('?', 0x68): // DEC private mode set
    for(int i = 0; i < argcount; i++) {
      if(!CSI_ARG_IS_MISSING(args[i]))
        set_dec_mode(state, CSI_ARG(args[i]), 1);
    }
    break;

  case 0x6a: // HPB - ECMA-48 8.3.58
    count = CSI_ARG_COUNT(args[0]);
    state->pos.col -= count;
    state->at_phantom = 0;
    break;

  case 0x6b: // VPB - ECMA-48 8.3.159
    count = CSI_ARG_COUNT(args[0]);
    state->pos.row -= count;
    state->at_phantom = 0;
    break;

  case 0x6c: // RM - ECMA-48 8.3.106
    if(!CSI_ARG_IS_MISSING(args[0]))
      set_mode(state, CSI_ARG(args[0]), 0);
    break;

  case LEADER('?', 0x6c): // DEC private mode reset
    for(int i = 0; i < argcount; i++) {
      if(!CSI_ARG_IS_MISSING(args[i]))
        set_dec_mode(state, CSI_ARG(args[i]), 0);
    }
    break;

  case 0x6d: // SGR - ECMA-48 8.3.117
    fyvt_state_setpen(state, args, argcount);
    break;

  case LEADER('?', 0x6d): // DECSGR
    /* No actual DEC terminal recognised these, but some printers did. These
     * are alternative ways to request subscript/superscript/off
     */
    for(int argi = 0; argi < argcount; argi++) {
      long arg;
      switch(arg = CSI_ARG(args[argi])) {
        case 4: // Superscript on
          arg = 73;
          fyvt_state_setpen(state, &arg, 1);
          break;
        case 5: // Subscript on
          arg = 74;
          fyvt_state_setpen(state, &arg, 1);
          break;
        case 24: // Super+subscript off
          arg = 75;
          fyvt_state_setpen(state, &arg, 1);
          break;
      }
    }
    break;

  case 0x6e: // DSR - ECMA-48 8.3.35
  case LEADER('?', 0x6e): // DECDSR
    val = CSI_ARG_OR(args[0], 0);

    {
      char *qmark = (leader_byte == '?') ? "?" : "";
      /* Neovim carries a theme query here (upstream commit f1c45fc7a4,
       * "feat(terminal): support theme update notifications (DEC mode
       * 2031)"). */
      bool dark = false;

      switch(val) {
      case 0: case 1: case 2: case 3: case 4:
        // ignore - these are replies
        break;
      case 5:
        fyvt_push_output_sprintf_ctrl(state->vt, C1_CSI, "%s0n", qmark);
        break;
      case 6: // CPR - cursor position report
        fyvt_push_output_sprintf_ctrl(state->vt, C1_CSI, "%s%d;%dR", qmark, state->pos.row + 1, state->pos.col + 1);
        break;
      case 996: // XTQTHEME - query the color scheme
        if(state->callbacks && state->callbacks->theme) {
          if(state->callbacks->theme(&dark, state->cbdata))
            fyvt_push_output_sprintf_ctrl(state->vt, C1_CSI, "?997;%cn", dark ? '1' : '2');
        }
        break;
      }
    }
    break;


  case INTERMED('!', 0x70): // DECSTR - DEC soft terminal reset
    fyvt_state_reset(state, 0);
    break;

  case LEADER('?', INTERMED('$', 0x70)):
    request_dec_mode(state, CSI_ARG(args[0]));
    break;

  case LEADER('>', 0x71): // XTVERSION - xterm query version string
    request_version_string(state);
    break;

  case LEADER('?', 0x75): // Kitty keyboard protocol: query flags
    request_key_encoding_flags(state);
    break;

  case LEADER('>', 0x75): // Kitty keyboard protocol: push flags
    push_key_encoding_flags(state, CSI_ARG_OR(args[0], 0));
    break;

  case LEADER('<', 0x75): // Kitty keyboard protocol: pop flags
    pop_key_encoding_flags(state, CSI_ARG_OR(args[0], 1));
    break;

  case LEADER('=', 0x75): // Kitty keyboard protocol: set flags
    val = argcount < 2 || CSI_ARG_IS_MISSING(args[1]) ? 1 : CSI_ARG(args[1]);
    set_key_encoding_flags(state, CSI_ARG_OR(args[0], 0), val);
    break;

  case INTERMED(' ', 0x71): // DECSCUSR - DEC set cursor shape
    val = CSI_ARG_OR(args[0], 1);

    switch(val) {
    case 0: case 1:
      settermprop_bool(state, FYVT_PROP_CURSORBLINK, 1);
      settermprop_int (state, FYVT_PROP_CURSORSHAPE, FYVT_PROP_CURSORSHAPE_BLOCK);
      break;
    case 2:
      settermprop_bool(state, FYVT_PROP_CURSORBLINK, 0);
      settermprop_int (state, FYVT_PROP_CURSORSHAPE, FYVT_PROP_CURSORSHAPE_BLOCK);
      break;
    case 3:
      settermprop_bool(state, FYVT_PROP_CURSORBLINK, 1);
      settermprop_int (state, FYVT_PROP_CURSORSHAPE, FYVT_PROP_CURSORSHAPE_UNDERLINE);
      break;
    case 4:
      settermprop_bool(state, FYVT_PROP_CURSORBLINK, 0);
      settermprop_int (state, FYVT_PROP_CURSORSHAPE, FYVT_PROP_CURSORSHAPE_UNDERLINE);
      break;
    case 5:
      settermprop_bool(state, FYVT_PROP_CURSORBLINK, 1);
      settermprop_int (state, FYVT_PROP_CURSORSHAPE, FYVT_PROP_CURSORSHAPE_BAR_LEFT);
      break;
    case 6:
      settermprop_bool(state, FYVT_PROP_CURSORBLINK, 0);
      settermprop_int (state, FYVT_PROP_CURSORSHAPE, FYVT_PROP_CURSORSHAPE_BAR_LEFT);
      break;
    }

    break;

  case INTERMED('"', 0x71): // DECSCA - DEC select character protection attribute
    val = CSI_ARG_OR(args[0], 0);

    switch(val) {
    case 0: case 2:
      state->protected_cell = 0;
      break;
    case 1:
      state->protected_cell = 1;
      break;
    }

    break;

  case 0x72: // DECSTBM - DEC custom
    state->scrollregion_top = CSI_ARG_OR(args[0], 1) - 1;
    state->scrollregion_bottom = argcount < 2 || CSI_ARG_IS_MISSING(args[1]) ? -1 : CSI_ARG(args[1]);
    LBOUND(state->scrollregion_top, 0);
    UBOUND(state->scrollregion_top, state->rows);
    LBOUND(state->scrollregion_bottom, -1);
    if(state->scrollregion_top == 0 && state->scrollregion_bottom == state->rows)
      state->scrollregion_bottom = -1;
    else
      UBOUND(state->scrollregion_bottom, state->rows);

    if(SCROLLREGION_BOTTOM(state) <= state->scrollregion_top) {
      // Invalid
      state->scrollregion_top    = 0;
      state->scrollregion_bottom = -1;
    }

    // Setting the scrolling region restores the cursor to the home position
    state->pos.row = 0;
    state->pos.col = 0;
    if(state->mode.origin) {
      state->pos.row += state->scrollregion_top;
      state->pos.col += SCROLLREGION_LEFT(state);
    }

    break;

  case 0x73: // DECSLRM - DEC custom
    // Always allow setting these margins, just they won't take effect without DECVSSM
    state->scrollregion_left = CSI_ARG_OR(args[0], 1) - 1;
    state->scrollregion_right = argcount < 2 || CSI_ARG_IS_MISSING(args[1]) ? -1 : CSI_ARG(args[1]);
    LBOUND(state->scrollregion_left, 0);
    UBOUND(state->scrollregion_left, state->cols);
    LBOUND(state->scrollregion_right, -1);
    if(state->scrollregion_left == 0 && state->scrollregion_right == state->cols)
      state->scrollregion_right = -1;
    else
      UBOUND(state->scrollregion_right, state->cols);

    if(state->scrollregion_right > -1 &&
       state->scrollregion_right <= state->scrollregion_left) {
      // Invalid
      state->scrollregion_left  = 0;
      state->scrollregion_right = -1;
    }

    // Setting the scrolling region restores the cursor to the home position
    state->pos.row = 0;
    state->pos.col = 0;
    if(state->mode.origin) {
      state->pos.row += state->scrollregion_top;
      state->pos.col += SCROLLREGION_LEFT(state);
    }

    break;

  case INTERMED('\'', 0x7D): // DECIC
    count = CSI_ARG_COUNT(args[0]);

    if(!is_cursor_in_scrollregion(state))
      break;

    rect.start_row = state->scrollregion_top;
    rect.end_row   = SCROLLREGION_BOTTOM(state);
    rect.start_col = state->pos.col;
    rect.end_col   = SCROLLREGION_RIGHT(state);

    scroll(state, rect, 0, -count);

    break;

  case INTERMED('\'', 0x7E): // DECDC
    count = CSI_ARG_COUNT(args[0]);

    if(!is_cursor_in_scrollregion(state))
      break;

    rect.start_row = state->scrollregion_top;
    rect.end_row   = SCROLLREGION_BOTTOM(state);
    rect.start_col = state->pos.col;
    rect.end_col   = SCROLLREGION_RIGHT(state);

    scroll(state, rect, 0, count);

    break;

  default:
    if(state->fallbacks && state->fallbacks->csi)
      if((*state->fallbacks->csi)(leader, args, argcount, intermed, command, state->fbdata))
        return 1;

    return 0;
  }

  if(state->mode.origin) {
    LBOUND(state->pos.row, state->scrollregion_top);
    UBOUND(state->pos.row, SCROLLREGION_BOTTOM(state)-1);
    LBOUND(state->pos.col, SCROLLREGION_LEFT(state));
    UBOUND(state->pos.col, SCROLLREGION_RIGHT(state)-1);
  }
  else {
    LBOUND(state->pos.row, 0);
    UBOUND(state->pos.row, state->rows-1);
    LBOUND(state->pos.col, 0);
    UBOUND(state->pos.col, THISROWWIDTH(state)-1);
  }

  updatecursor(state, &oldpos, cancel_phantom);

#ifdef DEBUG
  if(state->pos.row < 0 || state->pos.row >= state->rows ||
     state->pos.col < 0 || state->pos.col >= state->cols) {
    fprintf(stderr, "Position out of bounds after CSI %c: (%d,%d)\n",
        command, state->pos.row, state->pos.col);
    abort();
  }

  if(SCROLLREGION_BOTTOM(state) <= state->scrollregion_top) {
    fprintf(stderr, "Scroll region height out of bounds after CSI %c: %d <= %d\n",
        command, SCROLLREGION_BOTTOM(state), state->scrollregion_top);
    abort();
  }

  if(SCROLLREGION_RIGHT(state) <= SCROLLREGION_LEFT(state)) {
    fprintf(stderr, "Scroll region width out of bounds after CSI %c: %d <= %d\n",
        command, SCROLLREGION_RIGHT(state), SCROLLREGION_LEFT(state));
    abort();
  }
#endif

  return 1;
}

static char base64_one(uint8_t b)
{
  if(b < 26)
    return 'A' + b;
  else if(b < 52)
    return 'a' + b - 26;
  else if(b < 62)
    return '0' + b - 52;
  else if(b == 62)
    return '+';
  else if(b == 63)
    return '/';
  return 0;
}

static uint8_t unbase64one(char c)
{
  if(c >= 'A' && c <= 'Z')
    return c - 'A';
  else if(c >= 'a' && c <= 'z')
    return c - 'a' + 26;
  else if(c >= '0' && c <= '9')
    return c - '0' + 52;
  else if(c == '+')
    return 62;
  else if(c == '/')
    return 63;

  return 0xFF;
}

static void osc_selection(struct fyvt_state *state, struct fyvt_string_fragment frag)
{
  if(frag.initial) {
    state->tmp.selection.mask = 0;
    state->tmp.selection.state = SELECTION_INITIAL;
  }

  while(!state->tmp.selection.state && frag.len) {
    /* Parse selection parameter */
    switch(frag.str[0]) {
      case 'c':
        state->tmp.selection.mask |= FYVT_SELECTION_CLIPBOARD;
        break;
      case 'p':
        state->tmp.selection.mask |= FYVT_SELECTION_PRIMARY;
        break;
      case 'q':
        state->tmp.selection.mask |= FYVT_SELECTION_SECONDARY;
        break;
      case 's':
        state->tmp.selection.mask |= FYVT_SELECTION_SELECT;
        break;
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
        state->tmp.selection.mask |= (FYVT_SELECTION_CUT0 << (frag.str[0] - '0'));
        break;

      case ';':
        state->tmp.selection.state = SELECTION_SELECTED;
        if(!state->tmp.selection.mask)
          state->tmp.selection.mask = FYVT_SELECTION_SELECT|FYVT_SELECTION_CUT0;
        break;
    }

    frag.str++;
    frag.len--;
  }

  if(!frag.len) {
    /* Clear selection if we're already finished but didn't do anything */
    if(frag.final && state->selection.callbacks->set) {
      (*state->selection.callbacks->set)(state->tmp.selection.mask, (struct fyvt_string_fragment){
              .str     = NULL,
              .len     = 0,
              .initial = state->tmp.selection.state != SELECTION_SET,
              .final   = true,
            }, state->selection.user);
    }
    return;
  }

  if(state->tmp.selection.state == SELECTION_SELECTED) {
    if(frag.str[0] == '?') {
      state->tmp.selection.state = SELECTION_QUERY;
    }
    else {
      state->tmp.selection.state = SELECTION_SET_INITIAL;
      state->tmp.selection.recvpartial = 0;
    }
  }

  if(state->tmp.selection.state == SELECTION_QUERY) {
    if(state->selection.callbacks->query)
      (*state->selection.callbacks->query)(state->tmp.selection.mask, state->selection.user);
    return;
  }

  if(state->tmp.selection.state == SELECTION_INVALID)
    return;

  if(state->selection.callbacks->set) {
    size_t bufcur = 0;
    char *buffer = state->selection.buffer;

    uint32_t x = 0; /* Current decoding value */
    int n = 0;      /* Number of sextets consumed */

    if(state->tmp.selection.recvpartial) {
      n = state->tmp.selection.recvpartial >> 24;
      x = state->tmp.selection.recvpartial & 0x03FFFF; /* could be up to 18 bits of state in here */

      state->tmp.selection.recvpartial = 0;
    }

    while((state->selection.buflen - bufcur) >= 3 && frag.len) {
      if(frag.str[0] == '=') {
        if(n == 2) {
          buffer[0] = (x >> 4) & 0xFF;
          buffer += 1, bufcur += 1;
        }
        if(n == 3) {
          buffer[0] = (x >> 10) & 0xFF;
          buffer[1] = (x >>  2) & 0xFF;
          buffer += 2, bufcur += 2;
        }

        while(frag.len && frag.str[0] == '=')
          frag.str++, frag.len--;

        n = 0;
      }
      else {
        uint8_t b = unbase64one(frag.str[0]);
        if(b == 0xFF) {
          DEBUG_LOG("base64decode bad input %02X\n", (uint8_t)frag.str[0]);

          state->tmp.selection.state = SELECTION_INVALID;
          if(state->selection.callbacks->set) {
            (*state->selection.callbacks->set)(state->tmp.selection.mask, (struct fyvt_string_fragment){
                .str     = NULL,
                .len     = 0,
                .initial = true,
                .final   = true,
                }, state->selection.user);
          }
          break;
        }

        x = (x << 6) | b;
        n++;
        frag.str++, frag.len--;

        if(n == 4) {
          buffer[0] = (x >> 16) & 0xFF;
          buffer[1] = (x >>  8) & 0xFF;
          buffer[2] = (x >>  0) & 0xFF;

          buffer += 3, bufcur += 3;
          x = 0;
          n = 0;
        }
      }

      if(!frag.len || (state->selection.buflen - bufcur) < 3) {
        if(bufcur) {
          (*state->selection.callbacks->set)(state->tmp.selection.mask, (struct fyvt_string_fragment){
              .str     = state->selection.buffer,
              .len     = bufcur,
              .initial = state->tmp.selection.state == SELECTION_SET_INITIAL,
              .final   = frag.final && !frag.len,
            }, state->selection.user);
          state->tmp.selection.state = SELECTION_SET;
        }

        buffer = state->selection.buffer;
        bufcur = 0;
      }
    }

    if(n)
      state->tmp.selection.recvpartial = (n << 24) | x;
  }
}

/* Neovim carries a fix here (upstream commit ee143aaf65, "fix(terminal):
 * emit Termrequest for all OSC sequences"): the recognized OSC 0/1/2/52
 * commands used to return immediately, so a caller's osc fallback never saw
 * them. Fall through to the fallback for every command, recognized or not,
 * instead of only for the ones libvterm itself does not understand. */
static int on_osc(int command, struct fyvt_string_fragment frag, void *user)
{
  struct fyvt_state *state = user;

  switch(command) {
    case 0:
      settermprop_string(state, FYVT_PROP_ICONNAME, frag);
      settermprop_string(state, FYVT_PROP_TITLE, frag);
      break;

    case 1:
      settermprop_string(state, FYVT_PROP_ICONNAME, frag);
      break;

    case 2:
      settermprop_string(state, FYVT_PROP_TITLE, frag);
      break;

    case 52:
      if(state->selection.callbacks)
        osc_selection(state, frag);
      break;
  }

  if(state->fallbacks && state->fallbacks->osc)
    if((*state->fallbacks->osc)(command, frag, state->fbdata))
      return 1;

  return 0;
}

static void request_status_string(struct fyvt_state *state, struct fyvt_string_fragment frag)
{
  struct fyvt *vt = state->vt;

  char *tmp = state->tmp.decrqss;

  if(frag.initial)
    tmp[0] = tmp[1] = tmp[2] = tmp[3] = 0;

  size_t i = 0;
  while(i < sizeof(state->tmp.decrqss)-1 && tmp[i])
    i++;
  while(i < sizeof(state->tmp.decrqss)-1 && frag.len--)
    tmp[i++] = (frag.str++)[0];
  tmp[i] = 0;

  if(!frag.final)
    return;

  switch(tmp[0] | tmp[1]<<8 | tmp[2]<<16) {
    case 'm': {
      // Query SGR
      long args[20];
      int argc = fyvt_state_getpen(state, args, sizeof(args)/sizeof(args[0]));
      size_t cur = 0;

      cur += snprintf(vt->tmpbuffer + cur, vt->tmpbuffer_len - cur,
          vt->mode.ctrl8bit ? "\x90" "1$r" : ESC_S "P" "1$r"); // DCS 1$r ...
      if(cur >= vt->tmpbuffer_len)
        return;

      for(int argi = 0; argi < argc; argi++) {
        cur += snprintf(vt->tmpbuffer + cur, vt->tmpbuffer_len - cur,
            argi == argc - 1             ? "%ld" :
            CSI_ARG_HAS_MORE(args[argi]) ? "%ld:" :
                                           "%ld;",
            CSI_ARG(args[argi]));
        if(cur >= vt->tmpbuffer_len)
          return;
      }

      cur += snprintf(vt->tmpbuffer + cur, vt->tmpbuffer_len - cur,
          vt->mode.ctrl8bit ? "m" "\x9C" : "m" ESC_S "\\"); // ... m ST
      if(cur >= vt->tmpbuffer_len)
        return;

      fyvt_push_output_bytes(vt, vt->tmpbuffer, cur);
      return;
    }

    case 'r':
      // Query DECSTBM
      fyvt_push_output_sprintf_str(vt, C1_DCS, true,
          "1$r%d;%dr", state->scrollregion_top+1, SCROLLREGION_BOTTOM(state));
      return;

    case 's':
      // Query DECSLRM
      fyvt_push_output_sprintf_str(vt, C1_DCS, true,
          "1$r%d;%ds", SCROLLREGION_LEFT(state)+1, SCROLLREGION_RIGHT(state));
      return;

    case ' '|('q'<<8): {
      // Query DECSCUSR
      /* Default to the block shape: cursor_shape is only ever one of the
       * three below, but a DECSCUSR query must still answer something if a
       * future shape is added without updating this switch. */
      int reply = 2;
      switch(state->mode.cursor_shape) {
        case FYVT_PROP_CURSORSHAPE_BLOCK:     reply = 2; break;
        case FYVT_PROP_CURSORSHAPE_UNDERLINE: reply = 4; break;
        case FYVT_PROP_CURSORSHAPE_BAR_LEFT:  reply = 6; break;
      }
      if(state->mode.cursor_blink)
        reply--;
      fyvt_push_output_sprintf_str(vt, C1_DCS, true,
          "1$r%d q", reply);
      return;
    }

    case '\"'|('q'<<8):
      // Query DECSCA
      fyvt_push_output_sprintf_str(vt, C1_DCS, true,
          "1$r%d\"q", state->protected_cell ? 1 : 2);
      return;
  }

  fyvt_push_output_sprintf_str(state->vt, C1_DCS, true, "0$r");
}

static int on_dcs(const char *command, size_t commandlen, struct fyvt_string_fragment frag, void *user)
{
  struct fyvt_state *state = user;

  if(commandlen == 2 && strneq(command, "$q", 2)) {
    request_status_string(state, frag);
    return 1;
  }
  else if(state->fallbacks && state->fallbacks->dcs)
    if((*state->fallbacks->dcs)(command, commandlen, frag, state->fbdata))
      return 1;

  DEBUG_LOG("libvterm: Unhandled DCS %.*s\n", (int)commandlen, command);
  return 0;
}

static int on_apc(struct fyvt_string_fragment frag, void *user)
{
  struct fyvt_state *state = user;

  if(state->fallbacks && state->fallbacks->apc)
    if((*state->fallbacks->apc)(frag, state->fbdata))
      return 1;

  /* No DEBUG_LOG because all APCs are unhandled */
  return 0;
}

static int on_pm(struct fyvt_string_fragment frag, void *user)
{
  struct fyvt_state *state = user;

  if(state->fallbacks && state->fallbacks->pm)
    if((*state->fallbacks->pm)(frag, state->fbdata))
      return 1;

  /* No DEBUG_LOG because all PMs are unhandled */
  return 0;
}

static int on_sos(struct fyvt_string_fragment frag, void *user)
{
  struct fyvt_state *state = user;

  if(state->fallbacks && state->fallbacks->sos)
    if((*state->fallbacks->sos)(frag, state->fbdata))
      return 1;

  /* No DEBUG_LOG because all SOSs are unhandled */
  return 0;
}

static int on_resize(int rows, int cols, void *user)
{
  struct fyvt_state *state = user;
  struct fyvt_pos oldpos = state->pos;

  if(cols != state->cols) {
    unsigned char *newtabstops = fyvt_allocator_malloc(state->vt, (cols + 7) / 8);

    /* TODO: This can all be done much more efficiently bytewise */
    int col;
    for(col = 0; col < state->cols && col < cols; col++) {
      unsigned char mask = 1 << (col & 7);
      if(state->tabstops[col >> 3] & mask)
        newtabstops[col >> 3] |= mask;
      else
        newtabstops[col >> 3] &= ~mask;
      }

    for( ; col < cols; col++) {
      unsigned char mask = 1 << (col & 7);
      if(col % 8 == 0)
        newtabstops[col >> 3] |= mask;
      else
        newtabstops[col >> 3] &= ~mask;
    }

    fyvt_allocator_free(state->vt, state->tabstops);
    state->tabstops = newtabstops;
  }

  state->rows = rows;
  state->cols = cols;

  if(state->scrollregion_bottom > -1)
    UBOUND(state->scrollregion_bottom, state->rows);
  if(state->scrollregion_right > -1)
    UBOUND(state->scrollregion_right, state->cols);

  struct fyvt_state_fields fields = {
    .pos       = state->pos,
    .lineinfos = { [0] = state->lineinfos[0], [1] = state->lineinfos[1] },
  };

  if(state->callbacks && state->callbacks->resize) {
    (*state->callbacks->resize)(rows, cols, &fields, state->cbdata);
    state->pos = fields.pos;

    state->lineinfos[0] = fields.lineinfos[0];
    state->lineinfos[1] = fields.lineinfos[1];
  }
  else {
    if(rows != state->rows) {
      for(int bufidx = BUFIDX_PRIMARY; bufidx <= BUFIDX_ALTSCREEN; bufidx++) {
        struct fyvt_line_info *oldlineinfo = state->lineinfos[bufidx];
        if(!oldlineinfo)
          continue;

        struct fyvt_line_info *newlineinfo = fyvt_allocator_malloc(state->vt, rows * sizeof(struct fyvt_line_info));

        int row;
        for(row = 0; row < state->rows && row < rows; row++) {
          newlineinfo[row] = oldlineinfo[row];
        }

        for( ; row < rows; row++) {
          newlineinfo[row] = (struct fyvt_line_info){
            .doublewidth = 0,
          };
        }

        fyvt_allocator_free(state->vt, state->lineinfos[bufidx]);
        state->lineinfos[bufidx] = newlineinfo;
      }
    }
  }

  state->lineinfo = state->lineinfos[state->mode.alt_screen ? BUFIDX_ALTSCREEN : BUFIDX_PRIMARY];

  if(state->at_phantom && state->pos.col < cols-1) {
    state->at_phantom = 0;
    state->pos.col++;
  }

  if(state->pos.row < 0)
    state->pos.row = 0;
  if(state->pos.row >= rows)
    state->pos.row = rows - 1;
  if(state->pos.col < 0)
    state->pos.col = 0;
  if(state->pos.col >= cols)
    state->pos.col = cols - 1;

  updatecursor(state, &oldpos, 1);

  return 1;
}

static const struct fyvt_parser_callbacks parser_callbacks = {
  .text    = on_text,
  .control = on_control,
  .escape  = on_escape,
  .csi     = on_csi,
  .osc     = on_osc,
  .dcs     = on_dcs,
  .apc     = on_apc,
  .pm      = on_pm,
  .sos     = on_sos,
  .resize  = on_resize,
};

struct fyvt_state *fyvt_obtain_state(struct fyvt *vt)
{
  if(vt->state)
    return vt->state;

  struct fyvt_state *state = fyvt_state_new(vt);
  vt->state = state;

  fyvt_parser_set_callbacks(vt, &parser_callbacks, state);

  return state;
}

void fyvt_state_reset(struct fyvt_state *state, int hard)
{
  state->scrollregion_top = 0;
  state->scrollregion_bottom = -1;
  state->scrollregion_left = 0;
  state->scrollregion_right = -1;

  state->mode.keypad          = 0;
  state->mode.cursor          = 0;
  state->mode.autowrap        = 1;
  state->mode.insert          = 0;
  state->mode.newline         = 0;
  state->mode.alt_screen      = 0;
  state->mode.origin          = 0;
  state->mode.leftrightmargin = 0;
  state->mode.bracketpaste    = 0;
  state->mode.report_focus    = 0;

  state->mouse_flags = 0;

  state->vt->mode.ctrl8bit   = 0;

  for(int col = 0; col < state->cols; col++)
    if(col % 8 == 0)
      set_col_tabstop(state, col);
    else
      clear_col_tabstop(state, col);

  for(int row = 0; row < state->rows; row++)
    set_lineinfo(state, row, FORCE, DWL_OFF, DHL_OFF);

  if(state->callbacks && state->callbacks->initpen)
    (*state->callbacks->initpen)(state->cbdata);

  fyvt_state_resetpen(state);

  struct fyvt_encoding *default_enc = state->vt->mode.utf8 ?
      fyvt_lookup_encoding(ENC_UTF8,      'u') :
      fyvt_lookup_encoding(ENC_SINGLE_94, 'B');

  for(int i = 0; i < 4; i++) {
    state->encoding[i].enc = default_enc;
    if(default_enc->init)
      (*default_enc->init)(default_enc, state->encoding[i].data);
  }

  state->gl_set = 0;
  state->gr_set = 1;
  state->gsingle_set = 0;

  state->protected_cell = 0;

  // Initialise the props
  settermprop_bool(state, FYVT_PROP_CURSORVISIBLE, 1);
  settermprop_bool(state, FYVT_PROP_CURSORBLINK,   1);
  settermprop_int (state, FYVT_PROP_CURSORSHAPE,   FYVT_PROP_CURSORSHAPE_BLOCK);

  if(hard) {
    state->pos.row = 0;
    state->pos.col = 0;
    state->at_phantom = 0;

    struct fyvt_rect rect = { 0, state->rows, 0, state->cols };
    erase(state, rect, 0);
  }
}

void fyvt_state_get_cursorpos(const struct fyvt_state *state, struct fyvt_pos *cursorpos)
{
  *cursorpos = state->pos;
}

void fyvt_state_set_callbacks(struct fyvt_state *state, const struct fyvt_state_callbacks *callbacks, void *user)
{
  if(callbacks) {
    state->callbacks = callbacks;
    state->cbdata = user;

    if(state->callbacks && state->callbacks->initpen)
      (*state->callbacks->initpen)(state->cbdata);
  }
  else {
    state->callbacks = NULL;
    state->cbdata = NULL;
  }
}

void fyvt_state_callbacks_has_premove(struct fyvt_state *state)
{
  state->callbacks_has_premove = true;
}

void *fyvt_state_get_cbdata(struct fyvt_state *state)
{
  return state->cbdata;
}

void fyvt_state_set_unrecognised_fallbacks(struct fyvt_state *state, const struct fyvt_state_fallbacks *fallbacks, void *user)
{
  if(fallbacks) {
    state->fallbacks = fallbacks;
    state->fbdata = user;
  }
  else {
    state->fallbacks = NULL;
    state->fbdata = NULL;
  }
}

void *fyvt_state_get_unrecognised_fbdata(struct fyvt_state *state)
{
  return state->fbdata;
}

int fyvt_state_set_termprop(struct fyvt_state *state, enum fyvt_prop prop, union fyvt_value *val)
{
  /* Only store the new value of the property if usercode said it was happy.
   * This is especially important for altscreen switching */
  if(state->callbacks && state->callbacks->settermprop)
    if(!(*state->callbacks->settermprop)(prop, val, state->cbdata))
      return 0;

  switch(prop) {
  case FYVT_PROP_TITLE:
  case FYVT_PROP_ICONNAME:
    // we don't store these, just transparently pass through
    return 1;
  case FYVT_PROP_CURSORVISIBLE:
    state->mode.cursor_visible = val->boolean;
    return 1;
  case FYVT_PROP_CURSORBLINK:
    state->mode.cursor_blink = val->boolean;
    return 1;
  case FYVT_PROP_CURSORSHAPE:
    state->mode.cursor_shape = val->number;
    return 1;
  case FYVT_PROP_REVERSE:
    state->mode.screen = val->boolean;
    return 1;
  case FYVT_PROP_ALTSCREEN:
    state->mode.alt_screen = val->boolean;
    state->lineinfo = state->lineinfos[state->mode.alt_screen ? BUFIDX_ALTSCREEN : BUFIDX_PRIMARY];
    if(state->mode.alt_screen) {
      struct fyvt_rect rect = {
        .start_row = 0,
        .start_col = 0,
        .end_row = state->rows,
        .end_col = state->cols,
      };
      erase(state, rect, 0);
    }
    return 1;
  case FYVT_PROP_MOUSE:
    state->mouse_flags = 0;
    if(val->number)
      state->mouse_flags |= MOUSE_WANT_CLICK;
    if(val->number == FYVT_PROP_MOUSE_DRAG)
      state->mouse_flags |= MOUSE_WANT_DRAG;
    if(val->number == FYVT_PROP_MOUSE_MOVE)
      state->mouse_flags |= MOUSE_WANT_MOVE;
    return 1;
  case FYVT_PROP_FOCUSREPORT:
    state->mode.report_focus = val->boolean;
    return 1;
  case FYVT_PROP_THEMEUPDATES:
    state->mode.theme_updates = val->boolean;
    return 1;
  case FYVT_PROP_SYNCOUTPUT:
    state->mode.synchronized_output = val->boolean;
    return 1;

  case FYVT_N_PROPS:
    return 0;
  }

  return 0;
}

void fyvt_state_focus_in(struct fyvt_state *state)
{
  if(state->mode.report_focus)
    fyvt_push_output_sprintf_ctrl(state->vt, C1_CSI, "I");
}

void fyvt_state_focus_out(struct fyvt_state *state)
{
  if(state->mode.report_focus)
    fyvt_push_output_sprintf_ctrl(state->vt, C1_CSI, "O");
}

const struct fyvt_line_info *fyvt_state_get_lineinfo(const struct fyvt_state *state, int row)
{
  return state->lineinfo + row;
}

void fyvt_state_set_selection_callbacks(struct fyvt_state *state, const struct fyvt_selection_callbacks *callbacks, void *user,
    char *buffer, size_t buflen)
{
  bool owned = false;

  /* Replacing an earlier buffer we allocated ourselves. */
  if(state->selection.buffer_owned)
    fyvt_allocator_free(state->vt, state->selection.buffer);

  if(buflen && !buffer) {
    buffer = fyvt_allocator_malloc(state->vt, buflen);
    owned = true;
  }

  state->selection.callbacks   = callbacks;
  state->selection.user        = user;
  state->selection.buffer      = buffer;
  state->selection.buflen      = buflen;
  state->selection.buffer_owned = owned;
}

void fyvt_state_send_selection(struct fyvt_state *state, enum fyvt_selection_mask mask, struct fyvt_string_fragment frag)
{
  struct fyvt *vt = state->vt;

  if(frag.initial) {
    /* TODO: support sending more than one mask bit */
    static const char selection_chars[] = "cpqs";
    int idx;
    for(idx = 0; idx < 4; idx++)
      if(mask & (1 << idx))
        break;

    fyvt_push_output_sprintf_str(vt, C1_OSC, false, "52;%c;", selection_chars[idx]);

    state->tmp.selection.sendpartial = 0;
  }

  if(frag.len) {
    size_t bufcur = 0;
    char *buffer = state->selection.buffer;

    uint32_t x = 0;
    int n = 0;

    if(state->tmp.selection.sendpartial) {
      n = state->tmp.selection.sendpartial >> 24;
      x = state->tmp.selection.sendpartial & 0xFFFFFF;

      state->tmp.selection.sendpartial = 0;
    }

    while((state->selection.buflen - bufcur) >= 4 && frag.len) {
      x = (x << 8) | frag.str[0];
      n++;
      frag.str++, frag.len--;

      if(n == 3) {
        buffer[0] = base64_one((x >> 18) & 0x3F);
        buffer[1] = base64_one((x >> 12) & 0x3F);
        buffer[2] = base64_one((x >>  6) & 0x3F);
        buffer[3] = base64_one((x >>  0) & 0x3F);

        buffer += 4, bufcur += 4;
        x = 0;
        n = 0;
      }

      if(!frag.len || (state->selection.buflen - bufcur) < 4) {
        if(bufcur)
          fyvt_push_output_bytes(vt, state->selection.buffer, bufcur);

        buffer = state->selection.buffer;
        bufcur = 0;
      }
    }

    if(n)
      state->tmp.selection.sendpartial = (n << 24) | x;
  }

  if(frag.final) {
    if(state->tmp.selection.sendpartial) {
      int n      = state->tmp.selection.sendpartial >> 24;
      uint32_t x = state->tmp.selection.sendpartial & 0xFFFFFF;
      char *buffer = state->selection.buffer;

      /* n is either 1 or 2 now */
      x <<= (n == 1) ? 16 : 8;

      buffer[0] = base64_one((x >> 18) & 0x3F);
      buffer[1] = base64_one((x >> 12) & 0x3F);
      buffer[2] = (n == 1) ? '=' : base64_one((x >>  6) & 0x3F);
      buffer[3] = '=';

      fyvt_push_output_sprintf_str(vt, 0, true, "%.*s", 4, buffer);
    }
    else
      fyvt_push_output_sprintf_str(vt, 0, true, "");
  }
}
