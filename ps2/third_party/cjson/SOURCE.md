# cJSON for the PS2 target

- Project: https://github.com/DaveGamble/cJSON
- Source tag: [`v1.7.19`](https://github.com/DaveGamble/cJSON/tree/v1.7.19)
- Files: `cJSON.c`, `cJSON.h`, `LICENSE` copied verbatim from that tag.
- License: MIT; see `LICENSE` in this directory.

SHA-256 of upstream files (lowercase):

| File | SHA-256 |
|---|---|
| `cJSON.c` | `298581a04a36c0165da4b0aade235c23088cb2faa58651d720ea2f3706ed0b0d` |
| `cJSON.h` | `25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee` |
| `LICENSE` | `a36dda207c36db5818729c54e7ad4e8b0c6fba847491ba64f372c1a2037b6d5c` |

This folder exists only to satisfy `psp/include/json-c/json.h` when building
the game's offline PS2 target. It does not replace any PSP libraries and
does not require network access at build time.
