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

/*
 * Configuration for fyvt_create(). Zero-initialise it, or call
 * fyvt_cfg_default(), and set only what you need: every field falls back to
 * a sensible default.
 */
struct fyvt_cfg {
  /* sizeof(struct fyvt_cfg), so the library can tell which fields a caller
   * compiled against. fyvt_cfg_default() fills this in. */
  size_t struct_size;

  int rows, cols;              /* default: 25x80 */

  const struct fyvt_allocator *allocator;  /* default: malloc/free */
  void *allocdata;

  /* Override default sizes for various structures */
  size_t outbuffer_len;  /* default: 4096 */
  size_t tmpbuffer_len;  /* default: 4096 */
};

/*
 * fyvt_cfg_default() - fill a configuration with the defaults
 *
 * Sets struct_size and leaves every other field zero, which is what the
 * defaults above are expressed as.
 */
void fyvt_cfg_default(struct fyvt_cfg *cfg) FYVT_EXPORT;

/*
 * fyvt_create() - create a terminal instance
 *
 * Creates a terminal using the given configuration, or the defaults if cfg
 * is NULL. Returns NULL on error.
 */
struct fyvt *fyvt_create(const struct fyvt_cfg *cfg) FYVT_EXPORT;

/*
 * fyvt_destroy() - release a terminal instance
 *
 * Also releases the state and screen layers obtained from it, if any.
 * Does nothing when vt is NULL.
 */
void fyvt_destroy(struct fyvt *vt) FYVT_EXPORT;

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
