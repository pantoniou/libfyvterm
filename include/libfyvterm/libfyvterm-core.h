/*
 * libfyvterm-core.h - the terminal instance: lifecycle, size and byte streams
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


#ifndef LIBFYVTERM_CORE_H
#define LIBFYVTERM_CORE_H

#include <libfyvterm/libfyvterm-util.h>

#ifdef __cplusplus
extern "C" {
#endif

void fyvt_check_version(int major, int minor) FYVT_EXPORT;

struct fyvt_cfg {
  int ver; /* currently unused but reserved for some sort of ABI version flag */

  int rows, cols;

  const struct fyvt_allocator *allocator;
  void *allocdata;

  /* Override default sizes for various structures */
  size_t outbuffer_len;  /* default: 4096 */
  size_t tmpbuffer_len;  /* default: 4096 */
};

struct fyvt *fyvt_build(const struct fyvt_cfg *builder) FYVT_EXPORT;

/* A convenient shortcut for default cases */
struct fyvt *fyvt_new(int rows, int cols) FYVT_EXPORT;
/* This shortcuts are generally discouraged in favour of just using fyvt_build() */
struct fyvt *fyvt_new_with_allocator(int rows, int cols, struct fyvt_allocator *funcs, void *allocdata) FYVT_EXPORT;

void   fyvt_free(struct fyvt* vt) FYVT_EXPORT;

void fyvt_get_size(const struct fyvt *vt, int *rowsp, int *colsp) FYVT_EXPORT;
void fyvt_set_size(struct fyvt *vt, int rows, int cols) FYVT_EXPORT;

int  fyvt_get_utf8(const struct fyvt *vt) FYVT_EXPORT;
void fyvt_set_utf8(struct fyvt *vt, int is_utf8) FYVT_EXPORT;

size_t fyvt_input_write(struct fyvt *vt, const char *bytes, size_t len) FYVT_EXPORT;

/* Setting output callback will override the buffer logic */
typedef void fyvt_output_callback(const char *s, size_t len, void *user);
void fyvt_output_set_callback(struct fyvt *vt, fyvt_output_callback *func, void *user) FYVT_EXPORT;

/* These buffer functions only work if output callback is NOT set
 * These are deprecated and will be removed in a later version */
size_t fyvt_output_get_buffer_size(const struct fyvt *vt) FYVT_EXPORT;
size_t fyvt_output_get_buffer_current(const struct fyvt *vt) FYVT_EXPORT;
size_t fyvt_output_get_buffer_remaining(const struct fyvt *vt) FYVT_EXPORT;

/* This too */
size_t fyvt_output_read(struct fyvt *vt, char *buffer, size_t len) FYVT_EXPORT;


#ifdef __cplusplus
}
#endif

#endif /* LIBFYVTERM_CORE_H */
