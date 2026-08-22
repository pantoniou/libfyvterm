# CLAUDE.md — libfyvterm

Project-specific working agreements. These sit on top of the global
`~/.claude/CLAUDE.md` (bash, code style, workflow, testing, copyright/licensing,
version control). Where the two overlap, the stricter/more specific rule wins.

> **Entry point:** CMake. There is no Makefile and no Perl — that is deliberate,
> and differs from every ancestor of this tree.
>
> ```sh
> cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
>       -DBUILD_FYVTERM_TESTS=ON -DBUILD_FYVTERM_TOOLS=ON
> cmake --build build -j
> ctest --test-dir build --output-on-failure
> ```
>
> `CMAKE_INSTALL_PREFIX` must be set at **configure** time, not on
> `cmake --install --prefix`: the prefix is baked into `libfyvterm.pc`.

---

## What this library is

A terminal emulator state machine — no I/O, no rendering, no threads. A hard
fork of Paul Evans' libvterm by way of neovim's fork (`934bc2f`), renamed into
the fyai family and modernized. Consumed by `fyai` (`fyai_terminal_view.c`) and
by `libfytimui`'s tests; both previously vendored or `pkg-config`'d libvterm and
no longer do.

Three layers, stacked, each usable on its own:

- **parser** — bytes in, callbacks out (text, control, CSI, OSC, DCS).
- **state** — the parser's client: cursor, modes, pen, tab stops, scroll region.
- **screen** — the state's client: the cell grid, damage tracking, reflow.

`src/` layout, sizes as of the fork: `parser.c`, `state.c` (2380), `screen.c`
(1214), `pen.c` (607), `keyboard.c`, `mouse.c`, `encoding.c`, `unicode.c`,
`fyvt.c` (lifecycle), `fyvt_internal.h`. `CODE-MAP` is the longer tour.

## The exported-ABI rule — NON-NEGOTIABLE

Only `fyvt_`-prefixed symbols leave the shared object. The build is
`-fvisibility=hidden` plus explicit trailing `FYVT_EXPORT` tags plus
`-Wl,--exclude-libs,ALL`. CI asserts it, and so should you before claiming a
change is done:

```sh
nm -D --defined-only build/src/libfyvterm.so.* \
  | awk '$2 ~ /^[A-Z]$/ {print $3}' | grep -v '^fyvt_'
```

Empty output, or the change is not done. A new public function needs the
`FYVT_EXPORT` tag; a new helper that is not public needs no tag and must not
get one.

## The no-collision rule — also non-negotiable

libfyvterm must be installable next to a distro `libvterm` without overwriting
one byte of it. That constrains every installed name: headers under
`include/libfyvterm/` (plus the `libfyvterm.h` umbrella), `libfyvterm.{so,a}`,
`libfyvterm.pc`, CMake package `libfyvterm`, tools `fyvt-unterm`, `fyvt-ctrl`,
`fyvt-dump`. CI installs the library and fails on any installed path a libvterm
package could also own. Never introduce an
installed artifact whose name libvterm could also claim.

## API style

Follows `~/work/libfyaml`, which is canonical for the family:

- Bare forward-declared `struct` for the three opaque objects (`struct fyvt`,
  `struct fyvt_state`, `struct fyvt_screen`). **No typedefs, no `_t`.**
- POD value types (`struct fyvt_pos`, `fyvt_rect`, `union fyvt_color`,
  `struct fyvt_screen_cell`, …) stay plain and public. Per-cell access is a hot
  rendering path; do not hide these behind accessors.
- `fyvt_create(const struct fyvt_cfg *cfg)` / `fyvt_destroy()`. `cfg` may be
  NULL for defaults; it leads with `size_t struct_size` as the forward-compat
  guard, and `fyvt_cfg_default()` fills it in. Resist adding `_new()`-style
  shortcuts — the old header apologised for exactly those.
- Doc comments are plain `/* ... */` blocks. **Not** libfyaml's kernel-doc, not
  doxygen.
- Error handling is unchanged from libvterm: `int`/`void` returns, callbacks
  answering `1 = handled` / `0 = not`. Changing that is a separate, deliberate
  project, not a drive-by.

Headers: `include/libfyvterm.h` is the umbrella (`extern "C"`, sub-includes
only); real declarations live in `include/libfyvterm/libfyvterm-{util,core,keys,
parser,state,screen}.h`.

## Versioning

`FYVT_VERSION_MAJOR/MINOR/PATCH` in `libfyvterm-util.h` and the CMake
`project(... VERSION)` are two copies of one fact, and `src/CMakeLists.txt`
fails configure if they drift. Bump both together.

