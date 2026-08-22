/*
 * libfyvterm-parser.h - the parser layer: raw escape-sequence callbacks
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


#ifndef LIBFYVTERM_PARSER_H
#define LIBFYVTERM_PARSER_H

#include <libfyvterm/libfyvterm-util.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Flag to indicate non-final subparameters in a single CSI parameter.
 * Consider
 *   CSI 1;2:3:4;5a
 * 1 4 and 5 are final.
 * 2 and 3 are non-final and will have this bit set
 *
 * Don't confuse this with the final byte of the CSI escape; 'a' in this case.
 */
#define CSI_ARG_FLAG_MORE (1U<<31)
#define CSI_ARG_MASK      (~(1U<<31))

#define CSI_ARG_HAS_MORE(a) ((a) & CSI_ARG_FLAG_MORE)
#define CSI_ARG(a)          ((a) & CSI_ARG_MASK)

/* Can't use -1 to indicate a missing argument; use this instead */
#define CSI_ARG_MISSING ((1UL<<31)-1)

#define CSI_ARG_IS_MISSING(a) (CSI_ARG(a) == CSI_ARG_MISSING)
#define CSI_ARG_OR(a,def)     (CSI_ARG(a) == CSI_ARG_MISSING ? (def) : CSI_ARG(a))
#define CSI_ARG_COUNT(a)      (CSI_ARG(a) == CSI_ARG_MISSING || CSI_ARG(a) == 0 ? 1 : CSI_ARG(a))

struct fyvt_parser_callbacks {
  int (*text)(const char *bytes, size_t len, void *user);
  int (*control)(unsigned char control, void *user);
  int (*escape)(const char *bytes, size_t len, void *user);
  int (*csi)(const char *leader, const long args[], int argcount, const char *intermed, char command, void *user);
  int (*osc)(int command, struct fyvt_string_fragment frag, void *user);
  int (*dcs)(const char *command, size_t commandlen, struct fyvt_string_fragment frag, void *user);
  int (*apc)(struct fyvt_string_fragment frag, void *user);
  int (*pm)(struct fyvt_string_fragment frag, void *user);
  int (*sos)(struct fyvt_string_fragment frag, void *user);
  int (*resize)(int rows, int cols, void *user);
};

void  fyvt_parser_set_callbacks(struct fyvt *vt, const struct fyvt_parser_callbacks *callbacks, void *user) FYVT_EXPORT;
void *fyvt_parser_get_cbdata(struct fyvt *vt) FYVT_EXPORT;

/* Normally NUL, CAN, SUB and DEL are ignored. Setting this true causes them
 * to be emitted by the 'control' callback
 */
void fyvt_parser_set_emit_nul(struct fyvt *vt, bool emit) FYVT_EXPORT;

#ifdef __cplusplus
}
#endif

#endif /* LIBFYVTERM_PARSER_H */
