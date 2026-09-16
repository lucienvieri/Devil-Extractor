/* JSON-LD text extractor — standalone lightweight JSON parser + formatter.
 *
 * Extracts <script type="application/ld+json"> and
 * <script type="application/json"> content from raw HTML, parses the JSON,
 * and formats it as clean text for LLM consumption.
 *
 * Design decisions:
 * - Parsed from raw HTML string BEFORE DOM parsing (so JSON-LD survives
 *   even when <script> tags are filtered as noise)
 * - Formats nested objects/arrays as flat, readable text
 * - String keys become section headers, values follow
 * - Arrays become numbered lists
 * - No indentation levels — all flat for token efficiency
 *
 * Ponytail: single function. Zero dependencies on devil core.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ========================================================================
 * Internal state
 * ======================================================================== */

#define JSON_TYPE_NULL     0
#define JSON_TYPE_BOOL     1
#define JSON_TYPE_NUMBER   2
#define JSON_TYPE_STRING   3
#define JSON_TYPE_ARRAY    4
#define JSON_TYPE_OBJECT   5

typedef struct json_val json_val_t;
typedef struct json_kv json_kv_t;

struct json_kv {
    char *key;
    json_val_t *val;
};

struct json_val {
    int type;
    union {
        bool bv;
        double nv;
        struct { char *s; size_t len; } str;
        struct { json_val_t **items; size_t cnt; } arr;
        struct { json_kv_t *kvs; size_t cnt; } obj;
    } d;
};

typedef struct {
    const char *in;
    size_t pos;
    size_t len;
    int err;
} jpx;

/* ========================================================================
 * JSON parser
 * ======================================================================== */

static inline void jpx_ws(jpx *p) {
    while (p->pos < p->len &&
           (p->in[p->pos] == ' ' || p->in[p->pos] == '\n' ||
            p->in[p->pos] == '\r' || p->in[p->pos] == '\t'))
        p->pos++;
}

static inline int jpx_ch(jpx *p, char expected) {
    jpx_ws(p);
    if (p->pos >= p->len || p->in[p->pos] != expected) {
        p->err = 1;
        return 1;
    }
    p->pos++;
    return 0;
}

static json_val_t *jpx_str(jpx *p);

/* Decode one escaped char. Returns bytes written (1-4 for UTF-8). */
static int jpx_esc(jpx *p, char *out) {
    if (p->pos >= p->len) return -1;
    char c = p->in[p->pos++];
    switch (c) {
        case '"':  out[0]='"';  return 1;
        case '\\': out[0]='\\'; return 1;
        case '/':  out[0]='/';  return 1;
        case 'b':  out[0]='\b'; return 1;
        case 'f':  out[0]='\f'; return 1;
        case 'n':  out[0]='\n'; return 1;
        case 'r':  out[0]='\r'; return 1;
        case 't':  out[0]='\t'; return 1;
        case 'u': {
            unsigned cp = 0;
            for (int i = 0; i < 4; i++) {
                if (p->pos >= p->len) return -1;
                char h = p->in[p->pos++];
                cp <<= 4;
                if (h >= '0' && h <= '9') cp |= (h-'0');
                else if (h >= 'a' && h <= 'f') cp |= (h-'a'+10);
                else if (h >= 'A' && h <= 'F') cp |= (h-'A'+10);
                else return -1;
            }
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                if (p->pos+1 < p->len && p->in[p->pos]=='\\' && p->in[p->pos+1]=='u') {
                    p->pos += 2;
                    unsigned lo = 0;
                    for (int i = 0; i < 4; i++) {
                        if (p->pos >= p->len) return -1;
                        char h = p->in[p->pos++];
                        lo <<= 4;
                        if (h >= '0' && h <= '9') lo |= (h-'0');
                        else if (h >= 'a' && h <= 'f') lo |= (h-'a'+10);
                        else if (h >= 'A' && h <= 'F') lo |= (h-'A'+10);
                        else return -1;
                    }
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp-0xD800)<<10) + (lo-0xDC00);
                    } else { cp = 0xFFFD; }
                } else { cp = 0xFFFD; }
            }
            if (cp < 0x80) {
                out[0] = (char)cp;
                return 1;
            } else if (cp < 0x800) {
                out[0] = 0xC0|((cp>>6)&0x1F);
                out[1] = 0x80|(cp&0x3F);
                out[2] = 0;
                return 2;
            } else {
                out[0] = 0xE0|((cp>>12)&0x0F);
                out[1] = 0x80|((cp>>6)&0x3F);
                out[2] = 0x80|(cp&0x3F);
                out[3] = 0;
                return 3;
            }
        }
        default: return -1;
    }
}

