/**
 * devil.c — Core Devil Extractor
 *
 * Parses HTML with lexbor v3.x, walks the DOM, extracts text with
 * block-boundary newlines. Strips noise tags (script, style, nav,
 * etc.) during the walk so they never reach the output.
 *
 * Pipeline: searchngx | devil | chat_agent
 * Output: plain text, LLM-tokenizer-friendly.
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

/* ----------------------------------------------------------------
 * Opaque document struct — defined here so devil.h can use opaque.
 * ---------------------------------------------------------------- */
struct devil_doc {
    lxb_html_document_t *doc;       /* parsed DOM document           */
    lexbor_mraw_t       *mraw;      /* memory arena (v3.x required)  */
    lxb_html_parser_t   *parser;    /* parser instance               */
};

/* ----------------------------------------------------------------
 * Noise tags — these subtrees are dropped entirely.
 * ---------------------------------------------------------------- */
#define NOISE_TAGS \
    XX(SCRIPT)  XX(STYLE)  XX(NAV)     XX(ASIDE)  XX(FOOTER) \
    XX(HEADER)  XX(NOSCRIPT) XX(IFRAME) XX(FORM)  XX(SELECT) \
    XX(HEAD)    XX(META)   XX(LINK)    XX(BUTTON) XX(EMBED) \
    XX(OBJECT)  XX(SOURCE) XX(TRACK)   XX(MAP)    XX(BASE)  \
    XX(AREA)    XX(MARQUEE)

/* ----------------------------------------------------------------
 * Block-level tags — when we cross a boundary into/out of these,
 * emit a newline.  Prevents "HelloWorld" from <p>Hello</p><p>World</p>.
 * ---------------------------------------------------------------- */
#define BLOCK_TAGS \
    XX(DIV)   XX(P)       XX(H1) XX(H2) XX(H3) XX(H4) XX(H5) XX(H6) \
    XX(UL)    XX(OL)      XX(LI) XX(DL) XX(DT) XX(DD) XX(FIGURE) \
    XX(FIGCAPTION) \
    XX(ARTICLE) XX(SECTION) XX(ADDRESS) \
    XX(BLOCKQUOTE) XX(PRE) XX(HR) XX(BR) XX(CAPTION) \
    XX(TABLE) XX(TH) XX(TD) XX(TR) XX(THEAD) XX(TBODY) XX(TFOOT)

/* ----------------------------------------------------------------
 * Context state for the walker callback.
 * ---------------------------------------------------------------- */
typedef struct {
    lexbor_str_t *out;        /* growing output buffer       */
    lexbor_mraw_t *mraw;      /* arena for string alloc      */
    bool has_output;           /* true if we've emitted text  */
    bool in_block;             /* true if last node was block */
} walk_ctx_t;

/* ----------------------------------------------------------------
 * Noise check — returns true if tag should be dropped entirely.
 * ---------------------------------------------------------------- */
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

/* ----------------------------------------------------------------
 * Block check — returns true if this tag causes a newline
 * boundary after its content.
 * ---------------------------------------------------------------- */
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

/* ----------------------------------------------------------------
 * Trim trailing whitespace (spaces, tabs, newlines) from str.
 * ---------------------------------------------------------------- */
static void
trim_trailing(lexbor_str_t *str)
{
    size_t len = lexbor_str_len(str);
    lxb_char_t *data = (lxb_char_t *)lexbor_str_data(str);
    while (len > 0 &&
           (data[len - 1] == ' ' || data[len - 1] == '\t' ||
            data[len - 1] == '\n' || data[len - 1] == '\r'))
    {
        len--;
    }
    /* Set the length field directly */
    str->length = len;
}

/* ----------------------------------------------------------------
 * Walk callback — called for every node in preorder DFS.
 *
 * Returns LEXBOR_ACTION_NEXT to skip children (for noise tags).
 * LEXBOR_ACTION_OK to process children normally.
 * LEXBOR_ACTION_STOP to stop walking entirely.
 * ---------------------------------------------------------------- */
