/* Lightweight JSON parser — standalone, no dependencies.
 * Handles: objects, arrays, strings, numbers, bools, null.
 * Supports escaped chars: \" \\ / \b \f \n \r \t \uXXXX.
 * Error codes: 0=ok, 1=invalid input, 2=out of memory.
 *
 * Ponytail: single-file implementation included inline in json-ld-filter.c
 * via #include. Zero overhead — compiled directly into the filter.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "json-parser.h"

/* ========================================================================
 * Internal helpers
 * ======================================================================== */

static inline bool json_isspace(char c) {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

static inline void json_skip_ws(json_parser_t *p) {
    while (p->pos < p->len && json_isspace(p->input[p->pos]))
        p->pos++;
}

static inline int json_peek(json_parser_t *p) {
    json_skip_ws(p);
    if (p->pos >= p->len) return -1;
    return (unsigned char)p->input[p->pos];
}

static inline void json_advance(json_parser_t *p) {
    p->pos++;
}

static inline int json_expect(json_parser_t *p, char expected) {
    if (p->pos >= p->len || p->input[p->pos] != expected) {
        p->error = 1;
        return 1;
    }
    p->pos++;
    return 0;
}

static json_value_t *json_parse_value(json_parser_t *p);

/* Parse an escaped char in a JSON string. Sets *out to the decoded char.
 * Returns 0 on success, 1 on error. */
static int json_parse_escape(json_parser_t *p, char *out) {
    if (p->pos >= p->len) return 1;
    char c = p->input[p->pos++];
    switch (c) {
        case '"': *out = '"'; return 0;
        case '\\': *out = '\\'; return 0;
        case '/': *out = '/'; return 0;
        case 'b': *out = '\b'; return 0;
        case 'f': *out = '\f'; return 0;
        case 'n': *out = '\n'; return 0;
        case 'r': *out = '\r'; return 0;
        case 't': *out = '\t'; return 0;
        case 'u': {
            /* Parse 4 hex digits */
            unsigned int codepoint = 0;
            for (int i = 0; i < 4; i++) {
                if (p->pos >= p->len) return 1;
                char h = p->input[p->pos++];
                codepoint <<= 4;
                if (h >= '0' && h <= '9') codepoint |= (h - '0');
                else if (h >= 'a' && h <= 'f') codepoint |= (h - 'a' + 10);
                else if (h >= 'A' && h <= 'F') codepoint |= (h - 'A' + 10);
                else return 1;
            }
            /* Handle surrogate pairs */
            if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                if (p->pos + 1 < p->len && p->input[p->pos] == '\\' && p->input[p->pos+1] == 'u') {
                    p->pos += 2;
                    unsigned int low = 0;
                    for (int i = 0; i < 4; i++) {
                        if (p->pos >= p->len) return 1;
                        char h = p->input[p->pos++];
                        low <<= 4;
                        if (h >= '0' && h <= '9') low |= (h - '0');
                        else if (h >= 'a' && h <= 'f') low |= (h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') low |= (h - 'A' + 10);
                        else return 1;
                    }
                    if (low >= 0xDC00 && low <= 0xDFFF) {
                        codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                    } else {
                        codepoint = 0xFFFD; /* replacement char */
                    }
                } else {
                    codepoint = 0xFFFD;
                }
            }
            /* Encode as UTF-8 */
            if (codepoint < 0x80) {
                *out = (char)codepoint;
            } else if (codepoint < 0x800) {
                out[0] = (char)(0xC0 | ((codepoint >> 6) & 0x1F));
                out[1] = (char)(0x80 | (codepoint & 0x3F));
                out[2] = '\0';
                *out = out[0]; /* return first byte, caller reads second */
                /* Special handling: caller needs to know multi-byte */
                return 2;
            } else {
                out[0] = (char)(0xE0 | ((codepoint >> 12) & 0x0F));
                out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                out[2] = (char)(0x80 | (codepoint & 0x3F));
                out[3] = '\0';
                *out = out[0];
                return 3;
            }
            return 0;
        }
        default: return 1;
    }
}

static json_value_t *json_parse_string(json_parser_t *p) {
    if (json_expect(p, '"')) return NULL;
    
    /* Calculate raw string length first */
    size_t raw_start = p->pos;
    while (p->pos < p->len && p->input[p->pos] != '"') {
        if (p->input[p->pos] == '\\') p->pos++; /* skip escaped char */
        p->pos++;
    }
    if (p->pos >= p->len) { p->error = 1; return NULL; }
    size_t raw_len = p->pos - raw_start;
    
    /* Decode into output buffer */
    char *out = malloc(raw_len * 4 + 1);
    if (!out) { p->error = 2; return NULL; }
    
    size_t out_pos = 0;
    size_t i = raw_start;
    while (i < p->pos) {
        if (p->input[i] == '\\') {
            char tmp[4];
            int multi = json_parse_escape(p, tmp);
            if (multi < 0) { free(out); return NULL; }
            for (int j = 0; j < multi; j++) {
                out[out_pos++] = tmp[j];
            }
        } else {
            out[out_pos++] = p->input[i];
            i++;
        }
    }
    
    /* Create json_value */
    json_value_t *val = calloc(1, sizeof(json_value_t));
    if (!val) { free(out); p->error = 2; return NULL; }
    val->type = JSON_TYPE_STRING;
    val->data.string.str = out;
    val->data.string.len = out_pos;
    return val;
}

static json_value_t *json_parse_number(json_parser_t *p) {
    size_t start = p->pos;
    
    /* Minus sign */
    if (p->input[p->pos] == '-') p->pos++;
    
    /* Integer part */
    if (p->pos < p->len && p->input[p->pos] == '0') {
        p->pos++;
    } else if (p->pos < p->len && p->input[p->pos] >= '1' && p->input[p->pos] <= '9') {
        while (p->pos < p->len && p->input[p->pos] >= '0' && p->input[p->pos] <= '9')
            p->pos++;
    } else {
        p->error = 1;
        return NULL;
    }
    
    /* Fractional part */
    if (p->pos < p->len && p->input[p->pos] == '.') {
        p->pos++;
        if (!(p->pos < p->len && p->input[p->pos] >= '0' && p->input[p->pos] <= '9')) {
            p->error = 1;
            return NULL;
        }
        while (p->pos < p->len && p->input[p->pos] >= '0' && p->input[p->pos] <= '9')
            p->pos++;
    }
    
    /* Exponent */
    if (p->pos < p->len && (p->input[p->pos] == 'e' || p->input[p->pos] == 'E')) {
        p->pos++;
        if (p->pos < p->len && (p->input[p->pos] == '+' || p->input[p->pos] == '-'))
            p->pos++;
        if (p->pos >= p->len || !(p->input[p->pos] >= '0' && p->input[p->pos] <= '9')) {
            p->error = 1;
            return NULL;
        }
        while (p->pos < p->len && p->input[p->pos] >= '0' && p->input[p->pos] <= '9')
            p->pos++;
    }
    
    char num_str[64];
    size_t num_len = p->pos - start;
    if (num_len >= sizeof(num_str)) num_len = sizeof(num_str) - 1;
    strncpy(num_str, p->input + start, num_len);
    num_str[num_len] = '\0';
    
    json_value_t *val = calloc(1, sizeof(json_value_t));
    if (!val) { p->error = 2; return NULL; }
    val->type = JSON_TYPE_NUMBER;
    val->data.num_val = atof(num_str);
    return val;
}

static json_value_t *json_parse_literal(json_parser_t *p, const char *literal, bool bool_val) {
    size_t len = strlen(literal);
    if (p->pos + len > p->len || strncmp(p->input + p->pos, literal, len) != 0) {
        p->error = 1;
        return NULL;
    }
    p->pos += len;
    
    json_value_t *val = calloc(1, sizeof(json_value_t));
    if (!val) { p->error = 2; return NULL; }
    val->type = JSON_TYPE_BOOL;
    val->data.bool_val = bool_val;
    return val;
}

static json_value_t *json_parse_object(json_parser_t *p) {
    if (json_expect(p, '{')) return NULL;
    
    json_value_t *val = calloc(1, sizeof(json_value_t));
    if (!val) { p->error = 2; return NULL; }
    val->type = JSON_TYPE_OBJECT;
    
    json_skip_ws(p);
    if (p->pos < p->len && p->input[p->pos] == '}') {
        p->pos++;
        return val;
    }
    
    size_t capacity = 4;
    json_pair_t *pairs = malloc(sizeof(json_pair_t) * capacity);
    if (!pairs) { p->error = 2; free(val); return NULL; }
    
    while (1) {
        json_skip_ws(p);
        
        json_value_t *key_val = json_parse_string(p);
        if (!key_val) { free(pairs); free(val); return NULL; }
        
        json_skip_ws(p);
        if (json_expect(p, ':')) {
            json_free(key_val);
            free(pairs);
            free(val);
            return NULL;
        }
        
        json_skip_ws(p);
        json_value_t *value = json_parse_value(p);
        if (!value) {
            json_free(key_val);
            free(pairs);
            free(val);
            return NULL;
        }
        
        /* Resize pairs array if needed */
        if (val->count == capacity) {
            capacity *= 2;
            json_pair_t *tmp = realloc(pairs, sizeof(json_pair_t) * capacity);
            if (!tmp) {
                json_free(key_val);
                json_free(value);
                free(pairs);
                free(val);
                p->error = 2;
                return NULL;
            }
            pairs = tmp;
        }
        
        /* Store key */
        pairs[val->count].key = key_val->data.string.str;
        pairs[val->count].value = value;
        val->count++;
    }
    
    val->data.object.pairs = pairs;
    val->data.object.count = val->count;
    return val;
}

static json_value_t *json_parse_array(json_parser_t *p) {
    if (json_expect(p, '[')) return NULL;
    
    json_value_t *val = calloc(1, sizeof(json_value_t));
    if (!val) { p->error = 2; return NULL; }
    val->type = JSON_TYPE_ARRAY;
    
    json_skip_ws(p);
    if (p->pos < p->len && p->input[p->pos] == ']') {
        p->pos++;
        return val;
    }
    
    size_t capacity = 4;
    json_value_t **items = malloc(sizeof(json_value_t*) * capacity);
    if (!items) { p->error = 2; free(val); return NULL; }
    
    while (1) {
        json_skip_ws(p);
        
        json_value_t *item = json_parse_value(p);
        if (!item) {
            free(items);
            free(val);
            return NULL;
        }
        
        if (val->count == capacity) {
            capacity *= 2;
            json_value_t **tmp = realloc(items, sizeof(json_value_t*) * capacity);
            if (!tmp) {
                json_free(item);
                free(items);
                free(val);
                p->error = 2;
                return NULL;
            }
            items = tmp;
        }
        
        items[val->count++] = item;
        
        json_skip_ws(p);
        if (p->pos >= p->len || p->input[p->pos] == ']') {
            p->pos++;
            val->data.array.items = items;
            return val;
        }
        
        if (json_expect(p, ',')) {
            /* If we're here, we expected comma but got something else */
            val->data.array.items = items;
            return val;
        }
    }
}

static json_value_t *json_parse_value(json_parser_t *p) {
    json_skip_ws(p);
    if (p->pos >= p->len) { p->error = 1; return NULL; }
    
    char c = p->input[p->pos];
    switch (c) {
        case '"': return json_parse_string(p);
        case '{': return json_parse_object(p);
        case '[': return json_parse_array(p);
        case 't': case 'f': return json_parse_literal(p, c == 't' ? "true" : "false", c == 't');
        case 'n': return json_parse_literal(p, "null", false);
        case '-': case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            return json_parse_number(p);
        default:
            p->error = 1;
            return NULL;
    }
}

/* ========================================================================
 * Public API
 * ======================================================================== */

int json_parser_init(json_parser_t *parser, const char *input, size_t len) {
    parser->input = input;
    parser->pos = 0;
    parser->len = len;
    parser->error = 0;
    return 0;
}

json_value_t *json_parser_parse(json_parser_t *parser) {
    return json_parse_value(parser);
}

int json_parser_error(json_parser_t *parser) {
    return parser->error;
}

void json_free(json_value_t *val) {
    if (!val) return;
    
    switch (val->type) {
        case JSON_TYPE_STRING:
            free(val->data.string.str);
            break;
        case JSON_TYPE_ARRAY:
            for (size_t i = 0; i < val->data.array.count; i++)
                json_free(val->data.array.items[i]);
            free(val->data.array.items);
            break;
        case JSON_TYPE_OBJECT:
            for (size_t i = 0; i < val->data.object.count; i++) {
                json_free(val->data.object.pairs[i].value);
                /* key is a pointer into the string value — don't free it */
            }
            free(val->data.object.pairs);
            break;
        default:
            break;
    }
    free(val);
}

/* ========================================================================
 * json_format_text — convert JSON to clean text for LLM consumption
 *
 * Ponytail: flat, readable format. No nested indentation — each key-value
 * pair on its own line. Arrays become numbered lists. Nested objects/arrays
 * are indented by 2 spaces per level.
 *
 * Returns malloc'd string. Caller must free.
 * ======================================================================== */

static void json_format_value(json_value_t *val, int indent, char *buf, size_t *pos, size_t *cap) {
    /* Expand buffer if needed */
    while (*pos + 256 >= *cap) {
        *cap *= 2;
        buf = realloc(buf, *cap);
    }
    
    switch (val->type) {
        case JSON_TYPE_NULL:
            snprintf(buf + *pos, *cap - *pos, "null");
            *pos += 4;
            break;
            
        case JSON_TYPE_BOOL:
            snprintf(buf + *pos, *cap - *pos, val->data.bool_val ? "true" : "false");
            *pos += val->data.bool_val ? 4 : 5;
            break;
            
        case JSON_TYPE_NUMBER: {
            /* Format as integer if whole number, else as float */
            double d = val->data.num_val;
            if (d == floor(d) && fabs(d) < 1e15) {
                snprintf(buf + *pos, *cap - *pos, "%.0f", d);
            } else {
                snprintf(buf + *pos, *cap - *pos, "%g", d);
            }
            *pos += strlen(buf + *pos);
            break;
        }
            
        case JSON_TYPE_STRING: {
            /* Escape special chars for text output */
            for (size_t i = 0; i < val->data.string.len; i++) {
                char c = val->data.string.str[i];
                if (c == '\\') {
                    snprintf(buf + *pos, *cap - *pos, "\\\\", 1);
                    *pos += 2;
                } else if (c == '"') {
                    snprintf(buf + *pos, *cap - *pos, "\\\"", 1);
                    *pos += 2;
                } else if (c == '\n') {
                    snprintf(buf + *pos, *cap - *pos, "\\n", 1);
                    *pos += 2;
                } else if (c == '\r') {
                    snprintf(buf + *pos, *cap - *pos, "\\r", 1);
                    *pos += 2;
                } else if (c == '\t') {
                    snprintf(buf + *pos, *cap - *pos, "\\t", 1);
                    *pos += 2;
                } else {
                    buf[*pos++] = c;
                }
            }
            break;
        }
            
        case JSON_TYPE_ARRAY: {
            if (val->data.array.count == 0) {
                snprintf(buf + *pos, *cap - *pos, "[]");
                *pos += 2;
                break;
            }
            
            for (size_t i = 0; i < val->data.array.count; i++) {
                if (i > 0) {
                    snprintf(buf + *pos, *cap - *pos, ", ");
                    *pos += 2;
                }
                json_format_value(val->data.array.items[i], indent + 1, buf, pos, cap);
            }
            break;
        }
            
        case JSON_TYPE_OBJECT: {
            if (val->data.object.count == 0) {
                snprintf(buf + *pos, *cap - *pos, "{}");
                *pos += 2;
                break;
            }
            
            for (size_t i = 0; i < val->data.object.count; i++) {
                if (i > 0) {
                    snprintf(buf + *pos, *cap - *pos, ", ");
                    *pos += 2;
                }
                /* Key */
                snprintf(buf + *pos, *cap - *pos, "%s", val->data.object.pairs[i].key);
                *pos += strlen(val->data.object.pairs[i].key);
                snprintf(buf + *pos, *cap - *pos, ": ");
                *pos += 2;
                /* Value */
                json_format_value(val->data.object.pairs[i].value, indent + 1, buf, pos, cap);
            }
            break;
        }
            
        default:
            snprintf(buf + *pos, *cap - *pos, "<unknown>");
            *pos += 9;
            break;
    }
}

char *json_format_text(json_value_t *val) {
    if (!val) return strdup("(null)");
    
    size_t cap = 16384;
    char *buf = malloc(cap);
    if (!buf) return strdup("(out of memory)");
    
    size_t pos = 0;
    json_format_value(val, 0, buf, &pos, &cap);
    buf[pos] = '\0';
    
    return buf;
}
