# Spec: Devil Extractor v2 — Filter Plugin System

## Objective

Add a filter plugin system to devil-extractor v2 so that new extraction filters
can be added without touching devil.c core logic. Filters are modular but
compiled inline — zero function pointer indirection, zero runtime overhead.

**Problem:** Currently noise filtering, block detection, and text extraction are
all monolithic in devil.c. Adding new features requires touching core code.

**Goal:** A macro-composed filter system where each filter is a self-contained
`.c` file. No runtime cost — the compiler sees one monolithic function.

## Performance Constraint (CRITICAL)

**The system must be as fast as the current monolithic devil.c.** No function
pointer indirection, no per-node dispatch loops, no dynamic dispatch. Every
filter is compiled inline via #ifdef + static inline functions.

This means:
- Default build (no extra filters): byte-identical binary to current devil.c
- With filters enabled: inline checks that the compiler optimizes away if unused
- No per-node overhead: filter code is statically embedded at extension points

## Constraints

- Zero new external dependencies beyond lexbor
- Must compile with `gcc -Wall -Wextra -pedantic` with zero warnings
- No dynamic loading (dlopen, plugins, .so)
- Default build is byte-identical to current devil.c when no extra filters enabled
- ABI-stable header (devil.h stays unchanged for existing users)

## Commands

```
Build:  cd /home/dux/devil-extractor-v2 && cmake -B build && cmake --build build
Test:   ./build/devil-v2-cli < test-sites/wiki-article.html
Valgrind: valgrind --leak-check=full ./build/devil-v2-cli < test-sites/wiki-article.html
```

## Project Structure (After)

```
devil-extractor-v2/
├── CMakeLists.txt          # updated: filter options
├── include/
│   └── devil.h             # public API (unchanged)
├── src/
│   ├── devil.c             # core: parser + walk (with extension points)
│   ├── devil-filters.h     # NEW: inline filter hook macros
│   ├── devil-filters.c     # NEW: filter registration/init
│   ├── filters/            # NEW: individual filter plugins
│   │   ├── noise.c         # extracted from devil.c (always-on)
│   │   ├── ad-block.c      # NEW: CSS class-based noise removal
│   │   └── table-aware.c   # NEW: table structure preservation
│   └── main.c              # CLI (unchanged)
```

## Filter Composition Model

Filters are composed at compile time via static inline functions. The walk
function contains extension points that are #ifdef'd:

```c
// In devil.c walk_node():
if (ntype == LXB_DOM_NODE_TYPE_ELEMENT) {
    lxb_tag_id_t tag = lxb_dom_node_tag_id(node);
    /* Extension point: pre-filter hooks (inline, no indirection) */
    _devil_pre_filter(node, tag, ctx);
    if (ctx->skip) return LEXBOR_ACTION_NEXT;
}
```

Each filter defines `_devil_pre_filter()` as a static inline function. When
compiled with the filter enabled, the compiler inlines it. When disabled, the
code doesn't exist.

## Filter Hook Types

1. **PRE_FILTER** — called before node processing, can signal skip (returns bool)
2. **TEXT_HOOK** — called during text extraction, can transform text
3. **POST_FILTER** — called after node children processed, can emit content

## Code Style

- Follow existing devil.c conventions: doxygen comments, static helpers
- Filter code uses `static inline` for zero-call overhead
- Use `#ifdef DEVIL_FILTER_<NAME>` guards around filter code
- New code must not add compiler warnings

## Testing Strategy

- Existing test sites in test-sites/ serve as regression tests
- Each filter individually testable
- Valgrind must report zero leaks with all filters enabled
- Output comparison against baseline (current devil.c output)
- Byte-identical output when filters disabled

## Boundaries

**Always:**
- Run valgrind with all filters enabled before each commit
- Keep devil.h unchanged (ABI-stable)
- Default build must produce identical output to current devil.c

**Ask first:**
- Adding a new filter file
- Changing the filter hook signature
- Changing CMakeLists.txt structure

**Never:**
- Add runtime dependencies (no dlopen, no pkg-config)
- Modify devil.h public API
- Use global mutable state in filter callbacks

## Success Criteria

- [ ] Filter system compiles with zero warnings
- [ ] Noise filter extracted into filters/noise.c, works identically
- [ ] At least 2 new filters implemented (ad-block, table-aware)
- [ ] All 10 test sites produce identical output to current devil.c (with filters disabled)
- [ ] All 10 test sites produce expected output (with filters enabled)
- [ ] Valgrind reports zero memory leaks
- [ ] Default build is byte-identical to current devil.c

## Open Questions

1. Should filter hooks be in devil.c or in separate extension point files?
   → Inline in devil.c for maximum compiler optimization opportunity.
2. How does a plugin signal "skip subtree"?
   → Via a flag in walk_ctx_t or return value from _devil_pre_filter().
3. Do filters need access to the DOM element, or just tag ID?
   → Element for CSS class inspection; tag ID for tag-based decisions.
