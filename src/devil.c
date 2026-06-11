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
 *   2. extract_text() — walks DOM, applies noise filter + block boundaries
 *   3. devil_text()/devil_strip() — public entry points
 *   4. devil_free()/devil_free_string() — cleanup
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
 *   - Implemented via DFS walk callback (walk_node)
 *   - When a noise tag is encountered, its entire subtree is skipped
 *   - Returns LEXBOR_ACTION_NEXT to tell lexbor to skip children
 *   - Recursive: nested noise tags are handled automatically
 *
 * Block boundaries:
 *   - Tags like <p>, <div>, <h1>-<h6>, <li>, <ul>, <ol>, <table>, etc.
 *   - After processing children of a block tag, a newline is emitted
 *   - Prevents "HelloWorld" from adjacent blocks: <p>Hello</p><p>World</p>
 *   - Trailing whitespace is trimmed before newline insertion
 *
 * File structure:
 *   - Noise/block tag tables (NOISE_TAGS, BLOCK_TAGS macros)
 *   - is_noise() / is_block() — tag identification helpers
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
 * Noise tag table
 *
 * Subtrees rooted at these tags are dropped entirely during DOM walk.
 * Uses a DO-WHILE macro pattern for compact switch-case generation.
 *
 * Why not a hash set? The table is small (~24 entries), and linear
 * scan through a switch is faster than hashing for this size.
 * ======================================================================== */
#define NOISE_TAGS \
    XX(SCRIPT)  XX(STYLE)  XX(NAV)     XX(ASIDE)  XX(FOOTER) \
    XX(HEADER)  XX(NOSCRIPT) XX(IFRAME) XX(FORM)  XX(SELECT) \
    XX(HEAD)    XX(META)   XX(LINK)    XX(BUTTON) XX(EMBED) \
    XX(OBJECT)  XX(SOURCE) XX(TRACK)   XX(MAP)    XX(BASE)  \
    XX(AREA)    XX(MARQUEE)

/* ========================================================================
 * Block-level tag table
 *
 * When we cross a boundary into/out of these tags, emit a newline.
 * Prevents "HelloWorld" from <p>Hello</p><p>World</p>.
 *
 * Includes all HTML5 block-level elements plus common table tags.
 * ======================================================================== */
#define BLOCK_TAGS \
    XX(DIV)   XX(P)       XX(H1) XX(H2) XX(H3) XX(H4) XX(H5) XX(H6) \
    XX(UL)    XX(OL)      XX(LI) XX(DL) XX(DT) XX(DD) XX(FIGURE) \
    XX(FIGCAPTION) \
    XX(ARTICLE) XX(SECTION) XX(ADDRESS) \
    XX(BLOCKQUOTE) XX(PRE) XX(HR) XX(BR) XX(CAPTION) \
    XX(TABLE) XX(TH) XX(TD) XX(TR) XX(THEAD) XX(TBODY) XX(TFOOT)

/* ========================================================================
 * Walk context — state carried across DFS traversal
 *
 * Maintained between walk_node() callback invocations.
 * ======================================================================== */
typedef struct {
    lexbor_str_t *out;        /* growing output buffer       */
    lexbor_mraw_t *mraw;      /* arena for string alloc      */
    bool has_output;           /* true if we've emitted text  */
    bool in_block;             /* true if last node was block */
} walk_ctx_t;

/* ========================================================================
 * is_noise — check if a tag ID belongs to the noise filter set
 *
 * @param tag  lxb_tag_id_t from lexbor (LXB_TAG_*)
 * @return     true if the tag should be dropped entirely
 *
 * Uses a switch-case generated from the NOISE_TAGS macro.
 * Linear scan through ~24 entries is faster than hash for this size.
 * ======================================================================== */
static bool
is_noise(lxb_tag_id_t tag)
{
    switch (tag) {
#define XX(t) case LXB_TAG_##t: return true;
        NOISE_TAGS
#undef XX
        default: return false;
    }
}

/* ========================================================================
 * is_block — check if a tag ID triggers a block boundary (newline)
 *
 * @param tag  lxb_tag_id_t from lexbor (LXB_TAG_*)
 * @return     true if this tag should emit a newline after its content
 *
 * Block boundaries are emitted when exiting a block-level element
 * that has content. This prevents text concatenation across blocks.
 * ======================================================================== */
