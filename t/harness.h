/*
 * harness.h - the test harness command interpreter
 *
 * The harness turns a line of the test-script command language (INIT, PUSH,
 * WANTSTATE, ?cursor, ...) into libvterm calls, and records the callback
 * events libvterm reports back as lines of text. run-test.c drives it and
 * compares those lines against the expectations in a .test file.
 *
 * Output is collected rather than printed so that the driver and the
 * interpreter can live in one process, with no pipes and no second language
 * in the build.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HARNESS_H
#define HARNESS_H

#include <stdarg.h>
#include <stddef.h>

/*
 * Run one command line. Returns 0 if the command was understood, non-zero
 * if it was not (the "?" reply of the old pipe protocol).
 *
 * Any event lines the command produced are available afterwards through
 * harness_output_count()/harness_output_line(), and are discarded by the
 * next harness_output_reset().
 */
int harness_command(const char *cmdline);

/* Release the terminal instance; safe to call when none was created. */
void harness_cleanup(void);

/* Collected output lines from the most recent command(s). */
void harness_output_reset(void);
size_t harness_output_count(void);
const char *harness_output_line(size_t i);

/* Used by the harness itself, in place of printf(). */
int harness_printf(const char *fmt, ...);

#endif /* HARNESS_H */