static json_val_t *jpx_str(jpx *p) {
    if (jpx_ch(p, '"')) return NULL;
    size_t raw_start = p->pos;
    while (p->pos < p->len && p->in[p->pos] != '"') {
        if (p->in[p->pos] == '\\') p->pos++;
        p->pos++;
    }
    if (p->pos >= p->len) { p->err = 1; return NULL; }
    size_t raw_len = p->pos - raw_start;
    
    char *out = malloc(raw_len * 4 + 1);
    if (!out) { p->err = 2; return NULL; }
    size_t o = 0;
    for (size_t i = raw_start; i < p->pos; ) {
        if (p->in[i] == '\\') {
            char tmp[4];
            int n = jpx_esc(p, tmp);
            if (n < 0) { free(out); return NULL; }
            memcpy(out+o, tmp, n);
            o += n;
        } else {
            out[o++] = p->in[i];
            i++;
        }
    }
    json_val_t *v = calloc(1, sizeof(json_val_t));
    if (!v) { free(out); p->err = 2; return NULL; }
    v->type = JSON_TYPE_STRING;
    v->d.str.s = out;
    v->d.str.len = o;
    return v;
}

static json_val_t *jpx_val(jpx *p);
static void json_val_free(json_val_t *v);

static json_val_t *jpx_num(jpx *p) {
    size_t start = p->pos;
    if (p->pos < p->len && p->in[p->pos] == '-') p->pos++;
    if (p->pos < p->len && p->in[p->pos] == '0') {
        p->pos++;
    } else if (p->pos < p->len && p->in[p->pos] >= '1' && p->in[p->pos] <= '9') {
        while (p->pos < p->len && p->in[p->pos] >= '0' && p->in[p->pos] <= '9') p->pos++;
    } else { p->err = 1; return NULL; }
    if (p->pos < p->len && p->in[p->pos] == '.') {
        p->pos++;
        if (!(p->pos < p->len && p->in[p->pos] >= '0' && p->in[p->pos] <= '9')) {
            p->err = 1; return NULL;
        }
        while (p->pos < p->len && p->in[p->pos] >= '0' && p->in[p->pos] <= '9') p->pos++;
    }
    if (p->pos < p->len && (p->in[p->pos] == 'e' || p->in[p->pos] == 'E')) {
        p->pos++;
        if (p->pos < p->len && (p->in[p->pos] == '+' || p->in[p->pos] == '-')) p->pos++;
        if (p->pos >= p->len || p->in[p->pos] < '0' || p->in[p->pos] > '9') {
            p->err = 1; return NULL;
        }
        while (p->pos < p->len && p->in[p->pos] >= '0' && p->in[p->pos] <= '9') p->pos++;
    }
    char ns[64];
    size_t nl = p->pos - start;
    if (nl >= sizeof(ns)) nl = sizeof(ns)-1;
    memcpy(ns, p->in + start, nl);
    ns[nl] = '\0';
    
    json_val_t *v = calloc(1, sizeof(json_val_t));
    if (!v) { p->err = 2; return NULL; }
    v->type = JSON_TYPE_NUMBER;
    v->d.nv = atof(ns);
    return v;
}

static json_val_t *jpx_lit(jpx *p, const char *lit, bool bv) {
    size_t ln = strlen(lit);
    if (p->pos + ln > p->len || strncmp(p->in + p->pos, lit, ln) != 0) {
        p->err = 1; return NULL;
    }
    p->pos += ln;
    json_val_t *v = calloc(1, sizeof(json_val_t));
    if (!v) { p->err = 2; return NULL; }
    v->type = JSON_TYPE_BOOL;
    v->d.bv = bv;
    return v;
}

