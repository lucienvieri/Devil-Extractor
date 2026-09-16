// JS filter — extract text content from <script> and <style> elements
// When enabled, text inside <script>/<style> is preserved and emitted
// into the output instead of being pruned entirely.
//
// Usage: devil --js < file.html  or  cat page.html | devil --js
//
// How it works: the filter registers a pre-filter hook that catches
// <script> and <style> elements. When the walk reaches one of these,
// the hook collects the raw text from child text nodes, normalizes
// whitespace, appends it to the walk context's output string, then
// returns true so the rest of the subtree (child nodes) is not visited
// again by the normal walker — preventing double-emission.

#include "devil-filters.h"
#include <stdbool.h>

/* Forward declaration — defined after this file is included. */
static size_t normalize_ws(char *buf, size_t raw_len, const lxb_char_t *raw);

/* Collect raw text from a text node, normalize, and append to ctx->out. */
static void
extract_text_node(walk_ctx_t *ctx, lexbor_str_t *text)
{
    size_t raw_len = lexbor_str_len(text);
    if (raw_len == 0) return;

    const lxb_char_t *raw = (const lxb_char_t *)lexbor_str_data(text);

    /* Skip whitespace-only text nodes (indentation) */
    for (size_t i = 0; i < raw_len; i++) {
        if (raw[i] != ' ' && raw[i] != '\t' && raw[i] != '\n' && raw[i] != '\r') {
            goto do_normalize;
        }
    }
    return; /* all whitespace, skip */

do_normalize:
    char norm[8192];
    if (raw_len > sizeof(norm)) raw_len = sizeof(norm);
    size_t nlen = normalize_ws(norm, raw_len, raw);
    if (nlen > 0) {
        lexbor_str_append(ctx->out, ctx->mraw, "\n", 1);
        lexbor_str_append(ctx->out, ctx->mraw, norm, nlen);
        ctx->in_block = true;
    }
}

/*
 * Pre-filter hook for <script> and <style>.
 *
 * Collects raw text from child text nodes, normalizes whitespace,
 * emits it, then returns true so the subtree is not visited again
 * by the normal walk (avoids double-emission).
 */
static bool
js_pre_filter(lxb_dom_node_t *node, lxb_tag_id_t tag, void *ctx_ptr)
{
    if (tag != LXB_TAG_SCRIPT && tag != LXB_TAG_STYLE)
        return false;

    walk_ctx_t *ctx = (walk_ctx_t *)ctx_ptr;
    lxb_dom_node_t *child = lxb_dom_node_first_child(node);

    while (child) {
        if (lxb_dom_node_type(child) == LXB_DOM_NODE_TYPE_TEXT) {
            lxb_dom_character_data_t *cd = (lxb_dom_character_data_t *)child;
            extract_text_node(ctx, &cd->data);
        }
        child = lxb_dom_node_next(child);
    }

    return true; /* prune subtree */
}

void
js_register(walk_ctx_t *ctx)
{
    devil_pre_filter_register(ctx, js_pre_filter);
}
