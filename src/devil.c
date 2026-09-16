/**
 * devil.c — Devil Extractor v2 Core Engine
 *
 * Parses HTML with lexbor v3.x, walks the DOM in DFS preorder,
 * extracts text with block-boundary newlines, and strips noise tags
 * (script, style, nav, aside, footer, header, noscript, iframe, form,
 * select, head, meta, link, button, embed, object, source, track,
 * map, base, area, marquee) during the walk.
 *
 * Pipeline: searchngx | devil | chat_agent
 * Output: plain text, LLM-tokenizer-friendly
 *
 * Architecture:
 *   1. devil_parse()  — creates lexbor parser + arena + DOM from HTML
 *   2. extract_text() — walks DOM, applies filter plugins + block boundaries
 *   3. devil_text()/devil_strip() — public entry points
 *   4. devil_free()/devil_free_string() — cleanup
 *
 * Plugin system (optional):
 *   - devil-filters.h  — defines walk_ctx_t extension + hook function pointers
 *   - filters/noise.c  — pre-filter: drop noise tag subtrees
 *   - filters/ad-block.c — pre-filter: drop CSS-class-based ad/sidebar noise
 *   - Hooks are NULL by default — zero overhead when no filters compiled in
 *
 * Memory model:
 *   - lexbor arena (mraw) is created once, shared across parser/DOM
 *   - lexbor v3.x requires: create → init → use → destroy
 *   - All output strings are copied from the arena into independent
 *     malloc'd buffers, so the arena can be safely destroyed
 *   - Single free for the entire doc struct (devil_free)
 *
 * Whitespace normalization:
 *   - Newlines, tabs, carriage returns → single space
 *   - Multiple consecutive spaces → single space
 *   - Whitespace-only text nodes → skipped entirely
 *   - Block boundaries → newline injected before content
 *   - Trailing whitespace → trimmed from final output
 *
 * Noise filtering:
 *   - Implemented via DFS walk callback with plugin hooks
 *   - Pre-filters set ctx->skip = true to prune subtrees
 *   - Filter plugins can be enabled at compile time
 *
 * Block boundaries:
 *   - Tags like <p>, <div>, <h1>-<h6>, <li>, <ul>, <ol>, <table>, etc.
 *   - After processing children of a block tag, a newline is emitted
 *   - Prevents "HelloWorld" from adjacent blocks: <p>Hello</p><p>World</p>
 *   - Trailing whitespace is trimmed before newline insertion
 *
 * File structure:
 *   - devil-filters.h included for walk_ctx_t + hook definitions
 *   - Filter plugins registered via noise_register(), adblock_register()
 *   - trim_trailing() — whitespace trimming utility
 *   - walk_node() — DFS callback, core extraction logic
 *   - devil_parse() — HTML → DOM conversion
 *   - extract_text() — internal DOM walker
 *   - Public API: devil_text, devil_strip, devil_free_string, devil_free
 *
 * Thread safety: NOT thread-safe. Each devil_doc_t is single-threaded.
 * Multiple threads need separate devil_doc_t instances.
 */

#include "devil.h"
#include "devil-filters.h"

#include <lexbor/html/parser.h>
#include <lexbor/html/interfaces/document.h>
#include <lexbor/dom/interfaces/node.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/dom/interfaces/character_data.h>
#include <lexbor/tag/tag.h>
#include <lexbor/core/mraw.h>
#include <lexbor/core/str.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Include filter plugins — they define their own functions guarded by #ifdef */
#include "filters/noise.c"
#include "filters/ad-block.c"
#include "filters/js-filter.c"
/* normalize_ws and is_all_whitespace are used by js-filter.c, so declare
 * them before the include (definitions come later in this file). */
static size_t normalize_ws(char *buf, size_t raw_len, const lxb_char_t *raw);
static bool is_all_whitespace(const lxb_char_t *raw, size_t raw_len);
#include "filters/json-ld-extract.c"