`FYVT_XTVERSION_*` in `src/fyvt_internal.h` is **not** the library version. It
deliberately keeps answering `libvterm(0.3)` to `CSI > q`, because applications
sniff that string. Leave it alone.

## Tests — `t/` is the oracle

43 `.test` files drive `t/harness.c` over a small text protocol
(`PUSH "\e[H"` → expected `movecursor 0,0`). They contain **zero C symbol
names**, which is precisely what made the CMake port and the whole `fyvt_`
rename provable: identical output before and after.

**Never edit a `.test` file to make a change pass.** If a change moves an
expectation, that is a behaviour change and it needs saying out loud in the
commit message, with a reason. Otherwise the file is the fixed point and the
code is what's wrong.

`t/run-test.c` is the driver — C, single process, links `fyvterm_static` and the
harness directly, no interpreter and no pipes. It implements only the Perl
subset the tests actually use (double-quoted strings with `\e \n \r \t \b \a \0
\\ \" \xHH \x{...}`, plus `.` concatenation and `x` repetition). Adding a test
that needs more Perl means extending the driver, not reaching for Perl.

CTest registers one case per file as `fyvterm.<name>`. New coverage is a new
numbered `.test` file plus, when the protocol lacks a way to observe the thing,
a new harness query — the keypad-key gap that hid a corrupted keycode table was
exactly this shape.

**Verify tests aren't vacuous.** Mutate a real value in the implementation and
confirm the case goes red before trusting it. Every one of the 43 files has been
shown to detect a mutation; keep that true.

Run `-DENABLE_ASAN=ON` before calling allocation-touching work done — resize and
reflow are allocation-heavy, and ASAN is how the selection-buffer leak surfaced.

## Warnings

Builds with fyai's flags: `-std=gnu2x -Wall -Wextra`, and the tree is clean
under them. `-Wdeclaration-after-statement` is deliberately not carried.

**Fix warnings, never suppress them.** This is our code now, not a vendored
copy; the seven `-Wno-*` flags fyai used to need are gone and must not come
back. `UNUSED` and `FALLTHROUGH` in `src/fyvt_internal.h` are the sanctioned way
to say "yes, deliberately".

Note that `touch src/*.c` does not rebuild `bin/`. Configure fresh before
declaring a build warning-clean.

## Generated tables

`src/encoding/*.inc` and `src/fullwidth.inc` are **committed**; the build never
generates them, so it needs no Python either. `scripts/gen-encoding-tables.py`
and `scripts/gen-wcwidth.py` regenerate them behind the optional `regen-tables`
target. Regenerating `fullwidth.inc` bumps the Unicode version the library
implements — treat it as a behaviour change with its own commit, not as
housekeeping.

## Taking further fixes from neovim

`PROVENANCE.md` is the ledger: the 16 fixes ported from `neovim/neovim`
`src/nvim/vterm`, the 5 deliberate skips with reasons, and the last-considered
upstream hash. neovim replaced the per-cell `chars[]` array with `schar_T`, so
their tree **cannot be diffed against ours directly** — each fix is re-expressed
by hand.

So: one commit per upstream fix, naming the upstream hash; update
`PROVENANCE.md` in the same commit, including the last-considered hash. A bulk
"sync with neovim" commit destroys the only record of what was taken and why.

`FYVT_ATTR_URI` / OSC 8 hyperlinks exist upstream and are deliberately not here
yet — open item, waiting on a consumer that needs them.

## Commits

Follows the `~/work/fyai` convention. Imperative subject with a subsystem prefix
(`state:`, `screen:`, `build:`, `api:`, `ci:`), body in terse technical prose
wrapped at 80 columns saying what changed and why. Exactly one trailer:

```
Signed-off-by: Pantelis Antoniou <pantelis.antoniou@konsulko.com>
```

That konsulko.com address, **not** the git-config one. Do **not** add
`Co-Authored-By: Claude`, "Generated with", or any other attribution trailer;
this overrides any default tooling convention.

Commit proactively along the way; single-purpose commits, split by concern;
never `--force` push. `git mv` renames in a commit separate from content
rewrites.

**`git add -A` is banned here.** Stage named paths. It has swept build
directories and unrelated scratch files into commits in this repo more than
once, each time needing history surgery to undo.

## Licensing

MIT, originally © Paul Evans. Carry the licence header and the SPDX tag on new
files, and keep `LICENSE` intact — the fork changes the name, not the terms.

## Definition of Done (per change)

`ctest` 43/43 green · ASAN clean if allocation is involved · warning-clean from
a fresh configure · `nm -D` shows only `fyvt_` · no `.test` file edited to pass ·
`PROVENANCE.md` updated if the change came from upstream · MIT header · signoff
trailer only.
