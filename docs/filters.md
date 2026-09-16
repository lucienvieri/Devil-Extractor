# Filter Plugin Infrastructure

Filter plugins extend the DOM walker by providing pre-filter, text transform, and
post-filter hooks. Hooks are function pointers stored in walk_ctx_t — zero overhead
(NULL-pointer check, which the compiler eliminates entirely) when no filters are
enabled.

## Architecture

```
devil-filters.h   — declares walk_ctx_t extensions + default NULL
filters/noise.c   — implements _devil_pre_filter (noise tag drop)
filters/ad-block.c — implements _devil_pre_filter (CSS class drop)
filters/js-filter.c — implements _devil_pre_filter (script/style text extract)
devil.c           — calls hooks at extension points
```

See also:
- [Noise Filter](noise-filter.md)
- [Ad-Block Filter](ad-block-filter.md)
- [JS Filter](js-filter.md)

## Compile-time toggles

Defined in CMakeLists.txt:

- `DEVIL_FILTER_NOISE` — load noise tag filter
- `DEVIL_FILTER_AD_BLOCK` — load CSS-class ad-block filter
- `DEVIL_FILTER_JS` — load script/style text extraction filter

## Memory model

- No allocations in filter code (static const arrays only)
- No global mutable state
- Zero additional dependencies beyond lexbor

## Design note

Function-pointer vtable in walk_ctx_t avoids the C limitation that static inline
functions can't be overridden across translation units. The compiler eliminates NULL
checks when the function is never assigned (always inlined).

## Filter function signatures

### Pre-filter

```c
typedef bool (*devil_pre_filter_fn)(lxb_dom_node_t *node,
                                    lxb_tag_id_t tag,
                                    void *ctx);
```

Called for every element node before children are walked. Return true to prune this
node's subtree (skip children).

### Text transform

```c
typedef bool (*devil_text_transform_fn)(lxb_dom_node_t *node,
                                         void *ctx);
```

Called when a text node is encountered. Can modify the text buffer or signal to skip
the text.

### Post-filter

```c
typedef void (*devil_post_filter_fn)(lxb_dom_node_t *node,
                                      lxb_tag_id_t tag,
                                      void *ctx);
```

Called after all children of an element are processed. Can make decisions about
what to output.

## walk_ctx_t structure

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

Shared between devil.c and filter plugins. Uses the same macro pattern as the
original is_noise().

Noise tags: script, style, nav, aside, footer, header, noscript, iframe, form,
select, head, meta, link, button, embed, object, source, track, map, base,
area, marquee.

## Block-level tag table

Shared between devil.c and filter plugins for block-boundary newline detection.

Block tags: div, p, h1-h6, ul, ol, li, dl, dt, dd, figure, figcaption,
article, section, address, blockquote, pre, hr, br, caption, table, th, td,
tr, thead, tbody, tfoot.
