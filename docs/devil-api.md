# Devil Extractor v2 Public API

A blazing-fast C library that ingests raw HTML and outputs clean,
plain text formatted for LLM context windows.

Pipeline: `searchngx | devil | chat_agent`

## Design goals

- Zero-copy where possible (lexbor arena-backed allocation)
- Noise filtering (script, style, nav, etc.) at walk time
- Block-boundary newlines to prevent LLM tokenizer poisoning
- Safe memory management (single free per parse)

## Usage

```c
devil_doc_t *doc = devil_parse(html, len);
char *text = devil_strip(doc);   // filters noise tags
// ... use text ...
devil_free_string(text);
devil_free(doc);
```

## Memory ownership

- `devil_parse()` returns a malloc'd devil_doc_t. The caller owns it.
- `devil_text()`/`devil_strip()` return malloc'd strings. Caller frees.
- `devil_free(doc)` releases ALL resources (parser + DOM + arena + struct).
- `devil_free_string(str)` is a thin wrapper around free().

## Thread safety

NOT thread-safe. Each devil_doc_t is single-threaded.

## API Reference

### devil_parse

```c
devil_doc_t *devil_parse(const uint8_t *html, size_t len);
```

Parse raw HTML into a devil document.

Creates a lexbor parser, initializes the memory arena (v3.x),
and parses the HTML into a DOM tree. The arena is shared between
the DOM and the parser, so both must outlive any subsequent calls.

**Parameters:**
- `html` — UTF-8 HTML bytes (may contain any content, including malformed HTML — lexbor is forgiving)
- `len` — Number of bytes in html. If 0, returns NULL.

**Returns:** A devil_doc_t on success, NULL on failure. On NULL, no resources are leaked.

**Example:**
```c
const char *html = "<p>Hello</p>";
devil_doc_t *doc = devil_parse((const uint8_t *)html, strlen(html));
if (!doc) { return; } // handle error
```

### devil_text

```c
char *devil_text(devil_doc_t *doc);
```

Extract all text content from the document, including noise tags.

Walks the DOM tree in DFS preorder, emitting text nodes verbatim
(with whitespace normalization) and injecting newlines at block-level
boundaries. Unlike devil_strip(), this does NOT filter noise tags —
script/style content will appear in the output.

Use this only when you need raw text including script/style content.
For normal use, prefer devil_strip().

**Parameters:** `doc` — A devil_doc_t from devil_parse(). Must not be NULL.

**Returns:** A malloc'd NUL-terminated string. Caller must free via devil_free_string().
Returns NULL if doc is NULL or the DOM has no body element.

### devil_strip

```c
char *devil_strip(devil_doc_t *doc);
```

Extract text content while filtering out noise tags.

This is the primary extraction function. It walks the DOM tree,
skipping subtrees rooted at noise tags and emitting text from all
others. Noise tags are dropped entirely (including all descendants).

**Noise tags filtered:** script, style, nav, aside, footer, header,
noscript, iframe, form, select, head, meta, link, button, embed,
object, source, track, map, base, area, marquee.

Block-level boundaries produce newlines to prevent concatenated text
("HelloWorld" from adjacent <p> tags). Whitespace is normalized:
newlines/tabs become single spaces, multiple spaces collapse to one.

**Parameters:** `doc` — A devil_doc_t from devil_parse(). Must not be NULL.

**Returns:** A malloc'd NUL-terminated string. Caller must free via devil_free_string().
Returns NULL on failure.

### devil_free_string

```c
void devil_free_string(char *str);
```

Free a string returned by devil_text() or devil_strip().

NULL is safe — this is a thin wrapper around free().

**Parameters:** `str` — The string to free. May be NULL.

### devil_free

```c
void devil_free(devil_doc_t *doc);
```

Free all resources associated with a devil_doc_t.

Destroys the lexbor parser, the DOM document, and the memory arena
in the correct order. Then frees the devil_doc_t struct itself.

**IMPORTANT:** After calling this, the devil_doc_t is invalid. Do not
access doc->doc, doc->parser, or doc->mraw.

NULL is safe — this is a no-op.

**Parameters:** `doc` — The devil_doc_t to free. May be NULL.

**Cleanup order (lexbor v3.x requirement):**
1. `lxb_html_parser_destroy(parser)` — releases parser internals
2. `lxb_html_document_destroy(doc)` — releases DOM tree
3. `lexbor_mraw_destroy(mraw, true)` — frees arena allocator
4. `free(doc)` — frees the wrapper struct