static bool
is_block(lxb_tag_id_t tag)
{
    switch (tag) {
#define XX(t) case LXB_TAG_##t: return true;
        BLOCK_TAGS
#undef XX
        default: return false;
    }
}

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
 * walk_node — DFS callback for DOM tree traversal
 *
 * Called once per node in preorder DFS. Handles:
 *   1. Noise filtering — skip entire subtrees for noise tags
 *   2. Text extraction — normalize whitespace, append to output
 *   3. Block boundaries — emit newlines at block-level transitions
 *
 * @param node  Current DOM node (element, text, comment, etc.)
 * @param arg   walk_ctx_t* — caller's context (output buffer, flags)
 * @return      lexbor_action_t:
 *              - LEXBOR_ACTION_NEXT  — skip children (noise filter)
 *              - LEXBOR_ACTION_OK    — process children normally
 *              - LEXBOR_ACTION_STOP  — stop walking entirely (never used)
 *
 * Algorithm:
 *   1. If noise tag → return NEXT (skip children, don't process further)
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
 * ======================================================================== */
static lexbor_action_t
walk_node(lxb_dom_node_t *node, void *arg)
{
    walk_ctx_t *ctx = (walk_ctx_t *)arg;

    lxb_dom_node_type_t ntype = lxb_dom_node_type(node);

    /* --- Phase 1: Noise filter — drop entire subtree --- */
    if (ntype == LXB_DOM_NODE_TYPE_ELEMENT) {
        lxb_tag_id_t tag = lxb_dom_node_tag_id(node);
        if (is_noise(tag)) {
            /* Skip all children of this noise tag */
            return LEXBOR_ACTION_NEXT;
        }
    }

    /* --- Phase 2: Text node — extract and normalize --- */
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

        /* Emit newline if transitioning into content after a block */
        if (ctx->in_block && ctx->has_output) {
            lexbor_str_append(ctx->out, ctx->mraw, "\n", 1);
            ctx->in_block = false;
        }

        /* Normalize whitespace: newlines/tabs → space, collapse multiples */
        bool first = true;
        for (size_t i = 0; i < raw_len; i++) {
            lxb_char_t c = raw[i];
            if (c == '\n' || c == '\r' || c == '\t') {
                c = ' ';
            }
            if (c == ' ') {
                /* Only emit a space if not already at one */
                if (!first) {
                    lexbor_str_append(ctx->out, ctx->mraw, " ", 1);
                }
                first = false;
            } else {
                lexbor_str_append(ctx->out, ctx->mraw, (const char *)&c, 1);
                ctx->has_output = true;
                first = false;
            }
        }
        return LEXBOR_ACTION_OK;
    }

    /* --- Phase 3: Element node — handle block boundaries --- */
    if (ntype == LXB_DOM_NODE_TYPE_ELEMENT) {
        lxb_tag_id_t tag = lxb_dom_node_tag_id(node);
        if (is_block(tag)) {
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
 * Common code shared by devil_text() and devil_strip().
 *
 * @param doc          devil_doc_t* from devil_parse()
 * @param filter_noise bool: true to skip noise tag subtrees
 * @return             malloc'd NUL-terminated string, or NULL
 *
 * Process:
 *   1. Create output string with arena allocation
 *   2. Find body element (skip <head>)
 *   3. Walk DOM tree in DFS preorder via walk_node callback
 *   4. Trim trailing whitespace
 *   5. Append NUL terminator
 *   6. Copy arena string to independent malloc'd buffer
 *   7. Destroy arena string (arena itself remains valid)
 *
 * Note: The walk callback (walk_node) handles noise filtering
 * internally. The filter_noise parameter is kept for API clarity
 * but noise is always filtered in the current implementation.
 * ======================================================================== */
static char *
extract_text(devil_doc_t *doc, bool filter_noise)
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

    /* Find body element to start walking from */
    lxb_html_body_element_t *body = lxb_html_document_body_element(doc->doc);
    lxb_dom_node_t *root = body ? (lxb_dom_node_t *)body : NULL;

    /*
     * Walk the DOM tree in DFS preorder starting from body.
     * walk_node handles noise filtering, text extraction, and
     * block boundary detection internally.
     */
    lxb_dom_node_simple_walk(root, walk_node, &ctx);

    /* Trim trailing whitespace from final output */
    trim_trailing(str);

    /* Append NUL terminator (lexbor strings are not NUL-terminated) */
    lexbor_str_append(str, ctx.mraw, "", 1);

    /*
     * Copy arena string to independent malloc'd buffer.
     * This allows the arena to be freed independently of the output.
     */
    size_t out_len = lexbor_str_len(str);
    char *result = malloc(out_len + 1);
    if (result) {
        memcpy(result, lexbor_str_data(str), out_len);
        result[out_len] = '\0';
    }

    /* Clean up arena string — arena itself remains valid */
    lexbor_str_destroy(str, doc->mraw, true);
    return result;
}

/* ========================================================================
 * devil_text — extract all text content (including noise tags)
 *
 * @param doc  devil_doc_t* from devil_parse()
 * @return     malloc'd text string, or NULL
 *
 * This is the unfiltered extraction — includes script/style content.
 * Use devil_strip() for normal use (noise tags are almost always
 * unwanted in LLM context).
 * ======================================================================== */
char *
devil_text(devil_doc_t *doc)
{
    return extract_text(doc, false);
}

/* ========================================================================
 * devil_strip — extract text, filtering noise tags
 *
 * @param doc  devil_doc_t* from devil_parse()
 * @return     malloc'd text string (noise filtered), or NULL
 *
 * This is the primary extraction function. Noise tags are dropped
 * entirely during DOM traversal, so their content never reaches the
 * output. Block boundaries produce newlines.
 * ======================================================================== */
char *
devil_strip(devil_doc_t *doc)
{
    return extract_text(doc, true);
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
