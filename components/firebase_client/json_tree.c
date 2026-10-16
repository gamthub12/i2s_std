#include "json_tree.h"
#include <limits.h>
#include <string.h>

typedef struct {
    const char *json;
    size_t length;
    size_t pos;
    jt_token_t *tokens;
    int capacity;
    int used;
} parser_t;

static void spaces(parser_t *p)
{
    while (p->pos < p->length && (p->json[p->pos] == ' ' || p->json[p->pos] == '\r' ||
           p->json[p->pos] == '\n' || p->json[p->pos] == '\t')) ++p->pos;
}

static int value(parser_t *p, int depth);

static int string(parser_t *p)
{
    if (p->used >= p->capacity || p->json[p->pos++] != '"') return -1;
    int id = p->used++;
    jt_token_t *t = &p->tokens[id];
    t->type = JT_STRING;
    t->start = p->pos;
    while (p->pos < p->length) {
        unsigned char ch = p->json[p->pos++];
        if (ch == '"') {
            t->end = p->pos - 1;
            t->next = p->used;
            t->count = 0;
            return id;
        }
        if (ch < 32) return -1;
        if (ch == '\\') {
            if (p->pos >= p->length) return -1;
            ch = p->json[p->pos++];
            if (strchr("\"\\/bfnrtu", ch) == NULL) return -1;
            if (ch == 'u') {
                for (int i = 0; i < 4; ++i) {
                    if (p->pos >= p->length || !strchr("0123456789abcdefABCDEF", p->json[p->pos++]))
                        return -1;
                }
            }
        }
    }
    return -1;
}

static int value(parser_t *p, int depth)
{
    spaces(p);
    if (depth > 12 || p->pos >= p->length || p->used >= p->capacity) return -1;
    char ch = p->json[p->pos];
    if (ch == '"') return string(p);
    int id = p->used++;
    jt_token_t *t = &p->tokens[id];
    t->start = p->pos;
    t->count = 0;
    if (ch == '{' || ch == '[') {
        t->type = ch == '{' ? JT_OBJECT : JT_ARRAY;
        char closing = ch == '{' ? '}' : ']';
        ++p->pos;
        spaces(p);
        if (p->pos < p->length && p->json[p->pos] == closing) ++p->pos;
        else while (1) {
            if (ch == '{') {
                spaces(p);
                if (p->pos >= p->length || p->json[p->pos] != '"' || string(p) < 0)
                    return -1;
                spaces(p);
                if (p->pos >= p->length || p->json[p->pos++] != ':') return -1;
            }
            if (value(p, depth + 1) < 0) return -1;
            ++t->count;
            spaces(p);
            if (p->pos >= p->length) return -1;
            char sep = p->json[p->pos++];
            if (sep == closing) break;
            if (sep != ',') return -1;
        }
    } else {
        if (ch == 't' && p->length - p->pos >= 4 && !memcmp(p->json + p->pos, "true", 4)) {
            t->type = JT_BOOL; p->pos += 4;
        } else if (ch == 'f' && p->length - p->pos >= 5 && !memcmp(p->json + p->pos, "false", 5)) {
            t->type = JT_BOOL; p->pos += 5;
        } else if (ch == 'n' && p->length - p->pos >= 4 && !memcmp(p->json + p->pos, "null", 4)) {
            t->type = JT_NULL; p->pos += 4;
        } else {
            t->type = JT_NUMBER;
            if (ch == '-') ++p->pos;
            size_t digits = 0;
            while (p->pos < p->length && p->json[p->pos] >= '0' && p->json[p->pos] <= '9') {
                ++p->pos; ++digits;
            }
            if (!digits) return -1;
            if (p->pos < p->length && (p->json[p->pos] == '.' || p->json[p->pos] == 'e' ||
                                        p->json[p->pos] == 'E')) return -1;
        }
    }
    t->end = p->pos;
    t->next = p->used;
    return id;
}

