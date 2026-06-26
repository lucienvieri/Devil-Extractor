# Findings: Devil Extractor v2 — Filter Plugin System (Inline Macro Model)

## Research Findings

### 1. Current Codebase Analysis

**devil.c (535 lines)** is monolithic:
- `is_noise()`, `is_block()` — tag lookup functions (macro-generated switch)
- `trim_trailing()` — whitespace utility
- `walk_node()` — DFS preorder callback handling noise + text + blocks
- `extract_text()` — DOM walker setup + walk + output copy
- `devil_parse()`, `devil_strip()`, `devil_text()` — public API

**Noise tags** (24 entries): script, style, nav, aside, footer, header, noscript, iframe, form, select, head, meta, link, button, embed, object, source, track, map, base, area, marquee.

**Block tags** (~30 entries): div, p, h1-h6, ul, ol, li, dl, dt, dd, figure, figcaption, article, section, address, blockquote, pre, hr, br, caption, table, th, td, tr, thead, tbody, tfoot.

### 2. Performance Constraint Analysis

**Problem:** Function pointer dispatch costs ~1-2ns per call. For a 1000-node page with 4 filters, that's 4000 function pointer indirections — measurable overhead.

**Solution: static inline functions compiled into walk_node()**
- The compiler inlines all filter code into the walk function body
- Zero function pointer indirection
- Default build (no extra filters): compiler eliminates dead code via #ifdef
- Result: byte-identical to current devil.c when filters disabled

**How it works:**
```c
// devil-filters.h — declares extension points
static inline void _devil_pre_filter(lxb_dom_node_t *node, lxb_tag_id_t tag, walk_ctx_t *ctx) {
    // no-op default
}
static inline void _devil_text_transform(lxb_dom_node_t *node, walk_ctx_t *ctx) {
    // no-op default
}
static inline void _devil_post_filter(lxb_dom_node_t *node, lxb_tag_id_t tag, walk_ctx_t *ctx) {
    // no-op default
}

// filters/ad-block.c — provides the real implementation
#ifdef DEVIL_FILTER_AD_BLOCK
#undef _devil_pre_filter
static inline void _devil_pre_filter(lxb_dom_node_t *node, lxb_tag_id_t tag, walk_ctx_t *ctx) {
    // check CSS classes, set ctx->skip if needed
}
#endif

// devil.c — calls extension point
static lexbor_action_t walk_node(lxb_dom_node_t *node, void *arg) {
    walk_ctx_t *ctx = (walk_ctx_t *)arg;
    ...
    _devil_pre_filter(node, tag, ctx);  // inlined at compile time
    ...
}
```

### 3. Filter Composition Design

**Extension points in devil.c walk function:**
1. `_devil_pre_filter(node, tag, ctx)` — runs before node processing. Can set `ctx->skip = true` to skip subtree.
2. `_devil_text_transform(node, ctx)` — wraps text extraction. Can transform the text buffer.
3. `_devil_post_filter(node, tag, ctx)` — runs after children processed. Can emit content.

**Filter state model:**
- Filters receive `walk_ctx_t *ctx` which is the same context passed to walk_node()
- Filters can add state via `ctx->filter_state` (void *) — but only one state struct
- For v1: keep it simple, use a shared state struct in devil-filters.h
- State is zeroed at parse time (devil_parse()), cleared between filters

**Skip signaling:**
- `walk_ctx_t.skip` (bool) — set by pre-filters to signal subtree skip
- When `ctx->skip == true`, walk_node returns LEXBOR_ACTION_NEXT early

### 4. CMake Integration

```cmake
option(DEVIL_FILTERS "Build extra filters" ON)

target_sources(devil-v2-cli PRIVATE
    src/devil.c
    src/devil-filters.c
    src/main.c
    src/filters/noise.c
)

if(DEVIL_FILTERS)
    target_sources(devil-v2-cli PRIVATE
        src/filters/ad-block.c
        src/filters/table-aware.c
    )
    target_compile_definitions(devil-v2-cli PRIVATE
        DEVIL_FILTER_AD_BLOCK=1
        DEVIL_FILTER_TABLE_AWARE=1
    )
endif()
```

### 5. Testing Strategy

- **Regression test:** Run all 10 test sites, compare output byte-for-byte
- **Unit test:** Each filter testable in isolation with crafted HTML
- **Valgrind:** Zero leaks with all filters enabled
- **Compile flags:** `-Wall -Wextra -pedantic` → zero warnings
- **Byte-identical:** `cmp <(build-off) <(build-on)` when DEVIL_FILTERS=OFF

### 6. Performance Verification

- Use `perf stat -d` to measure cache misses across filter-enabled builds
- Compare with current devil.c baseline
- Expect <1% overhead even with all filters enabled (due to inline composition)
