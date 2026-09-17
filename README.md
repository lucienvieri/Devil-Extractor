# Devil Extractor

The goal is to be the fastest web extractor in existence, best used along side searchxng, it's a header-only C library for extracting readable text from HTML (v2.3). Strips noise (scripts, styles, ads), block tags, and optional filter plugins — powered by the [Lexbor](https://github.com/lexbor/Lexbor) HTML5 parser.

future plans are to integrate Lexbor code into the project   

## Features of 2.4


- **Noise removal** — strips script, style, nav, aside, footer, iframe and 20+ noise tags
- **Block-aware extraction** — preserves paragraph structure with block tag formatting
- **Filter plugin system** — inline macro-based extension points for custom filters:
  - Noise filter (built-in) — tag-based noise removal
  - Ad-block filter (optional) — CSS class-based ad suppression
  - Table-aware filter (optional) — structured table text extraction
  - JSON-LD extractor (optional) — structured data extraction from JSON-LD blocks
- **Zero-allocation walking** — depth-first DOM traversal with minimal overhead
- **Static inline filters** — compile-time filter composition, zero function pointer cost
- **CMake build** — simple build with `cmake` and `make`

## Requirements

- C11 compiler (gcc, clang)
- [Lexbor](https://github.com/lexbor/Lexbor) shared library installed on your system
- CMake 3.16+

## Installation

```bash
# Build
mkdir build && cd build
cmake ..
make

# Install (optional)
sudo make install
```

Lexbor is expected at `/usr/local/lib` by default. Adjust the CMake `PATHS` if installed elsewhere:

```bash
cmake .. -DCMAKE_PREFIX_PATH=/your/lexbor/path
```

## Usage

### CLI

```bash
./devil-v2-cli <url>
./devil-v2-cli <file.html>
```

### Library

Include `devil.h` and call the public API:

```c
#include "devil.h"

devil_ctx_t *ctx = devil_parse(html_string, html_len);
char *text = devil_text(ctx);   // extract readable text
devil_strip(ctx);               // strip in-place
devil_free(ctx);                // cleanup
```

See `docs/devil-api.md` for the full API reference.

## Filter System

The filter plugin system uses compile-time macro extension points:

| Extension Point | Purpose |
|---|---|
| `_devil_pre_filter()` | Runs before node processing; set `ctx->skip` to skip subtree |
| `_devil_text_transform()` | Transform extracted text |
| `_devil_post_filter()` | Runs after children processed; emit content |

Filters are enabled via CMake definitions:

```cmake
add_compile_definitions(DEVIL_FILTER_AD_BLOCK=1)
add_compile_definitions(DEVIL_FILTER_TABLE_AWARE=1)
```

See `docs/devil-filters.md` for filter implementation details.

## Project Structure

```
├── src/
│   ├── main.c              # CLI entry point
│   ├── devil.c             # Core parser + DOM walker
│   └── filters/
│       ├── noise.c         # Noise filter
│       └── json-ld-extract.c
├── include/
│   └── devil.h             # Public header
├── docs/
│   ├── devil-api.md        # API reference
│   └── devil-filters.md    # Filter system design
├── CMakeLists.txt
├── EXTRACTION_ISSUES.md    # Known issues & edge cases
└── SPEC.md                 # Project specification
```

## Known Issues

See `EXTRACTION_ISSUES.md` for a list of known extraction edge cases.

## License

See `LICENSE` in the repository root.
