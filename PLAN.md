# Implementation Plan: Devil Extractor v2 — Filter Plugin System

## Overview

Restructure devil.c's monolithic walk logic into a compile-time composable
filter system. Filters are static inline functions compiled into devil.c.
No function pointers, no runtime indirection. Default build = byte-identical.

## Architecture

```
devil.c (walk function has 3 extension points):

  _devil_pre_filter(node, tag, ctx)   ← pre-filter hooks (skip decisions)
  _devil_text_transform(node, text)   ← text hooks (transform content)
  _devil_post_filter(node, tag, ctx)  ← post-filter hooks (emit content)

filters/
  noise.c      ← extracted noise filter (always-on, sets ctx->skip)
  ad-block.c   ← CSS class check (sets ctx->skip)
  table-aware.c ← table-aware text/post hooks
```

Each filter file:
```c
#ifdef DEVIL_FILTER_NOISE
static inline void _devil_pre_filter(lxb_dom_node_t *node, lxb_tag_id_t tag, walk_ctx_t *ctx) {
    if (is_noise(tag)) {
        ctx->skip = true;
    }
}
#endif
```

No function pointer dispatch — the compiler sees inline code.

## Task List

### Phase 1: Foundation (1-2 days)

**Task 1: Create devil-filters.h — inline filter hook macros**
- `walk_ctx_t` gains a `skip` flag (bool)
- `_devil_pre_filter()`, `_devil_text_transform()`, `_devil_post_filter()` declared as static inline no-ops
- `#ifdef DEVIL_FILTER_*` guards
- devil.c includes devil-filters.h and calls extension points

**Acceptance:** Compiles with zero warnings, produces identical binary to current devil.c

**Files:** devil-filters.h, devil.c (add 3 extension point calls)

---

**Task 2: Extract noise filter into filters/noise.c**
- Move `is_noise()` and noise-checking from devil.c into filters/noise.c
- noise.c provides `_devil_pre_filter()` that sets `ctx->skip = true`
- Remove noise logic from devil.c walk function
- Verify byte-identical output

**Acceptance:** All 10 test sites produce identical output

**Files:** devil.c (remove noise logic, add noise.c to CMake), filters/noise.c

---

### Phase 2: New Filters (2-3 days)

**Task 3: Implement CSS class-based ad-block filter (filters/ad-block.c)**
- Recognizes: .ad-banner, .ad-container, .cookie-banner, .popup, .modal, .sidebar, .widget, .advertisement
- Checks element class attributes during DOM walk
- Sets `ctx->skip = true` for matching elements
- Class list is static const (no allocation)

**Acceptance:** Ad elements are fully stripped, sibling content preserved

**Files:** filters/ad-block.c

---

**Task 4: Implement table-aware filter (filters/table-aware.c)**
- Detects `<table>`, `<tr>`, `<td>`, `<th>` elements
- Adds `|` prefix on first cell, `| ` between cells, `|` at row end
- Pipe-separated table format (Markdown-style)
- Uses a small table state in walk_ctx_t

**Acceptance:** Tables output as pipe-separated text, non-table content unchanged

**Files:** filters/table-aware.c

---

### Phase 3: Integration (0.5 days)

**Task 5: Update CMakeLists.txt**
- Add `option(DEVIL_FILTERS "Build extra filters" ON)`
- When ON: add filters/*.c to compilation, pass `-DDEVIL_FILTER_*` defines
- When OFF: only noise filter (byte-identical to current)

**Acceptance:** `cmake -DDEVIL_FILTERS=OFF` builds identical binary

**Files:** CMakeLists.txt

---

**Task 6: Verification**
- Run all 10 test sites: compare with baseline (DEVIL_FILTERS=OFF vs ON)
- Run valgrind: zero leaks
- Compile with `-Wall -Wextra -pedantic`: zero warnings

**Acceptance:** All checks pass

**Files:** devil-filters.h (docs)

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Filter hooks add overhead to walk_node | High | Use `static inline` — compiler inlines everything |
| Multiple filters conflict | Medium | Pre-filters run first (noise always first), then text, then post |
| Table filter breaks on complex nested tables | Low | Start with flat tables, add nesting later |

## Open Questions

1. Should the default include noise filter, or make noise also optional?
   → Noise is always-on (it's the baseline behavior). Extra filters are opt-in.
