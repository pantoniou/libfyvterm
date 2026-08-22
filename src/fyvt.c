#include "fyvt_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

/*****************
 * API functions *
 *****************/

static void *default_malloc(size_t size, void *allocdata UNUSED)
{
  void *ptr = malloc(size);
  if(ptr)
    memset(ptr, 0, size);
  return ptr;
}

static void default_free(void *ptr, void *allocdata UNUSED)
{
  free(ptr);
}

static struct fyvt_allocator default_allocator = {
  .malloc = &default_malloc,
  .free   = &default_free,
};

void fyvt_cfg_default(struct fyvt_cfg *cfg)
{
  if(!cfg)
    return;

  memset(cfg, 0, sizeof(*cfg));
  cfg->struct_size = sizeof(*cfg);
}

/* A handy macro for defaulting values out of the configuration fields */
#define DEFAULT(v, def)  ((v) ? (v) : (def))

struct fyvt *fyvt_create(const struct fyvt_cfg *cfg)
{
  const struct fyvt_cfg defaults = { .struct_size = sizeof(struct fyvt_cfg) };
  const struct fyvt_allocator *allocator;

  if(!cfg)
    cfg = &defaults;

  allocator = DEFAULT(cfg->allocator, &default_allocator);

  /* Need to bootstrap using the allocator function directly */
  struct fyvt *vt = (*allocator->malloc)(sizeof(struct fyvt), cfg->allocdata);

  vt->allocator = allocator;
  vt->allocdata = cfg->allocdata;

  vt->rows = DEFAULT(cfg->rows, 25);
  vt->cols = DEFAULT(cfg->cols, 80);

  vt->parser.state = NORMAL;

  vt->parser.callbacks = NULL;
  vt->parser.cbdata    = NULL;

  vt->parser.emit_nul  = false;

  vt->outfunc = NULL;
  vt->outdata = NULL;

  vt->outbuffer_len = DEFAULT(cfg->outbuffer_len, 4096);
  vt->outbuffer_cur = 0;
  vt->outbuffer = fyvt_allocator_malloc(vt, vt->outbuffer_len);

  vt->tmpbuffer_len = DEFAULT(cfg->tmpbuffer_len, 4096);
  vt->tmpbuffer = fyvt_allocator_malloc(vt, vt->tmpbuffer_len);

  return vt;
}

void fyvt_destroy(struct fyvt *vt)
{
  if(!vt)
    return;

  if(vt->screen)
    fyvt_screen_free(vt->screen);

  if(vt->state)
    fyvt_state_free(vt->state);

  fyvt_allocator_free(vt, vt->outbuffer);
  fyvt_allocator_free(vt, vt->tmpbuffer);

  fyvt_allocator_free(vt, vt);
}

void *fyvt_allocator_malloc(struct fyvt *vt, size_t size)
{
  return (*vt->allocator->malloc)(size, vt->allocdata);
}

void fyvt_allocator_free(struct fyvt *vt, void *ptr)
{
  (*vt->allocator->free)(ptr, vt->allocdata);
}

void fyvt_get_size(const struct fyvt *vt, int *rowsp, int *colsp)
{
  if(rowsp)
    *rowsp = vt->rows;
  if(colsp)
    *colsp = vt->cols;
}

void fyvt_set_size(struct fyvt *vt, int rows, int cols)
{
  if(rows < 1 || cols < 1)
    return;

  vt->rows = rows;
  vt->cols = cols;

  if(vt->parser.callbacks && vt->parser.callbacks->resize)
    (*vt->parser.callbacks->resize)(rows, cols, vt->parser.cbdata);
}

int fyvt_get_utf8(const struct fyvt *vt)
{
  return vt->mode.utf8;
}

void fyvt_set_utf8(struct fyvt *vt, int is_utf8)
{
  vt->mode.utf8 = is_utf8;
}

void fyvt_output_set_callback(struct fyvt *vt, fyvt_output_callback *func, void *user)
{
  vt->outfunc = func;
  vt->outdata = user;
}

void fyvt_push_output_bytes(struct fyvt *vt, const char *bytes, size_t len)
{
  if(vt->outfunc) {
    (vt->outfunc)(bytes, len, vt->outdata);
    return;
  }

  if(len > vt->outbuffer_len - vt->outbuffer_cur)
    return;

  memcpy(vt->outbuffer + vt->outbuffer_cur, bytes, len);
  vt->outbuffer_cur += len;
}

void fyvt_push_output_vsprintf(struct fyvt *vt, const char *format, va_list args)
{
  size_t len = vsnprintf(vt->tmpbuffer, vt->tmpbuffer_len,
      format, args);

  fyvt_push_output_bytes(vt, vt->tmpbuffer, len);
}

void fyvt_push_output_sprintf(struct fyvt *vt, const char *format, ...)
{
  va_list args;
  va_start(args, format);
  fyvt_push_output_vsprintf(vt, format, args);
  va_end(args);
}