static json_val_t *jpx_obj(jpx *p) {
    if (jpx_ch(p, '{')) return NULL;
    json_val_t *v = calloc(1, sizeof(json_val_t));
    if (!v) { p->err = 2; return NULL; }
    v->type = JSON_TYPE_OBJECT;
    size_t cap = 4;
    json_kv_t *kvs = malloc(sizeof(json_kv_t) * cap);
    if (!kvs) { p->err = 2; free(v); return NULL; }
    
    while (1) {
        jpx_ws(p);
        if (p->pos < p->len && p->in[p->pos] == '}') { p->pos++; v->d.obj.kvs = kvs; return v; }
        json_val_t *kv = jpx_str(p);
        if (!kv) { free(kvs); free(v); return NULL; }
        if (jpx_ch(p, ':')) { json_val_free(kv); free(kvs); free(v); return NULL; }
        json_val_t *val = jpx_val(p);
        if (!val) { json_val_free(kv); free(kvs); free(v); return NULL; }
        
        if (v->d.obj.cnt == cap) {
            cap *= 2;
            json_kv_t *tmp = realloc(kvs, sizeof(json_kv_t) * cap);
            if (!tmp) { json_val_free(kv); json_val_free(val); free(kvs); free(v); p->err = 2; return NULL; }
            kvs = tmp;
        }
        kvs[v->d.obj.cnt].key = kv->d.str.s;
        kvs[v->d.obj.cnt].val = val;
        v->d.obj.cnt++;
    }
}

static json_val_t *jpx_arr(jpx *p) {
    if (jpx_ch(p, '[')) return NULL;
    json_val_t *v = calloc(1, sizeof(json_val_t));
    if (!v) { p->err = 2; return NULL; }
    v->type = JSON_TYPE_ARRAY;
    size_t cap = 4;
    json_val_t **items = malloc(sizeof(json_val_t*) * cap);
    if (!items) { p->err = 2; free(v); return NULL; }
    
    while (1) {
        jpx_ws(p);
        if (p->pos < p->len && p->in[p->pos] == ']') { p->pos++; v->d.arr.items = items; return v; }
        json_val_t *item = jpx_val(p);
        if (!item) { free(items); free(v); return NULL; }
        if (v->d.arr.cnt == cap) {
            cap *= 2;
            json_val_t **tmp = realloc(items, sizeof(json_val_t*) * cap);
            if (!tmp) { json_val_free(item); free(items); free(v); p->err = 2; return NULL; }
            items = tmp;
        }
        items[v->d.arr.cnt++] = item;
        
        jpx_ws(p);
        if (p->pos < p->len && p->in[p->pos] == ',') { p->pos++; continue; }
    }
}

static json_val_t *jpx_val(jpx *p) {
    jpx_ws(p);
    if (p->pos >= p->len) { p->err = 1; return NULL; }
    switch (p->in[p->pos]) {
        case '"': return jpx_str(p);
        case '{': return jpx_obj(p);
        case '[': return jpx_arr(p);
        case 't': case 'f': return jpx_lit(p, p->in[p->pos]=='t'?"true":"false", p->in[p->pos]=='t');
        case 'n': return jpx_lit(p, "null", false);
        case '-': case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            return jpx_num(p);
        default: p->err = 1; return NULL;
    }
}

/* ========================================================================
 * json_val_free — recursively free a parsed JSON value
 * ======================================================================== */
static void json_val_free(json_val_t *v) {
    if (!v) return;
    switch (v->type) {
        case JSON_TYPE_STRING: free(v->d.str.s); break;
        case JSON_TYPE_ARRAY:
            for (size_t i = 0; i < v->d.arr.cnt; i++) json_val_free(v->d.arr.items[i]);
            free(v->d.arr.items);
            break;
        case JSON_TYPE_OBJECT:
            for (size_t i = 0; i < v->d.obj.cnt; i++) json_val_free(v->d.obj.kvs[i].val);
            free(v->d.obj.kvs);
            break;
        default: break;
    }
    free(v);
}

/* ========================================================================
 * JSON formatter — clean text for LLM
 * ======================================================================== */

static void jfmt_val(json_val_t *v, char *buf, size_t *pos, size_t *cap);
static void jfmt_str_esc_by_ref(const char *s, size_t len, char *buf, size_t *pos, size_t *cap);

