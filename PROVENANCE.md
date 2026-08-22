# Provenance

`libfyvterm` is a hard fork of libvterm, Paul Evans' abstract VT220/xterm/
ECMA-48 emulation library, by way of the fork Neovim maintained.

- Original: https://www.leonerd.org.uk/code/libvterm (no release or commit
  since 2023)
- Intermediate fork: https://github.com/neovim/libvterm, forked at commit
  `934bc2fbf21800ac3458a499df8820ca5fb45fd3`
- Licence: MIT (see `LICENSE`), copyright Paul Evans

Neovim stopped tracking real changes in `neovim/libvterm` in mid-2024
(commit `2c01e1f`; everything after that is a CI bump) and moved its working
copy into `neovim/neovim` at `src/nvim/vterm` on 2025-01-03 (commit
`d8bc08db7`, "refactor: adopt vterm"), where it has kept receiving fixes.

This tree carries the substance of those later fixes. They cannot be applied
with `git apply`: `neovim/neovim` splits `vterm.h`/`vterm_internal.h` into
several headers, renames things, and — starting from the `d8bc08db7` import
itself — replaces the `chars[VTERM_MAX_CHARS_PER_CELL]` per-cell codepoint
array with Neovim's own single-integer `schar_T` encoding. Each change worth
carrying was re-expressed by hand against this tree's plain `chars[]` cell
representation.

## Fixes carried from neovim/neovim

Every commit touching `src/nvim/vterm` from the `d8bc08db7` import
(2025-01-03) up to the sync point below was considered. Each was either
ported or skipped, for the reason given. Each ported fix is a separate
commit in this repository's history, and the code comments at each site name
the upstream commit.

Ported:

- `f8c8a245aa` fix(terminal): don't crash on unprintable chars
- `f1c45fc7a4` feat(terminal): theme update notifications (DEC mode 2031)
- `6f0bde11cc` feat(terminal): kitty keyboard protocol (disambiguate only)
- `06a1f82f1c` feat(terminal): forward X1 and X2 mouse events
- `ee143aaf65` fix(terminal): emit Termrequest for all OSC sequences
- `b28bbee539` fix(terminal): skip setting `string_initial` to false on no-op
- `756751afa3` fix(terminal): stack overflow when too many csi args
  (`CSI_ARGS_MAX` 16 -> 32)
- `977e91b424` + `112092271b` DA1 response modernized to `?61;22;52c`.
  Carried the response string only, not the FFI-overridable global Neovim
  added around it for its own Lua tests.
- `03377b9552` feat(terminal): include sequence terminator in TermRequest
  event (adds `VTermStringFragment.terminator`)
- `7a6e8d4430` docs: misc (one comment typo)
- `814f2629cb` fix(terminal): handle split composing chars at right edge.
  Neovim's fix is written against a grapheme-cluster combining algorithm
  this tree does not have; the equivalent behaviour is expressed against the
  existing `vterm_unicode_is_combining()` check.
- `e40c5cb06d` fix(vterm): handle split UTF-8 after ASCII properly
- `2368a9edbd` feat(terminal): support SGR dim, overline attributes
- `b38173e493` feat(terminal): synchronized output (mode 2026). Carried the
  vterm-side mode/prop plumbing only, not Neovim's redraw gating in
  `terminal.c`, which is outside vterm.
- `7bb8231577` fix(terminal): do not reflow altscreen on resize
- `635acc7dc8` fix(terminal): cursor moves on resize when line above is full
  width

Skipped:

- `d8bc08db7` refactor: adopt vterm — the import commit itself, not a patch;
  it is the baseline the above are counted from.
- `47866cd8d2` refactor: delete duplicate utf8-functionality — a deliberate
  feature removal (drops the UK national-charset designator), not a fix.
  Keeping it costs nothing and keeps this tree a superset of Neovim's.
- `442f297c63` refactor(build): remove INCLUDE_GENERATED_DECLARATIONS guards
  — a Neovim build-macro cleanup with no equivalent here.
- `19eb75831b` ci(test): bump Windows runners — CI only, plus a `DLLEXPORT`
  on the global variable this tree doesn't carry (see the DA1 entry above).
- `VTERM_ATTR_URI` / OSC 8 hyperlink support — already present in Neovim's
  tree at the `d8bc08db7` import, so it predates the commit range above and
  was never in `neovim/libvterm` either. Worth revisiting separately if
  hyperlink attributes start to matter.

Last `neovim/neovim` commit considered:
`635acc7dc848853fe4a5db2f0c85dc52e8eabed0` (2026-07-28, `src/nvim/vterm`
HEAD examined: `31de0d69fc7`).

## Taking the next batch

Check `neovim/neovim`'s `src/nvim/vterm` history for commits after the hash
above. For each, decide port or skip using the same judgement: does it touch
vterm's own logic, or only Neovim's UI/build/CI integration; can its intent
be re-expressed against this tree's plain `chars[]` cell representation.
Land each ported fix as its own commit, cite the upstream hash in both the
commit message and a comment at the change site, add it to the list above,
and move the "last commit considered" pointer.

Note that this tree has since diverged further: the public API has been
renamed to the `fyvt_` namespace. Upstream changes must be re-expressed
against the new names.