void fyvt_push_output_sprintf_ctrl(struct fyvt *vt, unsigned char ctrl, const char *fmt, ...)
{
  size_t cur;

  if(ctrl >= 0x80 && !vt->mode.ctrl8bit)
    cur = snprintf(vt->tmpbuffer, vt->tmpbuffer_len,
        ESC_S "%c", ctrl - 0x40);
  else
    cur = snprintf(vt->tmpbuffer, vt->tmpbuffer_len,
        "%c", ctrl);

  if(cur >= vt->tmpbuffer_len)
    return;

  va_list args;
  va_start(args, fmt);
  cur += vsnprintf(vt->tmpbuffer + cur, vt->tmpbuffer_len - cur,
      fmt, args);
  va_end(args);

  if(cur >= vt->tmpbuffer_len)
    return;

  fyvt_push_output_bytes(vt, vt->tmpbuffer, cur);
}

void fyvt_push_output_sprintf_str(struct fyvt *vt, unsigned char ctrl, bool term, const char *fmt, ...)
{
  size_t cur = 0;

  if(ctrl) {
    if(ctrl >= 0x80 && !vt->mode.ctrl8bit)
      cur = snprintf(vt->tmpbuffer, vt->tmpbuffer_len,
          ESC_S "%c", ctrl - 0x40);
    else
      cur = snprintf(vt->tmpbuffer, vt->tmpbuffer_len,
          "%c", ctrl);

    if(cur >= vt->tmpbuffer_len)
      return;
  }

  va_list args;
  va_start(args, fmt);
  cur += vsnprintf(vt->tmpbuffer + cur, vt->tmpbuffer_len - cur,
      fmt, args);
  va_end(args);

  if(cur >= vt->tmpbuffer_len)
    return;

  if(term) {
    cur += snprintf(vt->tmpbuffer + cur, vt->tmpbuffer_len - cur,
        vt->mode.ctrl8bit ? "\x9C" : ESC_S "\\"); // ST

    if(cur >= vt->tmpbuffer_len)
      return;
  }

  fyvt_push_output_bytes(vt, vt->tmpbuffer, cur);
}

size_t fyvt_output_get_buffer_size(const struct fyvt *vt)
{
  return vt->outbuffer_len;
}

size_t fyvt_output_get_buffer_current(const struct fyvt *vt)
{
  return vt->outbuffer_cur;
}

size_t fyvt_output_get_buffer_remaining(const struct fyvt *vt)
{
  return vt->outbuffer_len - vt->outbuffer_cur;
}

size_t fyvt_output_read(struct fyvt *vt, char *buffer, size_t len)
{
  if(len > vt->outbuffer_cur)
    len = vt->outbuffer_cur;

  memcpy(buffer, vt->outbuffer, len);

  if(len < vt->outbuffer_cur)
    memmove(vt->outbuffer, vt->outbuffer + len, vt->outbuffer_cur - len);

  vt->outbuffer_cur -= len;

  return len;
}

enum fyvt_value_type fyvt_get_attr_type(enum fyvt_attr attr)
{
  switch(attr) {
    case FYVT_ATTR_BOLD:       return FYVT_VALUETYPE_BOOL;
    case FYVT_ATTR_UNDERLINE:  return FYVT_VALUETYPE_INT;
    case FYVT_ATTR_ITALIC:     return FYVT_VALUETYPE_BOOL;
    case FYVT_ATTR_BLINK:      return FYVT_VALUETYPE_BOOL;
    case FYVT_ATTR_REVERSE:    return FYVT_VALUETYPE_BOOL;
    case FYVT_ATTR_CONCEAL:    return FYVT_VALUETYPE_BOOL;
    case FYVT_ATTR_STRIKE:     return FYVT_VALUETYPE_BOOL;
    case FYVT_ATTR_FONT:       return FYVT_VALUETYPE_INT;
    case FYVT_ATTR_FOREGROUND: return FYVT_VALUETYPE_COLOR;
    case FYVT_ATTR_BACKGROUND: return FYVT_VALUETYPE_COLOR;
    case FYVT_ATTR_SMALL:      return FYVT_VALUETYPE_BOOL;
    case FYVT_ATTR_BASELINE:   return FYVT_VALUETYPE_INT;
    case FYVT_ATTR_DIM:        return FYVT_VALUETYPE_BOOL;
    case FYVT_ATTR_OVERLINE:   return FYVT_VALUETYPE_BOOL;

    case FYVT_N_ATTRS: return 0;
  }
  return 0; /* UNREACHABLE */
}

