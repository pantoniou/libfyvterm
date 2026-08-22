/*
 * run-test.c - driver for the .test scripts under t/
 *
 * Replaces the original Perl driver. A .test file interleaves commands for
 * the harness with the callback events they are expected to produce:
 *
 *     !Basic text            a named section
 *     PUSH "hello"           a command (capitalised)
 *       text 0x68, 0x65      an expectation (lowercase), indented
 *     ?cursor = 0,5          an assertion, checked immediately
 *
 * Expectations accumulate until the next command, then are compared in
 * order against what that command actually produced.
 *
 * Both the driver and the harness live in this one binary, so there are no
 * pipes and no second language in the build.
 *
 * SPDX-License-Identifier: MIT
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"

#define streq(a,b) (!strcmp(a,b))

static const char *test_path;
static int exitcode;
static int linenum;

/* ------------------------------------------------------------------ */
/* Growable string                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
  char *buf;
  size_t len, alloc;
} str_t;

static void str_init(str_t *s)
{
  s->buf = NULL;
  s->len = s->alloc = 0;
}

static void str_free(str_t *s)
{
  free(s->buf);
  str_init(s);
}

static void str_addn(str_t *s, const char *p, size_t n)
{
  if(s->len + n + 1 > s->alloc) {
    while(s->len + n + 1 > s->alloc)
      s->alloc = s->alloc ? s->alloc * 2 : 64;
    s->buf = realloc(s->buf, s->alloc);
    if(!s->buf)
      abort();
  }
  memcpy(s->buf + s->len, p, n);
  s->len += n;
  s->buf[s->len] = '\0';
}

static void str_addc(str_t *s, char c)
{
  str_addn(s, &c, 1);
}

static void str_addf(str_t *s, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(NULL, 0, fmt, args);
  va_end(args);
  if(n < 0)
    return;
  char *tmp = malloc((size_t)n + 1);
  if(!tmp)
    abort();
  va_start(args, fmt);
  vsnprintf(tmp, (size_t)n + 1, fmt, args);
  va_end(args);
  str_addn(s, tmp, (size_t)n);
  free(tmp);
}

/* ------------------------------------------------------------------ */
/* String literal parsing                                              */
/*                                                                     */
/* The test scripts write byte strings as double-quoted literals using  */
/* the escapes the original Perl driver understood: \e \n \r \t \b \a   */
/* \0 \\ \" \xHH and \x{HH...}. Every codepoint written this way in the */
/* current tests is a single byte, so \x{...} is treated as one byte    */
/* too, exactly as Perl's unpack "C*" did.                             */
/* ------------------------------------------------------------------ */

static bool parse_quoted(const char *p, str_t *out, const char **endp)
{
  if(*p != '"')
    return false;
  p++;

  while(*p && *p != '"') {
    if(*p != '\\') {
      str_addc(out, *p++);
      continue;
    }
    p++;
    switch(*p) {
    case 'e':  str_addc(out, 0x1b); p++; break;
    case 'n':  str_addc(out, '\n'); p++; break;
    case 'r':  str_addc(out, '\r'); p++; break;
    case 't':  str_addc(out, '\t'); p++; break;
    case 'b':  str_addc(out, '\b'); p++; break;
    case 'a':  str_addc(out, '\a'); p++; break;
    case '0':  str_addc(out, '\0'); p++; break;
    case '\\': str_addc(out, '\\'); p++; break;
    case '"':  str_addc(out, '"');  p++; break;
    case 'x': {
      p++;
      unsigned long v = 0;
      if(*p == '{') {
        p++;
        v = strtoul(p, (char **)&p, 16);
        if(*p == '}')
          p++;
      }
      else {
        char hex[3] = {0};
        int i = 0;
        while(i < 2 && isxdigit((unsigned char)*p))
          hex[i++] = *p++;
        v = strtoul(hex, NULL, 16);
      }
      str_addc(out, (char)(v & 0xff));
      break;
    }
    default:
      str_addc(out, *p ? *p++ : '\0');
      break;
    }
  }

  if(*p != '"')
    return false;
  p++;
  if(endp)
    *endp = p;
  return true;
}

/*
 * A string expression is one or more quoted strings joined by Perl's '.'
 * concatenation operator, each optionally repeated with 'x <count>':
 *
 *     "e" . "\xCC\x81" x 10
 *     "\n"x24
 *     "\e]52;c;" . "LS0t"x10 . "\e\\"
 */
