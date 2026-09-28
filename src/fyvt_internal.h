#ifndef LIBFYVTERM_INTERNAL_H
#define LIBFYVTERM_INTERNAL_H

#include <libfyvterm.h>

#include <stdarg.h>

/* Mark a parameter a callback signature requires but the implementation does
 * not read. An attribute rather than a "(void)x;" statement, so it stays on
 * the declaration and out of the function body. */
#if defined(__GNUC__)
# define UNUSED __attribute__((unused))
#else
# define UNUSED
#endif

/* Mark a deliberate fall-through between switch cases. The compiler only
 * honours a "fallthrough" comment placed immediately before the case label,
 * which is not where these switches explain themselves. */
#if defined(__GNUC__)
# define FALLTHROUGH __attribute__((fallthrough))
#else
# define FALLTHROUGH ((void)0)
#endif

#ifdef DEBUG
# define DEBUG_LOG(...) fprintf(stderr, __VA_ARGS__)
#else
# define DEBUG_LOG(...) do { } while(0)
#endif

/*
 * The identity this terminal reports to XTVERSION (CSI > q).
 *
 * Deliberately still "libvterm(0.3)" and deliberately NOT the libfyvterm
 * version: applications sniff this string to decide what the terminal can
 * do, and every one of them that knows this library knows it by that name.
 * Renumbering it with the fork's own version would tell them a libvterm
 * 0.0 exists, which is worse than saying nothing new.
 */
#define FYVT_XTVERSION_NAME  "libvterm"
#define FYVT_XTVERSION_MAJOR 0
#define FYVT_XTVERSION_MINOR 3

#define ESC_S "\x1b"

#define INTERMED_MAX 16

/* Neovim carries a stack overflow fix here (upstream commit 756751afa3,
 * "fix(terminal): stack overflow when too many csi args"): a CSI with more
 * than 16 subparameters (e.g. piped-back scrollback with dense SGR runs)
 * overflowed the args[] buffer. */
#define CSI_ARGS_MAX 32
#define CSI_LEADER_MAX 16

#define BUFIDX_PRIMARY   0
#define BUFIDX_ALTSCREEN 1

/* Kitty keyboard protocol flags, carried from Neovim's fork (upstream
 * commit 6f0bde11cc, "feat(terminal): add support for kitty keyboard
 * protocol"). https://sw.kovidgoyal.net/kitty/keyboard-protocol/#progressive-enhancement */
#define KEY_ENCODING_DISAMBIGUATE       0x1
#define KEY_ENCODING_REPORT_EVENTS      0x2
#define KEY_ENCODING_REPORT_ALTERNATE   0x4
#define KEY_ENCODING_REPORT_ALL_KEYS    0x8
#define KEY_ENCODING_REPORT_ASSOCIATED  0x10

struct fyvt_encoding;

struct fyvt_encoding_instance {
  struct fyvt_encoding *enc;

  // This size should be increased if required by other stateful encodings
  char           data[4*sizeof(uint32_t)];
};

struct fyvt_pen
{
  union fyvt_color fg;
  union fyvt_color bg;
  unsigned int bold:1;
  unsigned int underline:2;
  unsigned int italic:1;
  unsigned int blink:1;
  unsigned int reverse:1;
  unsigned int conceal:1;
  unsigned int strike:1;
  unsigned int font:4; /* To store 0-9 */
  unsigned int small:1;
  unsigned int baseline:2;
  unsigned int dim:1;
  unsigned int overline:1;
};

struct fyvt_key_encoding_flags {
  bool disambiguate:1;
  bool report_events:1;
  bool report_alternate:1;
  bool report_all_keys:1;
  bool report_associated:1;
};

struct fyvt_key_encoding_stack {
  struct fyvt_key_encoding_flags items[16];
  uint8_t size; /* Number of items in the stack: at least 1, at most
                 * ARRAY_SIZE(items). */
};

struct fyvt_state
{
  struct fyvt *vt;

  const struct fyvt_state_callbacks *callbacks;
  void *cbdata;
  bool callbacks_has_premove;

  const struct fyvt_state_fallbacks *fallbacks;
  void *fbdata;