enum fyvt_value_type fyvt_get_prop_type(enum fyvt_prop prop)
{
  switch(prop) {
    case FYVT_PROP_CURSORVISIBLE: return FYVT_VALUETYPE_BOOL;
    case FYVT_PROP_CURSORBLINK:   return FYVT_VALUETYPE_BOOL;
    case FYVT_PROP_ALTSCREEN:     return FYVT_VALUETYPE_BOOL;
    case FYVT_PROP_TITLE:         return FYVT_VALUETYPE_STRING;
    case FYVT_PROP_ICONNAME:      return FYVT_VALUETYPE_STRING;
    case FYVT_PROP_REVERSE:       return FYVT_VALUETYPE_BOOL;
    case FYVT_PROP_CURSORSHAPE:   return FYVT_VALUETYPE_INT;
    case FYVT_PROP_MOUSE:         return FYVT_VALUETYPE_INT;
    case FYVT_PROP_FOCUSREPORT:   return FYVT_VALUETYPE_BOOL;
    case FYVT_PROP_THEMEUPDATES:  return FYVT_VALUETYPE_BOOL;
    case FYVT_PROP_SYNCOUTPUT:    return FYVT_VALUETYPE_BOOL;

    case FYVT_N_PROPS: return 0;
  }
  return 0; /* UNREACHABLE */
}

void fyvt_scroll_rect(struct fyvt_rect rect,
    int downward,
    int rightward,
    int (*moverect)(struct fyvt_rect src, struct fyvt_rect dest, void *user),
    int (*eraserect)(struct fyvt_rect rect, int selective, void *user),
    void *user)
{
  struct fyvt_rect src;
  struct fyvt_rect dest;

  if(abs(downward)  >= rect.end_row - rect.start_row ||
     abs(rightward) >= rect.end_col - rect.start_col) {
    /* Scroll more than area; just erase the lot */
    (*eraserect)(rect, 0, user);
    return;
  }

  if(rightward >= 0) {
    /* rect: [XXX................]
     * src:     [----------------]
     * dest: [----------------]
     */
    dest.start_col = rect.start_col;
    dest.end_col   = rect.end_col   - rightward;
    src.start_col  = rect.start_col + rightward;
    src.end_col    = rect.end_col;
  }
  else {
    /* rect: [................XXX]
     * src:  [----------------]
     * dest:    [----------------]
     */
    int leftward = -rightward;
    dest.start_col = rect.start_col + leftward;
    dest.end_col   = rect.end_col;
    src.start_col  = rect.start_col;
    src.end_col    = rect.end_col - leftward;
  }

  if(downward >= 0) {
    dest.start_row = rect.start_row;
    dest.end_row   = rect.end_row   - downward;
    src.start_row  = rect.start_row + downward;
    src.end_row    = rect.end_row;
  }
  else {
    int upward = -downward;
    dest.start_row = rect.start_row + upward;
    dest.end_row   = rect.end_row;
    src.start_row  = rect.start_row;
    src.end_row    = rect.end_row - upward;
  }

  if(moverect)
    (*moverect)(dest, src, user);

  if(downward > 0)
    rect.start_row = rect.end_row - downward;
  else if(downward < 0)
    rect.end_row = rect.start_row - downward;

  if(rightward > 0)
    rect.start_col = rect.end_col - rightward;
  else if(rightward < 0)
    rect.end_col = rect.start_col - rightward;

  (*eraserect)(rect, 0, user);
}

void fyvt_copy_cells(struct fyvt_rect dest,
    struct fyvt_rect src,
    void (*copycell)(struct fyvt_pos dest, struct fyvt_pos src, void *user),
    void *user)
{
  int downward  = src.start_row - dest.start_row;
  int rightward = src.start_col - dest.start_col;

  int init_row, test_row, init_col, test_col;
  int inc_row, inc_col;

  if(downward < 0) {
    init_row = dest.end_row - 1;
    test_row = dest.start_row - 1;
    inc_row = -1;
  }
  else /* downward >= 0 */ {
    init_row = dest.start_row;
    test_row = dest.end_row;
    inc_row = +1;
  }

  if(rightward < 0) {
    init_col = dest.end_col - 1;
    test_col = dest.start_col - 1;
    inc_col = -1;
  }
  else /* rightward >= 0 */ {
    init_col = dest.start_col;
    test_col = dest.end_col;
    inc_col = +1;
  }

  struct fyvt_pos pos;
  for(pos.row = init_row; pos.row != test_row; pos.row += inc_row)
    for(pos.col = init_col; pos.col != test_col; pos.col += inc_col) {
      struct fyvt_pos srcpos = { pos.row + downward, pos.col + rightward };
      (*copycell)(pos, srcpos, user);
    }
}

const char *fyvt_version_string(void)
{
#ifdef FYVT_VERSION_STRING
  return FYVT_VERSION_STRING;
#else
  return "unknown";
#endif
}

void fyvt_check_version(int major, int minor)
{
  if(major != FYVT_VERSION_MAJOR) {
    fprintf(stderr, "libfyvterm major version mismatch; %d (wants) != %d (library)\n",
        major, FYVT_VERSION_MAJOR);
    exit(1);
  }

  if(minor > FYVT_VERSION_MINOR) {
    fprintf(stderr, "libfyvterm minor version mismatch; %d (wants) > %d (library)\n",
        minor, FYVT_VERSION_MINOR);
    exit(1);
  }

  // Happy
}