static bool parse_string_expr(const char *p, str_t *out, const char **endp)
{
  for(;;) {
    while(*p == ' ')
      p++;

    str_t term;
    str_init(&term);
    if(!parse_quoted(p, &term, &p)) {
      str_free(&term);
      return false;
    }

    while(*p == ' ')
      p++;

    /* optional repetition: x <count> */
    unsigned long reps = 1;
    if(*p == 'x' && (isdigit((unsigned char)p[1]) || p[1] == ' ')) {
      p++;
      while(*p == ' ')
        p++;
      char *end;
      reps = strtoul(p, &end, 10);
      p = end;
    }

    for(unsigned long i = 0; i < reps; i++)
      str_addn(out, term.buf ? term.buf : "", term.len);
    str_free(&term);

    while(*p == ' ')
      p++;

    if(*p != '.')
      break;
    p++;
  }

  if(endp)
    *endp = p;
  return true;
}

/*
 * A value in a test script is either a quoted string or a comma-separated
 * list of numbers. Returns the bytes of the string, or the numbers, in
 * *out / *nums.
 */
static size_t parse_numbers(const char *p, unsigned long *nums, size_t max)
{
  size_t n = 0;

  while(*p && n < max) {
    while(*p == ' ' || *p == ',')
      p++;
    if(!*p)
      break;
    char *end;
    unsigned long v = strtoul(p, &end, 0);
    if(end == p)
      break;
    nums[n++] = v;
    p = end;
  }

  return n;
}

/* ------------------------------------------------------------------ */
/* Expectation list                                                    */
/* ------------------------------------------------------------------ */

static char **expect;
static size_t expect_count, expect_alloc;

static void expect_add(const char *line)
{
  if(expect_count == expect_alloc) {
    expect_alloc = expect_alloc ? expect_alloc * 2 : 16;
    expect = realloc(expect, expect_alloc * sizeof(*expect));
    if(!expect)
      abort();
  }
  expect[expect_count++] = strdup(line);
}

static void expect_clear(void)
{
  for(size_t i = 0; i < expect_count; i++)
    free(expect[i]);
  expect_count = 0;
}

static char *pending_command;

/* ------------------------------------------------------------------ */
/* Running one command and checking its output                         */
/* ------------------------------------------------------------------ */

static void do_onetest(void)
{
  if(!pending_command)
    return;

  harness_output_reset();
  harness_command(pending_command);
  free(pending_command);
  pending_command = NULL;

  int fail_printed = 0;
  size_t got = harness_output_count();

  for(size_t i = 0; i < got; i++) {
    const char *outline = harness_output_line(i);

    if(i >= expect_count) {
      if(!fail_printed++)
        printf("# line %d: Test failed\n", linenum);
      printf("#    expected nothing more\n#   Actual:   %s\n", outline);
      continue;
    }

    if(streq(expect[i], outline))
      continue;

    if(!fail_printed++)
      printf("# line %d: Test failed\n", linenum);
    printf("#   Expected: %s\n#   Actual:   %s\n", expect[i], outline);
  }

  for(size_t i = got; i < expect_count; i++) {
    if(!fail_printed++)
      printf("# line %d: Test failed\n", linenum);
    printf("#   Expected: %s\n#    didn't happen\n", expect[i]);
  }

  if(fail_printed)
    exitcode = 1;

  expect_clear();
}

/*
 * Ask the harness a question and return its single-line reply. The reply
 * is owned by the harness output buffer and is valid until the next
 * command.
 */
static const char *query(const char *fmt, ...)
{
  char cmd[1024];
  va_list args;

  va_start(args, fmt);
  vsnprintf(cmd, sizeof cmd, fmt, args);
  va_end(args);

  harness_output_reset();
  harness_command(cmd);

  return harness_output_count() ? harness_output_line(0) : "";
}

static void trim(char *s)
{
  size_t n = strlen(s);
  while(n && isspace((unsigned char)s[n-1]))
    s[--n] = '\0';
}

/* ------------------------------------------------------------------ */
/* Line handling                                                       */
/* ------------------------------------------------------------------ */

static void do_line(const char *line);

/*
 * Rewrite the convenience notation a test script uses into the canonical
 * form the harness prints, so the two can be compared as plain strings.
 */
