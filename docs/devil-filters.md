# Filter Plugin Infrastructure

Filter plugins extend the DOM walker by providing pre-filter, text
transform, and post-filter hooks. Hooks are function pointers
stored in walk_ctx_t — zero overhead (NULL-pointer check, which the
compiler eliminates entirely) when no filters are enabled.

## Architecture

```
devil-filters.h   — declares walk_ctx_t extensions + default NULL
filters/noise.c   — implements _devil_pre_filter (noise tag drop)
filters/ad-block.c — implements _devil_pre_filter (CSS class drop)
devil.c           — calls hooks at extension points
```

## Compile-time toggles (defined in CMakeLists.txt)

- `DEVIL_FILTER_NOISE` — load noise tag filter
- `DEVIL_FILTER_AD_BLOCK` — load CSS-class ad-block filter

## Memory model

- No allocations in filter code (static const arrays only)
- No global mutable state
- Zero additional dependencies beyond lexbor

## Design note

Function-pointer vtable in walk_ctx_t avoids the C limitation that static inline
functions can't be overridden across translation units. The compiler eliminates
NULL checks when the function is never assigned (always inlined).

## Filter function signatures

These are called at three extension points during DOM walk.
A filter sets ctx->skip = true to signal subtree pruning.

### Pre-filter

```c
typedef bool (*devil_pre_filter_fn)(lxb_dom_node_t *node,
                                    lxb_tag_id_t tag,
                                    void *ctx);
```

Called for every element node before children are walked.
Return true to prune this node's subtree (skip children).

### Text transform

```c
typedef bool (*devil_text_transform_fn)(lxb_dom_node_t *node,
                                         void *ctx);
```

Called when a text node is encountered.
Can modify the text buffer or signal to skip the text.

### Post-filter

```c
typedef void (*devil_post_filter_fn)(lxb_dom_node_t *node,
                                      lxb_tag_id_t tag,
                                      void *ctx);
```

Called after all children of an element are processed.
Can make decisions about what to output.

## Walk context

The base fields match devil.c's original walk_ctx_t layout for
ABI compatibility. Filter hooks are appended after.

```c
typedef struct devil_walk_ctx {
    void     *out;        /* lexbor_str_t* — growing output buffer */
    void     *mraw;       /* lexbor_mraw_t* — arena for alloc       */
    bool      has_output; /* true if we've emitted text            */
    bool      in_block;   /* true if last node was block           */
    bool      skip;       /* set by pre-filters to prune subtree   */

    /* Filter hooks — NULL = no-op when called */
    devil_pre_filter_fn  pre_filter;
    devil_text_transform_fn text_transform;
    devil_post_filter_fn post_filter;
} walk_ctx_t;
```

## Noise tag table

Defined here so both devil.c and filter plugins share the same set.
Uses the same macro pattern as the original is_noise().

```c
#define DEVIL_NOISE_TAGS \
    XX(SCRIPT)  XX(STYLE)  XX(NAV)     XX(ASIDE)  XX(FOOTER) \
    XX(HEADER)  XX(NOSCRIPT) XX(IFRAME) XX(FORM)  XX(SELECT) \
    XX(HEAD)    XX(META)   XX(LINK)    XX(BUTTON) XX(EMBED) \
    XX(OBJECT)  XX(SOURCE) XX(TRACK)   XX(MAP)    XX(BASE)  \
    XX(AREA)    XX(MARQUEE)
```

## Block-level tag table

Defined here so filters can reuse the same block detection.

```c
#define DEVIL_BLOCK_TAGS \
    XX(DIV)   XX(P)       XX(H1) XX(H2) XX(H3) XX(H4) XX(H5) XX(H6) \
    XX(UL)    XX(OL)      XX(LI) XX(DL) XX(DT) XX(DD) XX(FIGURE) \
    XX(FIGCAPTION) \
    XX(ARTICLE) XX(SECTION) XX(ADDRESS) \
    XX(BLOCKQUOTE) XX(PRE) XX(HR) XX(BR) XX(CAPTION) \
    XX(TABLE) XX(TH) XX(TD) XX(TR) XX(THEAD) XX(TBODY) XX(TFOOT)
```

## Helper functions

### _devil_is_noise

Check if a tag ID is a noise tag (shared between devil.c and filters).

### _devil_is_block

Check if a tag ID is a block-level tag (shared between devil.c and filters).