static void jfmt_str_esc(json_val_t *v, char *buf, size_t *pos, size_t *cap) {
    for (size_t i = 0; i < v->d.str.len; i++) {
        char c = v->d.str.s[i];
        if (c == '\\') {
            while (*pos + 3 >= *cap) { *cap *= 2; buf = realloc(buf, *cap); }
            buf[(*pos)++] = '\\';
            buf[(*pos)++] = '\\';
        } else if (c == '"') {
            while (*pos + 3 >= *cap) { *cap *= 2; buf = realloc(buf, *cap); }
            buf[(*pos)++] = '\\';
            buf[(*pos)++] = '"';
        } else if (c == '\n') {
            while (*pos + 3 >= *cap) { *cap *= 2; buf = realloc(buf, *cap); }
            buf[(*pos)++] = '\\';
            buf[(*pos)++] = 'n';
        } else if (c == '\r') {
            while (*pos + 3 >= *cap) { *cap *= 2; buf = realloc(buf, *cap); }
            buf[(*pos)++] = '\\';
            buf[(*pos)++] = 'r';
        } else if (c == '\t') {
            while (*pos + 3 >= *cap) { *cap *= 2; buf = realloc(buf, *cap); }
            buf[(*pos)++] = '\\';
            buf[(*pos)++] = 't';
        } else {
            while (*pos + 2 >= *cap) { *cap *= 2; buf = realloc(buf, *cap); }
            buf[(*pos)++] = c;
        }
    }
}

static void jfmt_val(json_val_t *v, char *buf, size_t *pos, size_t *cap) {
    switch (v->type) {
        case JSON_TYPE_NULL:
            while (*pos+5 >= *cap) { *cap *= 2; buf = realloc(buf, *cap); }
            memcpy(buf+*pos, "null", 4);
            *pos += 4;
            break;
        case JSON_TYPE_BOOL:
            while (*pos+6 >= *cap) { *cap *= 2; buf = realloc(buf, *cap); }
            memcpy(buf+*pos, v->d.bv ? "true" : "false", v->d.bv ? 4 : 5);
            *pos += v->d.bv ? 4 : 5;
            break;
        case JSON_TYPE_NUMBER: {
            double d = v->d.nv;
            char fmt[64];
            if (d == (double)((long long)d) && d >= -1e15 && d <= 1e15) {
                snprintf(fmt, sizeof(fmt), "%.0f", d);
            } else {
                snprintf(fmt, sizeof(fmt), "%g", d);
            }
            while (*pos + strlen(fmt) + 1 >= *cap) { *cap *= 2; buf = realloc(buf, *cap); }
            strcpy(buf+*pos, fmt);
            *pos += strlen(fmt);
            break;
        }
        case JSON_TYPE_STRING:
            jfmt_str_esc(v, buf, pos, cap);
            break;
        case JSON_TYPE_ARRAY: {
            for (size_t i = 0; i < v->d.arr.cnt; i++) {
                if (i > 0) {
                    while (*pos+3 >= *cap) { *cap *= 2; buf = realloc(buf, *cap); }
                    buf[(*pos)++] = ',';
                    buf[(*pos)++] = ' ';
                }
                jfmt_val(v->d.arr.items[i], buf, pos, cap);
            }
            break;
        }
        case JSON_TYPE_OBJECT: {
            for (size_t i = 0; i < v->d.obj.cnt; i++) {
                if (i > 0) {
                    while (*pos+3 >= *cap) { *cap *= 2; buf = realloc(buf, *cap); }
                    buf[(*pos)++] = ',';
                    buf[(*pos)++] = ' ';
                }
                /* key */
                jfmt_str_esc_by_ref(v->d.obj.kvs[i].key, strlen(v->d.obj.kvs[i].key), buf, pos, cap);
                while (*pos+3 >= *cap) { *cap *= 2; buf = realloc(buf, *cap); }
                buf[(*pos)++] = ':';
                buf[(*pos)++] = ' ';
                /* value */
                jfmt_val(v->d.obj.kvs[i].val, buf, pos, cap);
            }
            break;
        }
    }
}