static bool convert_expectation(const char *line, str_t *out)
{
  const char *sp = strchr(line, ' ');
  size_t verblen = sp ? (size_t)(sp - line) : strlen(line);
  const char *arg = sp ? sp + 1 : "";
  char verb[32];

  if(verblen >= sizeof verb)
    return false;
  memcpy(verb, line, verblen);
  verb[verblen] = '\0';

  /* text/encout: a list of codepoints, printed as bare hex */
  if(streq(verb, "text") || streq(verb, "encout")) {
    unsigned long nums[256];
    size_t n = parse_numbers(arg, nums, 256);
    str_addf(out, "%s", verb);
    for(size_t i = 0; i < n; i++)
      str_addf(out, "%s%lx", i ? "," : " ", nums[i]);
    return true;
  }

  /* output: a byte string, printed as bare hex bytes */
  if(streq(verb, "output")) {
    str_t s;
    str_init(&s);
    if(!parse_string_expr(arg, &s, NULL)) {
      str_free(&s);
      return false;
    }
    str_addf(out, "output");
    for(size_t i = 0; i < s.len; i++)
      str_addf(out, "%s%x", i ? "," : " ", (unsigned char)s.buf[i]);
    str_free(&s);
    return true;
  }

  /* control: a single byte, printed as two hex digits */
  if(streq(verb, "control")) {
    unsigned long v = strtoul(arg, NULL, 0);
    str_addf(out, "control %02lx", v);
    return true;
  }

  /* csi: leading byte in hex, remainder verbatim */
  if(streq(verb, "csi")) {
    char *end;
    unsigned long v = strtoul(arg, &end, 0);
    while(*end == ' ')
      end++;
    str_addf(out, "csi %02lx %s", v, end);
    return true;
  }

  /*
   * osc: an optional "[N" command prefix, then a string, then an optional
   * closing "]". The string becomes a run of two-digit hex bytes.
   */
  if(streq(verb, "osc")) {
    const char *p = arg;
    str_addf(out, "osc ");

    if(*p == '[') {
      const char *q = p + 1;
      while(isdigit((unsigned char)*q))
        q++;
      str_addn(out, p, (size_t)(q - p));
      if(q > p + 1)
        str_addc(out, ';');
      p = q;
      while(*p == ' ')
        p++;
    }

    str_t s;
    str_init(&s);
    if(*p == '"') {
      if(!parse_string_expr(p, &s, &p)) {
        str_free(&s);
        return false;
      }
    }
    for(size_t i = 0; i < s.len; i++)
      str_addf(out, "%02x", (unsigned char)s.buf[i]);
    str_free(&s);

    while(*p == ' ')
      p++;
    if(*p == ']')
      str_addc(out, ']');
    return true;
  }

  /* escape/dcs/apc/pm/sos: optional "[", a string, optional "]" */
  if(streq(verb, "escape") || streq(verb, "dcs") || streq(verb, "apc") ||
     streq(verb, "pm") || streq(verb, "sos")) {
    const char *p = arg;
    str_addf(out, "%s ", verb);

    if(*p == '[') {
      str_addc(out, '[');
      p++;
    }

    str_t s;
    str_init(&s);
    if(*p == '"') {
      if(!parse_string_expr(p, &s, &p)) {
        str_free(&s);
        return false;
      }
    }
    for(size_t i = 0; i < s.len; i++)
      str_addf(out, "%02x", (unsigned char)s.buf[i]);
    str_free(&s);

    while(*p == ' ')
      p++;
    if(*p == ']')
      str_addc(out, ']');
    return true;
  }

  /* putglyph: a codepoint list, then width and position verbatim */
  if(streq(verb, "putglyph")) {
    const char *sp2 = strchr(arg, ' ');
    if(!sp2)
      return false;
    char cps[128];
    size_t n = (size_t)(sp2 - arg);
    if(n >= sizeof cps)
      return false;
    memcpy(cps, arg, n);
    cps[n] = '\0';

    unsigned long nums[16];
    size_t cnt = parse_numbers(cps, nums, 16);
    str_addf(out, "putglyph ");
    for(size_t i = 0; i < cnt; i++)
      str_addf(out, "%s%lx", i ? "," : "", nums[i]);
    str_addf(out, " %s", sp2 + 1);
    return true;
  }

  /* selection-set: mask verbatim, then a bracketed string as hex */
  if(streq(verb, "selection-set")) {
    const char *p = arg;
    const char *sp2 = strchr(p, ' ');
    if(!sp2)
      return false;
    str_addf(out, "selection-set ");
    str_addn(out, p, (size_t)(sp2 - p));
    str_addc(out, ' ');
    p = sp2 + 1;

    if(*p == '[') {
      str_addc(out, '[');
      p++;
    }

    str_t s;
    str_init(&s);
    if(*p == '"') {
      if(!parse_string_expr(p, &s, &p)) {
        str_free(&s);
        return false;
      }
    }
    for(size_t i = 0; i < s.len; i++)
      str_addf(out, "%02x", (unsigned char)s.buf[i]);
    str_free(&s);

    while(*p == ' ')
      p++;
    if(*p == ']')
      str_addc(out, ']');
    return true;
  }

  /* Everything else is compared exactly as written. */
  if(streq(verb, "movecursor") || streq(verb, "scrollrect") ||
     streq(verb, "premove") || streq(verb, "moverect") ||
     streq(verb, "erase") || streq(verb, "damage") ||
     streq(verb, "sb_pushline") || streq(verb, "sb_popline") ||
     streq(verb, "sb_clear") || streq(verb, "settermprop") ||
     streq(verb, "setmousefunc") || streq(verb, "selection-query") ||
     streq(verb, "bell") || streq(verb, "resize") ||
     streq(verb, "setlineinfo") || streq(verb, "initpen") ||
     streq(verb, "putglyph") || streq(verb, "theme")) {
    str_addf(out, "%s", line);
    return true;
  }

  str_addf(out, "%s", line);
  return true;
}

