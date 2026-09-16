/* Lightweight JSON parser — standalone, no dependencies.
 * Handles: objects, arrays, strings, numbers, bools, null.
 * Supports escaped chars: \" \\ / \b \f \n \r \t \uXXXX.
 * Error codes: 0=ok, 1=invalid input, 2=out of memory.
 */

#ifndef DEVIL_JSON_PARSER_H
#define DEVIL_JSON_PARSER_H

#include <stddef.h>
#include <stdbool.h>

/* JSON value types */
#define JSON_TYPE_NULL     0
#define JSON_TYPE_BOOL     1
#define JSON_TYPE_NUMBER   2
#define JSON_TYPE_STRING   3
#define JSON_TYPE_ARRAY    4
#define JSON_TYPE_OBJECT   5

/* Forward declaration */
typedef struct json_value json_value_t;
typedef struct json_pair json_pair_t;

/* Key-value pair for objects */
struct json_pair {
    char *key;
    json_value_t *value;
};

/* JSON value */
struct json_value {
    int type;
    union {
        bool bool_val;
        double num_val;
        struct {
            char *str;
            size_t len;
        } string;
        struct {
            json_value_t **items;
            size_t count;
        } array;
        struct {
            json_pair_t *pairs;
            size_t count;
        } object;
    } data;
};

/* Parser state */
typedef struct {
    const char *input;
    size_t pos;
    size_t len;
    int error;  /* 0=ok, non-zero error code */
} json_parser_t;

/* API */
int json_parser_init(json_parser_t *parser, const char *input, size_t len);
json_value_t *json_parser_parse(json_parser_t *parser);
int json_parser_error(json_parser_t *parser);
void json_free(json_value_t *val);

/* Format a JSON value as clean text (for LLM consumption) */
char *json_format_text(json_value_t *val);

#endif /* DEVIL_JSON_PARSER_H */
