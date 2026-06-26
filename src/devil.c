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

#include "filters/noise.c"
#include "filters/ad-block.c"

struct devil_doc {
    lxb_html_document_t *doc;
    lexbor_mraw_t       *mraw;
    lxb_html_parser_t   *parser;
};

static void
trim_trailing(lexbor_str_t *str)
{
    size_t len = lexbor_str_len(str);
    lxb_char_t *data = (lxb_char_t *)lexbor_str_data(str);
    while (len > 0 &&
           (data[len - 1] == ' ' || data[len - 1] == '\t' ||
            data[len - 1] == '\n' || data[len - 1] == '\r'))
        len--;
    str->length = len;
}

static lexbor_action_t
walk_node(lxb_dom_node_t *node, void *arg)
{
    walk_ctx_t *ctx = (walk_ctx_t *)arg;
    lxb_dom_node_type_t ntype = lxb_dom_node_type(node);

    if (ntype == LXB_DOM_NODE_TYPE_ELEMENT && ctx->pre_filter) {
        lxb_tag_id_t tag = lxb_dom_node_tag_id(node);
        if (ctx->pre_filter(node, tag, ctx))
            return LEXBOR_ACTION_NEXT;
    }

    if (ntype == LXB_DOM_NODE_TYPE_ELEMENT) {
        lxb_tag_id_t tag = lxb_dom_node_tag_id(node);
        if (_devil_is_noise(tag)) {
            ctx->skip = true;
            return LEXBOR_ACTION_NEXT;
        }
    }

    if (ntype == LXB_DOM_NODE_TYPE_TEXT) {
        lxb_dom_character_data_t *cd = (lxb_dom_character_data_t *)node;
        lexbor_str_t *text = &cd->data;
        size_t raw_len = lexbor_str_len(text);

        if (raw_len == 0)
            return LEXBOR_ACTION_OK;

        const lxb_char_t *raw = (const lxb_char_t *)lexbor_str_data(text);

        bool all_ws = true;
        for (size_t i = 0; i < raw_len; i++) {
            if (raw[i] != ' ' && raw[i] != '\t' && raw[i] != '\n' && raw[i] != '\r') {
                all_ws = false;
                break;
            }
        }
        if (all_ws)
            return LEXBOR_ACTION_OK;

        if (ctx->in_block && ctx->has_output) {
            lexbor_str_append(ctx->out, ctx->mraw, "\n", 1);
            ctx->in_block = false;
        }

        bool first = true;
        for (size_t i = 0; i < raw_len; i++) {
            lxb_char_t c = raw[i];
            if (c == '\n' || c == '\r' || c == '\t')
                c = ' ';
            if (c == ' ') {
                if (!first)
                    lexbor_str_append(ctx->out, ctx->mraw, " ", 1);
                first = false;
            } else {
                lexbor_str_append(ctx->out, ctx->mraw, (const char *)&c, 1);
                ctx->has_output = true;
                first = false;
            }
        }
        return LEXBOR_ACTION_OK;
    }

    if (ntype == LXB_DOM_NODE_TYPE_ELEMENT) {
        lxb_tag_id_t tag = lxb_dom_node_tag_id(node);
        if (_devil_is_block(tag)) {
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

devil_doc_t *
devil_parse(const uint8_t *html, size_t len)
{
    if (!html || len == 0) return NULL;

    lexbor_mraw_t *mraw = lexbor_mraw_create();
    if (!mraw) return NULL;
    lexbor_mraw_init(mraw, 4096);

    lxb_html_parser_t *parser = lxb_html_parser_create();
    if (!parser) {
        lexbor_mraw_destroy(mraw, true);
        return NULL;
    }
    lxb_html_parser_init(parser);

    lxb_html_document_t *doc = lxb_html_parse(parser,
        (const lxb_char_t *)html, (size_t)len);

    if (!doc) {
        lxb_html_parser_destroy(parser);
        lexbor_mraw_destroy(mraw, true);
        return NULL;
    }

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

static char *
extract_text(devil_doc_t *doc, bool filter_noise)
{
    if (!doc || !doc->doc) return NULL;

    lexbor_str_t *str = lexbor_str_create();
    if (!str) return NULL;

    if (!lexbor_str_init(str, doc->mraw, 256)) {
        lexbor_str_destroy(str, doc->mraw, true);
        return NULL;
    }

    walk_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.out = str;
    ctx.mraw = doc->mraw;
    ctx.has_output = false;
    ctx.in_block = false;

    if (filter_noise) {
#ifdef DEVIL_FILTER_NOISE
        noise_register(&ctx);
#endif
#ifdef DEVIL_FILTER_AD_BLOCK
        adblock_register(&ctx);
#endif
    }

    lxb_html_body_element_t *body = lxb_html_document_body_element(doc->doc);
    lxb_dom_node_t *root = body ? (lxb_dom_node_t *)body : NULL;

    lxb_dom_node_simple_walk(root, walk_node, &ctx);

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

char *
devil_text(devil_doc_t *doc)
{
    return extract_text(doc, false);
}

char *
devil_strip(devil_doc_t *doc)
{
    return extract_text(doc, true);
}

void
devil_free_string(char *str)
{
    free(str);
}

void
devil_free(devil_doc_t *doc)
{
    if (!doc) return;
    if (doc->parser) lxb_html_parser_destroy(doc->parser);
    if (doc->doc) lxb_html_document_destroy(doc->doc);
    if (doc->mraw) lexbor_mraw_destroy(doc->mraw, true);
    free(doc);
}