/*
 * Commands taking a byte-string argument are sent to the harness as hex,
 * which is what its parser expects.
 */
static bool convert_command(const char *line, str_t *out)
{
  if(!strncmp(line, "PUSH ", 5) || !strncmp(line, "ENCIN ", 6)) {
    const char *sp = strchr(line, ' ');
    const char *p = sp + 1;
    while(*p == ' ')
      p++;

    str_t s;
    str_init(&s);
    if(!parse_string_expr(p, &s, NULL)) {
      str_free(&s);
      return false;
    }

    str_addn(out, line, (size_t)(sp - line));
    str_addc(out, ' ');
    for(size_t i = 0; i < s.len; i++)
      str_addf(out, "%02x", (unsigned char)s.buf[i]);
    str_free(&s);
    return true;
  }

  if(!strncmp(line, "SELECTION ", 10)) {
    /* SELECTION <mask> [<string>] -- the string becomes hex */
    const char *p = line + 10;
    const char *q = p;
    while(*q && *q != ' ')
      q++;

    str_addf(out, "SELECTION ");
    str_addn(out, p, (size_t)(q - p));
    str_addc(out, ' ');
    while(*q == ' ')
      q++;

    bool open_bracket = false;
    if(*q == '[') {
      open_bracket = true;
      q++;
    }
    str_addc(out, open_bracket ? '[' : ' ');

    str_t s;
    str_init(&s);
    if(*q == '"') {
      if(!parse_string_expr(q, &s, &q)) {
        str_free(&s);
        return false;
      }
    }
    for(size_t i = 0; i < s.len; i++)
      str_addf(out, "%02x", (unsigned char)s.buf[i]);
    str_free(&s);

    while(*q == ' ')
      q++;
    if(*q == ']')
      str_addf(out, " ]");
    return true;
  }

  str_addf(out, "%s", line);
  return true;
}

/* ?screen_row N = "..." or = 0xNN,0xNN,... */
static void do_screen_row(const char *line)
{
  const char *p = line;
  while(*p == ' ')
    p++;
  char *end;
  unsigned long row = strtoul(p, &end, 10);
  p = end;
  while(*p == ' ' || *p == '=')
    p++;

  do_onetest();

  const char *reply = query("?screen_chars %lu", row);

  /* Both sides are compared as codepoint lists. */
  unsigned long want[512], got[512];
  size_t nwant, ngot;

  if(*p == '"') {
    str_t s;
    str_init(&s);
    parse_string_expr(p, &s, NULL);
    nwant = s.len < 512 ? s.len : 512;
    for(size_t i = 0; i < nwant; i++)
      want[i] = (unsigned char)s.buf[i];
    str_free(&s);
  }
  else {
    nwant = parse_numbers(p, want, 512);
  }

  ngot = parse_numbers(reply, got, 512);

  bool same = nwant == ngot;
  for(size_t i = 0; same && i < nwant; i++)
    if(want[i] != got[i])
      same = false;

  if(!same) {
    printf("# line %d: Assert ?screen_row %lu failed:\n", linenum, row);
    printf("# Expected:");
    for(size_t i = 0; i < nwant; i++)
      printf(" 0x%02lx", want[i]);
    printf("\n# Actual:  ");
    for(size_t i = 0; i < ngot; i++)
      printf(" 0x%02lx", got[i]);
    printf("\n");
    exitcode = 1;
  }
}

