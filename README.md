# libfyvterm

An abstract VT220/xterm/ECMA-48 terminal emulation library: it parses a
terminal byte stream and maintains the resulting screen state, without owning
a PTY, a font, or a window.

A hard fork of Paul Evans' libvterm by way of Neovim's fork, carrying the
fixes Neovim has made in its own tree since. See `PROVENANCE.md` for exactly
what was taken and from where.

## Build

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Nothing beyond a C compiler and CMake is required: the generated Unicode and
encoding tables are checked in, and the test suite is a single C binary.

## Library API

```c
#include <libfyvterm.h>

struct fyvt_cfg cfg;
fyvt_cfg_default(&cfg);
cfg.rows = 25;
cfg.cols = 80;

struct fyvt *vt = fyvt_create(&cfg);      /* NULL cfg is fine: all defaults */
fyvt_set_utf8(vt, 1);

struct fyvt_screen *s = fyvt_obtain_screen(vt);
fyvt_screen_reset(s, 1);

fyvt_input_write(vt, bytes, len);         /* feed the terminal byte stream */

struct fyvt_screen_cell cell;
fyvt_screen_get_cell(s, (struct fyvt_pos){ 0, 0 }, &cell);

fyvt_destroy(vt);
```

Three layers sit on top of each other, and you attach callbacks at whichever
one you need:

| header | layer |
|---|---|
| `libfyvterm/libfyvterm-parser.h` | raw escape sequences |
| `libfyvterm/libfyvterm-state.h` | cursor, pen and terminal modes |
| `libfyvterm/libfyvterm-screen.h` | the cell buffer and damage |

Everything is `fyvt_`-prefixed and nothing else is exported. The terminal,
state and screen are opaque; the value types are plain structs you can hold
by value.

## Tests

`t/` holds the test scripts, one CTest case per file. Each script interleaves
harness commands with the callback events they should produce:

```
!Basic text
PUSH "hello"
  text 0x68, 0x65, 0x6c, 0x6c, 0x6f
?cursor = 0,5
```

`t/run-test.c` drives them and `t/harness.c` executes them.

## Licence

MIT, see `LICENSE`.