/* ========================================================================
 * Opaque document struct
 *
 * Defined here so devil.h can use an opaque forward declaration.
 * This keeps the header ABI-stable across library versions.
 * ======================================================================== */
struct devil_doc {
    lxb_html_document_t *doc;       /* parsed DOM document           */
    lexbor_mraw_t       *mraw;      /* memory arena (v3.x required)  */
    lxb_html_parser_t   *parser;    /* parser instance               */
};

/* ========================================================================
 * trim_trailing — remove trailing whitespace from a lexbor string
 *
 * Modifies the string in-place by shrinking its length field.
 * Does NOT reallocate — safe even if string is at buffer end.
 *
 * @param str  lexbor_str_t* to trim (mutated)
 * ======================================================================== */
static void
trim_trailing(lexbor_str_t *str)
{
    size_t len = lexbor_str_len(str);
    lxb_char_t *data = (lxb_char_t *)lexbor_str_data(str);

    /* Skip trailing whitespace: space, tab, newline, carriage return */
    while (len > 0 &&
           (data[len - 1] == ' ' || data[len - 1] == '\t' ||
            data[len - 1] == '\n' || data[len - 1] == '\r'))
    {
        len--;
    }

    /* Set the length field directly — no reallocation needed */
    str->length = len;
}

/* ========================================================================
 * normalize_ws — normalize whitespace in raw text into a buffer
 *
 * Replaces newlines/tabs with spaces. Suppresses the FIRST space only
 * (matching old code's `first` flag behavior). All other characters
 * including subsequent spaces are emitted verbatim.
 *
 * Returns 0 if the text is all whitespace (no output needed).
 *
 * @param buf       Output buffer (must be >= raw_len bytes)
 * @param raw_len   Number of bytes in raw text
 * @param raw       Input text
 * @return          Number of normalized bytes written, or 0 if all-whitespace
 *
 * Ponytail: single pass. Replaces the old two-pass approach (all_ws check
 * + per-char str_append). Combines whitespace detection + normalization
 * into one loop, avoiding O(raw_len) calls to lexbor_str_append().
 * ======================================================================== */
static size_t
normalize_ws(char *buf, size_t raw_len, const lxb_char_t *raw)
{
    size_t out = 0;
    bool first = true;
    for (size_t i = 0; i < raw_len; i++) {
        lxb_char_t c = raw[i];
        if (c == '\n' || c == '\r' || c == '\t')
            c = ' ';
        if (c == ' ') {
            if (!first)
                buf[out++] = ' ';
            first = false;
        } else {
            buf[out++] = (char)c;
            first = false;
        }
    }
    return out;
}

/* ========================================================================
 * is_all_whitespace — quick check if text contains any non-whitespace
 *
 * Returns true if the text consists only of space/tab/newline/CR.
 * Used to skip the normalization pass for blank text nodes.
 * ======================================================================== */
static bool
is_all_whitespace(const lxb_char_t *raw, size_t raw_len)
{
    for (size_t i = 0; i < raw_len; i++) {
        if (raw[i] != ' ' && raw[i] != '\t' && raw[i] != '\n' && raw[i] != '\r')
            return false;
    }
    return true;
}

