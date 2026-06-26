# Noise Tag Filter

Noise tag subtree pruner.

Implements devil_pre_filter_fn that drops subtrees rooted at noise
tags:

- script
- style
- nav
- aside
- footer
- header
- noscript
- iframe
- form
- select
- head
- meta
- link
- button
- embed
- object
- source
- track
- map
- base
- area
- marquee

Registered by `noise_register()` which sets `ctx->pre_filter`.

The function is static — no symbol leaks into the public ABI.

## Algorithm

On each element node, the tag ID is checked against the noise table
(`_devil_is_noise()`). If it matches:

1. Set `ctx->skip = true` to signal the walker to prune this subtree
2. Return true to indicate pruning

If the tag is not noise, return false and the walker continues normally.
