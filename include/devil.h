/**
 * devil.h — Devil Extractor public API
 *
 * A blazing-fast C library that ingests raw HTML and outputs clean,
 * plain text formatted for LLM context windows.
 *
 * Pipeline: searchngx | devil | chat_agent
 *
 * Usage:
 *   devil_doc_t *doc = devil_parse(html, len);
 *   char *text = devil_strip(doc);
 *   // ... use text ...
 *   devil_free_string(text);
 *   devil_free(doc);
 */

#ifndef DEVIL_H
#define DEVIL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque handle to a parsed HTML document. */
typedef struct devil_doc devil_doc_t;

/**
 * Parse raw HTML into a devil document.
 *
 * @param html  UTF-8 HTML bytes.
 * @param len   Number of bytes in html.
 * @return      A devil_doc_t on success, NULL on failure.
 */
devil_doc_t *devil_parse(const uint8_t *html, size_t len);

/**
 * Extract all text content from the document, including whitespace
 * at block-level boundaries. This does NOT filter noise tags —
 * use devil_strip() for that.
 *
 * @param doc  A devil_doc_t from devil_parse().
 * @return     A malloc'd NUL-terminated string. Caller must free
 *             via devil_free_string(). Returns NULL on failure.
 */
char *devil_text(devil_doc_t *doc);

/**
 * Extract text content while filtering out noise tags
 * (script, style, nav, aside, footer, header, noscript, iframe,
 *  form, select, head, meta, link). Block-level boundaries
 * are marked with newlines to prevent LLM tokenizer poisoning.
 *
 * @param doc  A devil_doc_t from devil_parse().
 * @return     A malloc'd NUL-terminated string. Caller must free
 *             via devil_free_string(). Returns NULL on failure.
 */
char *devil_strip(devil_doc_t *doc);

/**
 * Free a string returned by devil_text() or devil_strip().
 *
 * @param str  The string to free. NULL is safe.
 */
void devil_free_string(char *str);

/**
 * Free all resources associated with a devil_doc_t.
 * Destroys the parser, document, and memory arena.
 *
 * @param doc  The devil_doc_t to free. NULL is safe.
 */
void devil_free(devil_doc_t *doc);

#ifdef __cplusplus
}
#endif

#endif /* DEVIL_H */