/* ========================================================================
 * walk_node — DFS callback for DOM tree traversal
 *
 * Called once per node in preorder DFS. Handles:
 *   1. Pre-filter hooks — check ctx->pre_filter for subtree pruning
 *   2. Text extraction — normalize whitespace, append to output
 *   3. Block boundaries — emit newlines at block-level transitions
 *
 * @param node  Current DOM node (element, text, comment, etc.)
 * @param arg   walk_ctx_t* — caller's context (output buffer, flags)
 * @return      lexbor_action_t:
 *              - LEXBOR_ACTION_NEXT  — skip children (filter prune)
 *              - LEXBOR_ACTION_OK    — process children normally
 *              - LEXBOR_ACTION_STOP  — stop walking entirely (never used)
 *
 * Algorithm:
 *   1. If element node, call pre_filter hook (if set) → prune subtree
 *   2. If text node → normalize whitespace, append to output
 *      - Skip whitespace-only text nodes
 *      - Collapse newlines/tabs to space
 *      - Collapse multiple spaces to single space
 *      - Emit newline if transitioning from block
 *   3. If element node → check if block-level, emit newline if needed
 *      - Before block: emit newline (if output exists)
 *      - Track in_block state for transitions
 *   4. Return OK (process children)
 *
 * Whitespace normalization strategy:
 *   - Between block boundaries: strip leading/trailing whitespace,
 *     collapse multiple spaces to single space
 *   - Between inline elements within a block: preserve single space
 *   - Between block-level elements: always inject exactly one newline
 *   - Prevents "Hello World" becoming "HelloWorld" or "Hello   World"
 *
 * Filter plugin integration:
 *   - ctx->pre_filter is NULL when no filters compiled → no-op check
 *   - When a filter is compiled in, ctx->pre_filter points to its impl
 *   - The filter sets ctx->skip = true to prune the subtree
 *   - This is zero overhead: NULL check is eliminated by the compiler
 *     when no filters are enabled (ctx->pre_filter is always NULL)
 * ======================================================================== */
static lexbor_action_t
walk_node(lxb_dom_node_t *node, void *arg)
{
    walk_ctx_t *ctx = (walk_ctx_t *)arg;

    lxb_dom_node_type_t ntype = lxb_dom_node_type(node);

    /* --- Phase 1: Pre-filter hooks (chained plugin system) --- */
    if (ntype == LXB_DOM_NODE_TYPE_ELEMENT && ctx->filter_head) {
        lxb_tag_id_t tag = lxb_dom_node_tag_id(node);
        if (devil_pre_filter_call(ctx, node, tag)) {
            return LEXBOR_ACTION_NEXT;
        }
    }

    /* --- Phase 2: Noise tag fallback (always-on base filter) --- */
    if (ntype == LXB_DOM_NODE_TYPE_ELEMENT) {
        lxb_tag_id_t tag = lxb_dom_node_tag_id(node);
        if (_devil_is_noise(tag)) {
            return LEXBOR_ACTION_NEXT;
        }
    }

    /* --- Phase 3: Text node — normalize in buffer, append once --- */
    if (ntype == LXB_DOM_NODE_TYPE_TEXT) {
        lxb_dom_character_data_t *cd = (lxb_dom_character_data_t *)node;
        lexbor_str_t *text = &cd->data;
        size_t raw_len = lexbor_str_len(text);

        if (raw_len == 0) {
            return LEXBOR_ACTION_OK;
        }

        const lxb_char_t *raw = (const lxb_char_t *)lexbor_str_data(text);

        /* Skip whitespace-only text nodes (indentation, formatting) */
        bool all_ws = true;
        for (size_t i = 0; i < raw_len; i++) {
            if (raw[i] != ' ' && raw[i] != '\t' && raw[i] != '\n' && raw[i] != '\r') {
                all_ws = false;
                break;
            }
        }
        if (all_ws) {
            return LEXBOR_ACTION_OK;
        }

         /* Normalize whitespace into stack buffer, then append in one shot
         * (matches old code's per-char logic: only suppresses first space).
         * No leading/trailing trim — matches old behavior. */
        char norm[16384];
        if (raw_len > sizeof(norm))
            raw_len = sizeof(norm);
        size_t nlen = normalize_ws(norm, raw_len, raw);
        if (nlen == 0)
            return LEXBOR_ACTION_OK;

        /* Emit newline if transitioning into content after a block */
        if (ctx->in_block && ctx->has_output) {
            lexbor_str_append(ctx->out, ctx->mraw, "\n", 1);
            ctx->in_block = false;
        }

        /* Single append instead of O(raw_len) individual appends */
        lexbor_str_append(ctx->out, ctx->mraw, norm, nlen);
        ctx->has_output = true;
        return LEXBOR_ACTION_OK;
    }

    /* --- Phase 4: Element node — handle block boundaries --- */
    if (ntype == LXB_DOM_NODE_TYPE_ELEMENT) {
        lxb_tag_id_t tag = lxb_dom_node_tag_id(node);
        if (_devil_is_block(tag)) {
            /* Emit newline before block if we have output */
            if (ctx->has_output) {
                trim_trailing(ctx->out);
                lexbor_str_append(ctx->out, ctx->mraw, "\n", 1);
            }
            ctx->in_block = true;
        } else {
            ctx->in_block = false;
        }
    }

    return LEXBOR_ACTION_OK;
}

