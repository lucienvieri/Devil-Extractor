# JS Filter — Extract script/style text content

Extracts text content from `<script>` and `<style>` elements during DOM traversal.

When enabled, text inside `<script>` and `<style>` blocks is captured and emitted
into the output. The element itself is then pruned (subtree not double-emitted).

## Use case

Useful for JS-heavy sites where important content lives inside script blocks:
- Structured data (JSON-LD, Microdata)
- Content rendered by JavaScript frameworks (React, Vue, Angular SSR)
- Configuration data embedded in inline scripts
- CSS variables and themes in `<style>` blocks

## Implementation

Registered by `js_filter()` which sets `ctx->pre_filter` to `js_extract_text`.

### Algorithm

1. When a `<script>` or `<style>` element is encountered, the pre-filter runs.
2. Collects text from all child text nodes, concatenates raw text.
3. Normalizes whitespace using `normalize_ws()` (collapse runs of whitespace,
   strip leading/trailing, preserve paragraph breaks).
4. Prepends a newline separator if text is non-empty.
5. Sets `ctx->skip = true` to prune the subtree after extraction.

### Code

See `src/filters/js-filter.c`.

## CLI flag

`--js` / `-j` — enable script/style text extraction (default: off)

## Memory model

- Uses static 256-byte buffer for normalized text (no heap alloc)
- No global mutable state
- Zero additional dependencies beyond lexbor

## Design notes

- Text from scripts appears between surrounding block elements, not inline
- Multiple script blocks each get their own line in output
- If both `--js` and `--noise` are enabled, scripts/styles are still extracted
  (js-filter runs before noise-pruning via `ctx->skip`)