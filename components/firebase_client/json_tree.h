#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { JT_OBJECT, JT_ARRAY, JT_STRING, JT_NUMBER, JT_BOOL, JT_NULL } jt_type_t;
typedef struct {
    jt_type_t type;
    int start;
    int end;
    int next;
    int count;
} jt_token_t;

int jt_parse(const char *json, size_t length, jt_token_t *tokens, int capacity);
int jt_field(const char *json, const jt_token_t *tokens, int object, const char *key);
int jt_array_item(const jt_token_t *tokens, int array, int index);
bool jt_string(const char *json, const jt_token_t *token, char *output, size_t capacity);
bool jt_int(const char *json, const jt_token_t *token, int64_t *value);
bool jt_bool(const char *json, const jt_token_t *token, bool *value);