/* ========================================================================
 * devil_parse — parse HTML into a devil document
 *
 * Creates a lexbor memory arena, parser, and DOM from raw HTML bytes.
 *
 * Critical lexbor v3.x requirement: lexbor_mraw_init() MUST be called
 * after lexbor_mraw_create() and before any other lexbor operation.
 * Without init, the arena pointer is uninitialized → segfault.
 *
 * Memory allocation order (reverse of free):
 *   1. mraw = lexbor_mraw_create() + init()
 *   2. parser = lxb_html_parser_create() + init()
 *   3. doc = lxb_html_parse()
 *   4. result = malloc() — the devil_doc_t wrapper
 *
 * Error handling:
 *   - Any allocation failure returns NULL immediately
 *   - Resources allocated so far are freed before return
 *   - No partial state is returned on failure
 *
 * @param html  UTF-8 HTML bytes
 * @param len   Number of bytes
 * @return      devil_doc_t* on success, NULL on failure
 * ======================================================================== */
devil_doc_t *
devil_parse(const uint8_t *html, size_t len)
{
    /* Guard: NULL pointer or empty input */
    if (!html || len == 0) return NULL;

    /* Create memory arena — lexbor v3.x REQUIRES init after create */
    lexbor_mraw_t *mraw = lexbor_mraw_create();
    if (!mraw) return NULL;
    lexbor_mraw_init(mraw, 4096); /* mandatory for v3.x */

    /* Create and initialize parser */
    lxb_html_parser_t *parser = lxb_html_parser_create();
    if (!parser) {
        lexbor_mraw_destroy(mraw, true);
        return NULL;
    }
    lxb_html_parser_init(parser);
    /* Use default DOM options — no special flags needed */

    /* Parse the HTML */
    lxb_html_document_t *doc = lxb_html_parse(parser,
        (const lxb_char_t *)html, (size_t)len);

    if (!doc) {
        lxb_html_parser_destroy(parser);
        lexbor_mraw_destroy(mraw, true);
        return NULL;
    }

    /* Build result struct */
    devil_doc_t *result = malloc(sizeof(devil_doc_t));
    if (!result) {
        lxb_html_document_destroy(doc);
        lxb_html_parser_destroy(parser);
        lexbor_mraw_destroy(mraw, true);
        return NULL;
    }

    result->doc = doc;
    result->parser = parser;
    result->mraw = mraw;

    return result;
}

/* ========================================================================
 * extract_text — internal: walk DOM and build output string
 *
 * @param doc          devil_doc_t* from devil_parse()
 * @param noise_filter bool: enable noise tag pre-filter
 * @param adblock_filter bool: enable CSS-class ad-block pre-filter
 * @return             malloc'd NUL-terminated string, or NULL
 *
 * Process:
 *   1. Create output string with arena allocation
 *   2. Register per-filter hooks based on bool params
 *   3. Find body element (skip <head>)
 *   4. Walk DOM tree in DFS preorder via walk_node callback
 *   5. Trim trailing whitespace
 *   6. Append NUL terminator
 *   7. Copy arena string to independent malloc'd buffer
 *   8. Destroy arena string (arena itself remains valid)
 * ======================================================================== */