static lexbor_action_t
walk_node(lxb_dom_node_t *node, void *arg)
{
    walk_ctx_t *ctx = (walk_ctx_t *)arg;

    lxb_dom_node_type_t ntype = lxb_dom_node_type(node);

    /* --- Noise filter: drop entire subtree --- */
    if (ntype == LXB_DOM_NODE_TYPE_ELEMENT) {
        lxb_tag_id_t tag = lxb_dom_node_tag_id(node);
        if (is_noise(tag)) {
            return LEXBOR_ACTION_NEXT; /* skip children */
        }
    }

    /* --- Text node: extract and append --- */
    if (ntype == LXB_DOM_NODE_TYPE_TEXT) {
        lxb_dom_character_data_t *cd = (lxb_dom_character_data_t *)node;
        lexbor_str_t *text = &cd->data;
        size_t raw_len = lexbor_str_len(text);

        if (raw_len == 0) {
            return LEXBOR_ACTION_OK;
        }

        const lxb_char_t *raw = (const lxb_char_t *)lexbor_str_data(text);

        /* Skip whitespace-only text nodes */
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

        /* Collapse internal whitespace: newlines/tabs -> space,
           collapse multiple spaces to one */
        bool first = true;
        for (size_t i = 0; i < raw_len; i++) {
            lxb_char_t c = raw[i];
            if (c == '\n' || c == '\r' || c == '\t') {
                c = ' ';
            }
            if (c == ' ') {
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

    /* --- Element node: check for block boundary --- */
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

/* ----------------------------------------------------------------
 * devil_parse — parse HTML into a devil document.
 * ---------------------------------------------------------------- */
devil_doc_t *
devil_parse(const uint8_t *html, size_t len)
{
    if (!html || len == 0) return NULL;

    /* Create memory arena — v3.x REQUIRES init after create */
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

/* ----------------------------------------------------------------
 * Internal: run the walk and return a string (malloc'd, caller frees).
 * ---------------------------------------------------------------- */
static char *
extract_text(devil_doc_t *doc, bool filter_noise)
{
    if (!doc || !doc->doc) return NULL;

    /* Create output string with the arena */
    lexbor_str_t *str = lexbor_str_create();
    if (!str) return NULL;

    /* Initialize with mraw — v3.x returns lxb_char_t* on success */
    if (!lexbor_str_init(str, doc->mraw, 256)) {
        lexbor_str_destroy(str, doc->mraw, true);
        return NULL;
    }

    /* Setup context */
    walk_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.out = str;
    ctx.mraw = doc->mraw;
    ctx.has_output = false;
    ctx.in_block = false;

    /* Walk the document starting from body.
     * lxb_dom_node_simple_walk(root, ...) visits all descendants of root
     * (starting from root->first_child internally), so we pass body itself. */
    lxb_dom_node_t *root = NULL;

    lxb_html_body_element_t *body = lxb_html_document_body_element(doc->doc);
    if (body) {
        root = (lxb_dom_node_t *)body;
    }

    /* Simple walk — our callback handles noise filtering internally */
    lxb_dom_node_simple_walk(root, walk_node, &ctx);

    /* Null-terminate the string */
    trim_trailing(str);
    /* Append NUL terminator */
    lexbor_str_append(str, ctx.mraw, "", 1);

    /* Copy to a separate malloc'd buffer */
    size_t out_len = lexbor_str_len(str);
    char *result = malloc(out_len + 1);
    if (result) {
        memcpy(result, lexbor_str_data(str), out_len);
        result[out_len] = '\0';
    }

    lexbor_str_destroy(str, doc->mraw, true);
    return result;
}

/* ----------------------------------------------------------------
 * devil_text — extract all text (including noise).
 * ---------------------------------------------------------------- */
char *
devil_text(devil_doc_t *doc)
{
    return extract_text(doc, false);
}

/* ----------------------------------------------------------------
 * devil_strip — extract text, filtering noise tags.
 * ---------------------------------------------------------------- */
char *
devil_strip(devil_doc_t *doc)
{
    return extract_text(doc, true);
}

/* ----------------------------------------------------------------
 * devil_free_string — free a result string.
 * ---------------------------------------------------------------- */
void
devil_free_string(char *str)
{
    free(str);
}

/* ----------------------------------------------------------------
 * devil_free — destroy all parser state.
 * ---------------------------------------------------------------- */
void
devil_free(devil_doc_t *doc)
{
    if (!doc) return;

    if (doc->parser) lxb_html_parser_destroy(doc->parser);
    if (doc->doc) lxb_html_document_destroy(doc->doc);
    if (doc->mraw) lexbor_mraw_destroy(doc->mraw, true);

    free(doc);
}