/* ?<assertion> = <expectation> */
static void do_assertion(const char *line)
{
  const char *eq = strchr(line, '=');
  if(!eq) {
    fprintf(stderr, "%s:%d: malformed assertion '%s'\n", test_path, linenum, line);
    exit(125);
  }

  char assertion[512];
  size_t n = (size_t)(eq - line);
  while(n && isspace((unsigned char)line[n-1]))
    n--;
  if(n >= sizeof assertion)
    n = sizeof assertion - 1;
  memcpy(assertion, line, n);
  assertion[n] = '\0';

  const char *exp = eq + 1;
  while(*exp == ' ')
    exp++;

  do_onetest();

  char reply[4096];
  snprintf(reply, sizeof reply, "%s", query("?%s", assertion));
  trim(reply);

  /* ?screen_chars written as a string is compared as a byte list. */
  str_t want;
  str_init(&want);
  if(!strncmp(assertion, "screen_chars", 12) && *exp == '"') {
    str_t s;
    str_init(&s);
    parse_string_expr(exp, &s, NULL);
    for(size_t i = 0; i < s.len; i++)
      str_addf(&want, "%s0x%02x", i ? "," : "", (unsigned char)s.buf[i]);
    str_free(&s);
  }
  else {
    str_addf(&want, "%s", exp);
  }

  if(!streq(reply, want.buf ? want.buf : "")) {
    printf("# line %d: Assert %s failed:\n", linenum, assertion);
    printf("# Expected: %s\n", want.buf ? want.buf : "");
    printf("# Actual:   %s\n", reply);
    exitcode = 1;
  }

  str_free(&want);
}

static void do_line(const char *line)
{
  /* Section heading */
  if(line[0] == '!') {
    do_onetest();
    printf("> %s\n", line + 1);
    return;
  }

  /* $SEQ low high: <line with \# substituted> */
  if(!strncmp(line, "$SEQ ", 5)) {
    const char *p = line + 5;
    char *end;
    unsigned long low = strtoul(p, &end, 10);
    unsigned long high = strtoul(end, &end, 10);
    while(*end == ':' || *end == ' ')
      end++;

    for(unsigned long v = low; v <= high; v++) {
      str_t s;
      str_init(&s);
      for(const char *q = end; *q; ) {
        if(q[0] == '\\' && q[1] == '#') {
          str_addf(&s, "%lu", v);
          q += 2;
        }
        else
          str_addc(&s, *q++);
      }
      do_line(s.buf ? s.buf : "");
      str_free(&s);
    }
    return;
  }

  /* $REP count: <line> */
  if(!strncmp(line, "$REP ", 5)) {
    const char *p = line + 5;
    char *end;
    unsigned long count = strtoul(p, &end, 10);
    while(*end == ':' || *end == ' ')
      end++;
    for(unsigned long i = 0; i < count; i++)
      do_line(end);
    return;
  }

  /* Assertions */
  if(line[0] == '?') {
    if(!strncmp(line + 1, "screen_row ", 11))
      do_screen_row(line + 12);
    else
      do_assertion(line + 1);
    return;
  }

  /* Commands are capitalised */
  if(isupper((unsigned char)line[0])) {
    do_onetest();

    str_t s;
    str_init(&s);
    if(!convert_command(line, &s)) {
      fprintf(stderr, "%s:%d: bad command '%s'\n", test_path, linenum, line);
      exit(125);
    }
    pending_command = s.buf ? s.buf : strdup("");
    expect_clear();
    return;
  }

  /* Expectations are lowercase */
  if(islower((unsigned char)line[0])) {
    str_t s;
    str_init(&s);
    if(!convert_expectation(line, &s)) {
      fprintf(stderr, "%s:%d: bad expectation '%s'\n", test_path, linenum, line);
      exit(125);
    }
    expect_add(s.buf ? s.buf : "");
    str_free(&s);
    return;
  }

  fprintf(stderr, "%s:%d: unrecognised line '%s'\n", test_path, linenum, line);
  exit(125);
}

int main(int argc, char **argv)
{
  if(argc < 2) {
    fprintf(stderr, "usage: %s <test-file>\n", argv[0]);
    return 2;
  }

  test_path = argv[1];

  FILE *f = fopen(test_path, "r");
  if(!f) {
    fprintf(stderr, "Cannot open test script %s\n", test_path);
    return 2;
  }

  char line[4096];
  while(fgets(line, sizeof line, f)) {
    linenum++;

    /* strip leading whitespace and the trailing newline */
    char *p = line;
    while(*p == ' ' || *p == '\t')
      p++;
    size_t n = strlen(p);
    while(n && (p[n-1] == '\n' || p[n-1] == '\r'))
      p[--n] = '\0';

    if(!*p || *p == '#')
      continue;
    if(streq(p, "__END__"))
      break;

    do_line(p);
  }

  do_onetest();

  fclose(f);
  harness_cleanup();

  return exitcode;
}