  int rows;
  int cols;

  /* Current cursor position */
  struct fyvt_pos pos;

  int at_phantom; /* True if we're on the "81st" phantom column to defer a wraparound */

  int scrollregion_top;
  int scrollregion_bottom; /* -1 means unbounded */
#define SCROLLREGION_BOTTOM(state) ((state)->scrollregion_bottom > -1 ? (state)->scrollregion_bottom : (state)->rows)
  int scrollregion_left;
#define SCROLLREGION_LEFT(state)  ((state)->mode.leftrightmargin ? (state)->scrollregion_left : 0)
  int scrollregion_right; /* -1 means unbounded */
#define SCROLLREGION_RIGHT(state) ((state)->mode.leftrightmargin && (state)->scrollregion_right > -1 ? (state)->scrollregion_right : (state)->cols)

  /* Bitvector of tab stops */
  unsigned char *tabstops;

  /* Primary and Altscreen; lineinfos[1] is lazily allocated as needed */
  struct fyvt_line_info *lineinfos[2];

  /* lineinfo will == lineinfos[0] or lineinfos[1], depending on altscreen */
  struct fyvt_line_info *lineinfo;
#define ROWWIDTH(state,row) ((state)->lineinfo[(row)].doublewidth ? ((state)->cols / 2) : (state)->cols)
#define THISROWWIDTH(state) ROWWIDTH(state, (state)->pos.row)

  /* Mouse state */
  int mouse_col, mouse_row;
  int mouse_buttons;
  int mouse_flags;
#define MOUSE_WANT_CLICK 0x01
#define MOUSE_WANT_DRAG  0x02
#define MOUSE_WANT_MOVE  0x04

  enum { MOUSE_X10, MOUSE_UTF8, MOUSE_SGR, MOUSE_RXVT } mouse_protocol;

  /* Last glyph output, for Unicode recombining purposes */
  uint32_t *combine_chars;
  size_t combine_chars_size; // Number of ELEMENTS in the above
  int combine_width; // The width of the glyph above
  struct fyvt_pos combine_pos;   // Position before movement

  struct {
    unsigned int keypad:1;
    unsigned int cursor:1;
    unsigned int autowrap:1;
    unsigned int insert:1;
    unsigned int newline:1;
    unsigned int cursor_visible:1;
    unsigned int cursor_blink:1;
    unsigned int cursor_shape:2;
    unsigned int alt_screen:1;
    unsigned int origin:1;
    unsigned int screen:1;
    unsigned int leftrightmargin:1;
    unsigned int bracketpaste:1;
    unsigned int report_focus:1;
    unsigned int theme_updates:1;
    unsigned int synchronized_output:1;
  } mode;

  struct fyvt_encoding_instance encoding[4], encoding_utf8;
  int gl_set, gr_set, gsingle_set;

  struct fyvt_pen pen;

  union fyvt_color default_fg;
  union fyvt_color default_bg;
  union fyvt_color colors[16]; // Store the 8 ANSI and the 8 ANSI high-brights only

  int bold_is_highbright;

  unsigned int protected_cell : 1;

  /* Saved state under DEC mode 1048/1049 */
  struct {
    struct fyvt_pos pos;
    struct fyvt_pen pen;

    struct {
      unsigned int cursor_visible:1;
      unsigned int cursor_blink:1;
      unsigned int cursor_shape:2;
    } mode;
  } saved;

  /* Temporary state for DECRQSS parsing */
  union {
    char decrqss[4];
    struct {
      uint16_t mask;
      enum {
        SELECTION_INITIAL,
        SELECTION_SELECTED,
        SELECTION_QUERY,
        SELECTION_SET_INITIAL,
        SELECTION_SET,
        SELECTION_INVALID,
      } state : 8;
      uint32_t recvpartial;
      uint32_t sendpartial;
    } selection;
  } tmp;

  struct {
    const struct fyvt_selection_callbacks *callbacks;
    void *user;
    char *buffer;
    size_t buflen;
    /* True when the buffer above was allocated here rather than handed in
     * by the caller, and so is ours to free. */
    bool buffer_owned;
  } selection;

