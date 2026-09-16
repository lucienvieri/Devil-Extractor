// Noise tag subtree pruner
// See docs/noise-filter.md for full documentation.

#ifndef NOISE_FILTER_DEFINED
#define NOISE_FILTER_DEFINED

#include "devil-filters.h"
#include <stdbool.h>

/* Noise tag check — called per element during DOM walk. */
static bool
noise_pre_filter(lxb_dom_node_t *node, lxb_tag_id_t tag, void *ctx_ptr)
{
    (void)node;
    (void)ctx_ptr;
    return _devil_is_noise(tag);
}

void
noise_register(walk_ctx_t *ctx)
{
    devil_pre_filter_register(ctx, noise_pre_filter);
}

#endif /* NOISE_FILTER_DEFINED */