int jt_parse(const char *json, size_t length, jt_token_t *tokens, int capacity)
{
    parser_t p = {.json = json, .length = length, .tokens = tokens, .capacity = capacity};
    if (!json || !tokens || capacity < 1 || value(&p, 0) != 0) return -1;
    spaces(&p);
    return p.pos == p.length ? p.used : -1;
}

int jt_field(const char *json, const jt_token_t *tokens, int object, const char *key)
{
    if (object < 0 || tokens[object].type != JT_OBJECT) return -1;
    int child = object + 1;
    size_t keylen = strlen(key);
    for (int i = 0; i < tokens[object].count; ++i) {
        const jt_token_t *k = &tokens[child];
        int val = k->next;
        if (k->type == JT_STRING && k->end - k->start == keylen &&
            !memcmp(json + k->start, key, keylen)) return val;
        child = tokens[val].next;
    }
    return -1;
}

int jt_array_item(const jt_token_t *tokens, int array, int index)
{
    if (array < 0 || tokens[array].type != JT_ARRAY || index < 0 || index >= tokens[array].count)
        return -1;
    int child = array + 1;
    while (index-- > 0) child = tokens[child].next;
    return child;
}

static int hex_digit(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

bool jt_string(const char *json, const jt_token_t *token, char *output, size_t capacity)
{
    if (!token || token->type != JT_STRING || !capacity) return false;
    size_t out = 0;
    for (int i = token->start; i < token->end; ++i) {
        unsigned char ch = json[i];
        if (ch == '\\') {
            ch = json[++i];
            switch (ch) {
            case 'n': ch = '\n'; break; case 'r': ch = '\r'; break;
            case 't': ch = '\t'; break; case 'b': ch = '\b'; break;
            case 'f': ch = '\f'; break;
            case 'u': {
                if (i + 4 >= token->end) return false;
                uint32_t code = 0;
                for (int k = 0; k < 4; ++k) code = (code << 4) | hex_digit(json[++i]);
                if (code >= 0xd800 && code <= 0xdbff) {
                    if (i + 6 >= token->end || json[i + 1] != '\\' || json[i + 2] != 'u') return false;
                    i += 2;
                    uint32_t low = 0;
                    for (int k = 0; k < 4; ++k) low = (low << 4) | hex_digit(json[++i]);
                    if (low < 0xdc00 || low > 0xdfff || out + 4 >= capacity) return false;
                    code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
                    output[out++] = 0xf0 | (code >> 18);
                    output[out++] = 0x80 | ((code >> 12) & 0x3f);
                    output[out++] = 0x80 | ((code >> 6) & 0x3f);
                    output[out++] = 0x80 | (code & 0x3f);
                } else if (code >= 0xdc00 && code <= 0xdfff) return false;
                else if (code >= 0x800) {
                    if (out + 3 >= capacity) return false;
                    output[out++] = 0xe0 | (code >> 12);
                    output[out++] = 0x80 | ((code >> 6) & 0x3f);
                    output[out++] = 0x80 | (code & 0x3f);
                } else if (code >= 0x80) {
                    if (out + 2 >= capacity) return false;
                    output[out++] = 0xc0 | (code >> 6);
                    output[out++] = 0x80 | (code & 0x3f);
                } else {
                    if (out + 1 >= capacity) return false;
                    output[out++] = code;
                }
                continue;
            }
            default: break;
            }
        }
        if (out + 1 >= capacity) return false;
        output[out++] = ch;
    }
    output[out] = 0;
    return true;
}

bool jt_int(const char *json, const jt_token_t *token, int64_t *value)
{
    if (!token || token->type != JT_NUMBER || !value) return false;
    bool negative = json[token->start] == '-';
    int i = token->start + negative;
    int64_t n = 0;
    for (; i < token->end; ++i) {
        int digit = json[i] - '0';
        if (digit < 0 || digit > 9 || n > (INT64_MAX - digit) / 10) return false;
        n = n * 10 + digit;
    }
    *value = negative ? -n : n;
    return true;
}

bool jt_bool(const char *json, const jt_token_t *token, bool *value)
{
    if (!token || token->type != JT_BOOL || !value) return false;
    *value = json[token->start] == 't';
    return true;
}