  /* Kitty keyboard protocol flag stack: one for the primary screen, one for
   * the altscreen. */
  struct fyvt_key_encoding_stack key_encoding_stacks[2];
};

struct fyvt
{
  const struct fyvt_allocator *allocator;
  void *allocdata;

  int rows;
  int cols;

  struct {
    unsigned int utf8:1;
    /* U+FE0F leaves an emoji base its own width, as VTE draws it */
    unsigned int emoji_vs_narrow:1;
    unsigned int ctrl8bit:1;
  } mode;

  struct {
    enum fyvt_parser_state {
      NORMAL,
      CSI_LEADER,
      CSI_ARGS,
      CSI_INTERMED,
      DCS_COMMAND,
      /* below here are the "string states" */
      OSC_COMMAND,
      OSC,
      DCS,
      APC,
      PM,
      SOS,
    } state;

    bool in_esc : 1;

    int intermedlen;
    char intermed[INTERMED_MAX];

    union {
      struct {
        int leaderlen;
        char leader[CSI_LEADER_MAX];

        int argi;
        long args[CSI_ARGS_MAX];
      } csi;
      struct {
        int command;
      } osc;
      struct {
        int commandlen;
        char command[CSI_LEADER_MAX];
      } dcs;
    } v;

    const struct fyvt_parser_callbacks *callbacks;
    void *cbdata;

    bool string_initial;

    bool emit_nul;
  } parser;

  /* len == malloc()ed size; cur == number of valid bytes */

  fyvt_output_callback *outfunc;
  void                *outdata;

  char  *outbuffer;
  size_t outbuffer_len;
  size_t outbuffer_cur;

  char  *tmpbuffer;
  size_t tmpbuffer_len;

  struct fyvt_state *state;
  struct fyvt_screen *screen;
};

struct fyvt_encoding {
  void (*init) (struct fyvt_encoding *enc, void *data);
  void (*decode)(struct fyvt_encoding *enc, void *data,
                 uint32_t cp[], int *cpi, int cplen,
                 const char bytes[], size_t *pos, size_t len);
};

enum fyvt_encoding_type {
  ENC_UTF8,
  ENC_SINGLE_94
};

void *fyvt_allocator_malloc(struct fyvt *vt, size_t size);
void  fyvt_allocator_free(struct fyvt *vt, void *ptr);

void fyvt_push_output_bytes(struct fyvt *vt, const char *bytes, size_t len);
void fyvt_push_output_vsprintf(struct fyvt *vt, const char *format, va_list args);
void fyvt_push_output_sprintf(struct fyvt *vt, const char *format, ...);
void fyvt_push_output_sprintf_ctrl(struct fyvt *vt, unsigned char ctrl, const char *fmt, ...);
void fyvt_push_output_sprintf_str(struct fyvt *vt, unsigned char ctrl, bool term, const char *fmt, ...);

void fyvt_state_free(struct fyvt_state *state);

void fyvt_state_newpen(struct fyvt_state *state);
void fyvt_state_resetpen(struct fyvt_state *state);
void fyvt_state_setpen(struct fyvt_state *state, const long args[], int argcount);
int  fyvt_state_getpen(struct fyvt_state *state, long args[], int argcount);
void fyvt_state_savepen(struct fyvt_state *state, int save);

enum {
  C1_SS3 = 0x8f,
  C1_DCS = 0x90,
  C1_CSI = 0x9b,
  C1_ST  = 0x9c,
  C1_OSC = 0x9d,
};

void fyvt_state_push_output_sprintf_CSI(struct fyvt_state *vts, const char *format, ...);

void fyvt_screen_free(struct fyvt_screen *screen);

struct fyvt_encoding *fyvt_lookup_encoding(enum fyvt_encoding_type type, char designation);

int fyvt_unicode_width(uint32_t codepoint);
int fyvt_unicode_is_combining(uint32_t codepoint);
int fyvt_unicode_emoji_vs(uint32_t codepoint);
/* The columns of a glyph: @chars is its base and its combining codepoints,
 * zero-terminated, and @width is their sum, codepoint by codepoint. */
int fyvt_unicode_glyph_width(const uint32_t *chars, int width);

#endif