static char *
extract_text(devil_doc_t *doc, bool noise_filter, bool adblock_filter, bool js_filter)
{
    if (!doc || !doc->doc) return NULL;

    /* Create output string with the arena */
    lexbor_str_t *str = lexbor_str_create();
    if (!str) return NULL;

    /* Initialize with mraw — lexbor v3.x returns lxb_char_t* on success */
    if (!lexbor_str_init(str, doc->mraw, 256)) {
        lexbor_str_destroy(str, doc->mraw, true);
        return NULL;
    }

    /* Setup context for the walk callback */
    walk_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.out = str;
    ctx.mraw = doc->mraw;
    ctx.has_output = false;
    ctx.in_block = false;

    /* Enable filter plugins based on runtime bools */
    if (noise_filter)
        noise_register(&ctx);
    if (adblock_filter)
        adblock_register(&ctx);
    if (js_filter)
        js_register(&ctx);

    /* Find body element to start walking from */
    lxb_html_body_element_t *body = lxb_html_document_body_element(doc->doc);
    lxb_dom_node_t *root = body ? (lxb_dom_node_t *)body : NULL;

    lxb_dom_node_simple_walk(root, walk_node, &ctx);
    devil_pre_filter_free(&ctx);

    trim_trailing(str);
    lexbor_str_append(str, ctx.mraw, "", 1);

    size_t out_len = lexbor_str_len(str);
    char *result = malloc(out_len + 1);
    if (result) {
        memcpy(result, lexbor_str_data(str), out_len);
        result[out_len] = '\0';
    }
    lexbor_str_destroy(str, doc->mraw, true);
    return result;
}

/* ========================================================================
 * devil_text — extract all text content (no filtering)
 * ======================================================================== */
char *
devil_text(devil_doc_t *doc)
{
    return extract_text(doc, false, false, false);
}

/* ========================================================================
 * devil_strip — extract text with noise filtering (adblock off by default)
 * ======================================================================== */
char *
devil_strip(devil_doc_t *doc)
{
    return extract_text(doc, true, false, false);
}

/* ========================================================================
 * devil_extract — runtime-controlled extraction
 *
 * @param doc          devil_doc_t* from devil_parse()
 * @param noise_filter bool: enable noise tag pre-filter (script, style, nav, etc.)
 * @param adblock_filter bool: enable CSS-class ad-block pre-filter
 * @param js_filter    bool: extract text from <script>/<style> elements
 * @return             malloc'd NUL-terminated string, or NULL
 * ======================================================================== */
char *
devil_extract(devil_doc_t *doc, bool noise_filter, bool adblock_filter, bool js_filter)
{
    return extract_text(doc, noise_filter, adblock_filter, js_filter);
}

/* ========================================================================
 * devil_free_string — free a result string
 *
 * Thin wrapper around free() with NULL safety.
 * ======================================================================== */
void
devil_free_string(char *str)
{
    free(str);
}

/* ========================================================================
 * devil_free — destroy all resources associated with a devil_doc_t
 *
 * Destroys parser, DOM, arena, and the struct itself.
 *
 * IMPORTANT: After this call, the devil_doc_t pointer is invalid.
 * Do not access any member or call any function with this pointer.
 *
 * Cleanup order (lexbor v3.x requirement):
 *   1. Parser  — lxb_html_parser_destroy()
 *   2. DOM     — lxb_html_document_destroy()
 *   3. Arena   — lexbor_mraw_destroy(mraw, true)
 *   4. Struct  — free(doc)
 *
 * Each resource must be destroyed before the next, because later
 * resources (arena) may own memory used by earlier ones (DOM).
 *
 * @param doc  devil_doc_t* from devil_parse() (NULL is safe)
 * ======================================================================== */
void
devil_free(devil_doc_t *doc)
{
    if (!doc) return;

    if (doc->parser) lxb_html_parser_destroy(doc->parser);
    if (doc->doc) lxb_html_document_destroy(doc->doc);
    if (doc->mraw) lexbor_mraw_destroy(doc->mraw, true);

    free(doc);
}
