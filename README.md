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