static void jfmt_str_esc_by_ref(const char *s, size_t len, char *buf, size_t *pos, size_t *cap) {
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (c == '\\') {
            while (*pos+3 >= *cap) { *cap *= 2; buf = realloc(buf, *cap); }
            buf[(*pos)++] = '\\'; buf[(*pos)++] = '\\';
        } else if (c == '"') {
            while (*pos+3 >= *cap) { *cap *= 2; buf = realloc(buf, *cap); }
            buf[(*pos)++] = '\\'; buf[(*pos)++] = '"';
        } else {
            while (*pos+2 >= *cap) { *cap *= 2; buf = realloc(buf, *cap); }
            buf[(*pos)++] = c;
        }
    }
}

/* ========================================================================
 * JSON-LD extraction from raw HTML
 *
 * Searches for <script type="application/ld+json"> and
 * <script type="application/json"> tags, extracts their content,
 * parses the JSON, and formats it as clean text.
 *
 * @param html     Raw HTML string
 * @param html_len Length of HTML
 * @return         malloc'd formatted text string (caller frees), or NULL on error
 * ======================================================================== */
char *json_ld_extract(const char *html, size_t html_len) {
    /* Search for all JSON-LD script tags */
    static const char *patterns[] = {
        "application/ld+json",
        "application/json",
    };
    
    char *result = NULL;
    size_t result_len = 0;
    bool first = true;
    
    for (int p = 0; p < 2; p++) {
        const char *search = html;
        while (1) {
            const char *match = strstr(search, "application/ld+json");
            if (!match) match = strstr(search, "application/json");
            if (!match) break;
            search = match;
            
            /* Find the matching script tag */
            const char *tag_start = search;
            /* Look backward for <script */
            while (tag_start > html && tag_start[-1] != '<') tag_start--;
            
            if (tag_start <= html || strncasecmp(tag_start-1, "<script", 8) != 0) {
                search += 1;
                continue;
            }
            
            /* Find the closing > of the script tag */
            const char *tag_end = strchr(tag_start, '>');
            if (!tag_end) {
                search = tag_start + 1;
                continue;
            }
            
            /* Extract content between script tags */
            const char *content_start = tag_end + 1;
            const char *content_end = strstr(content_start, "</script>");
            if (!content_end) continue;
            
            size_t content_len = content_end - content_start;
            if (content_len == 0) {
                search = content_end + 1;
                continue;
            }
            
            /* Parse the JSON */
            jpx parser = { content_start, 0, content_len, 0 };
            json_val_t *json = jpx_val(&parser);
            if (!json || parser.err) {
                /* Cleanup any partially parsed value */
                if (json) {
                    if (json->type == JSON_TYPE_STRING) free(json->d.str.s);
                    if (json->type == JSON_TYPE_ARRAY) {
                        for (size_t i = 0; i < json->d.arr.cnt; i++) json_val_free(json->d.arr.items[i]);
                        free(json->d.arr.items);
                    }
                    if (json->type == JSON_TYPE_OBJECT) {
                        for (size_t i = 0; i < json->d.obj.cnt; i++) {
                            json_val_free(json->d.obj.kvs[i].val);
                        }
                        free(json->d.obj.kvs);
                    }
                    free(json);
                }
                search = content_end + 1;
                continue;
            }
            
            /* Format as text */
            size_t fmt_cap = 16384;
            char *fmt = malloc(fmt_cap);
            if (!fmt) { json_val_free(json); search = content_end + 1; continue; }
            size_t fmt_pos = 0;
            jfmt_val(json, fmt, &fmt_pos, &fmt_cap);
            json_val_free(json);
            fmt[fmt_pos] = '\0';
            
            if (fmt_pos > 0) {
                if (!first) {
                    /* Add separator */
                    size_t new_len = result_len + 4; /* "---\n" */
                    char *new_result = realloc(result, new_len);
                    if (new_result) {
                        result = new_result;
                        memcpy(result + result_len, "\n---\n", 4);
                        result_len = new_len - 1;
                    }
                }
                
                size_t new_len = result_len + fmt_pos;
                char *new_result = realloc(result, new_len + 1);
                if (new_result) {
                    memcpy(new_result + result_len, fmt, fmt_pos);
                    result = new_result;
                    result_len = new_len;
                    new_result[fmt_pos + result_len - fmt_pos] = '\0';
                }
                first = false;
            }
            free(fmt);
            
            search = content_end + 1;
        }
    }
    
    return result;
}
