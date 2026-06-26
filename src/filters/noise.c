// Noise tag subtree pruner
// See docs/noise-filter.md for full documentation.

#ifndef NOISE_FILTER_DEFINED
#define NOISE_FILTER_DEFINED

#include "devil-filters.h"
#include <stdbool.h>

static bool
noise_pre_filter(lxb_dom_node_t *node, lxb_tag_id_t tag, void *ctx_ptr)
{
    (void)node;
    if (_devil_is_noise(tag)) {
        walk_ctx_t *ctx = (walk_ctx_t *)ctx_ptr;
        ctx->skip = true;
        return true;
    }
    return false;
}

void
noise_register(walk_ctx_t *ctx)
{
    ctx->pre_filter = noise_pre_filter;
}

#endif /* NOISE_FILTER_DEFINED */
