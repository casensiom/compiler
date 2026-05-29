#include "tokenizer.h"

// #define MACRO_EXPAND_DEBUG
// #define MACRO_DEFINE_DEBUG

#define EXECUTE_UNIT_TEST
#include "test/tests.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/errno.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

TokenPtrArray gAllTokens;

Token *tokenize_file(const char *filename, State *state);

#define TRACE_ERROR(level, fmt, ...)                                \
    do {                                                            \
        log_msg_(__FILE__, __LINE__, 0, level, fmt, ##__VA_ARGS__); \
        abort();                                                    \
    } while(0)

#define FATAL(fmt, ...)       TRACE_ERROR("[FATAL]", fmt, ##__VA_ARGS__)
#define TODO(fmt, ...)        TRACE_ERROR("[TODO]", fmt, ##__VA_ARGS__)
#define UNREACHABLE(fmt, ...) TRACE_ERROR("[UNREACHABLE]", fmt, ##__VA_ARGS__)
#define UNUSED(var)           (void)var

#define LOG_DEBUG(fmt, ...) log_msg(stdout, "[DEBUG]", fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  log_msg(stdout, "[INFO]", fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  log_msg(stderr, "[WARN]", fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) log_msg(stderr, "[ERROR]", fmt, ##__VA_ARGS__)

char *
string_format(const char *fmt, ...) {
    char   *ret = NULL;
    va_list args;
    va_start(args, fmt);
    vasprintf(&ret, fmt, args);
    va_end(args);
    return ret;
}

void
log_msg(FILE *out, const char *level, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(out, "%s ", level);
    vfprintf(out, fmt, args);
    va_end(args);
    fprintf(out, "\n");
}

void
log_msg_(const char *file, int line, int column, const char *level, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "%s:%d:%d %s ", file, line, column, level);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

static int
read_file_content(const char *file_path, CharArray *content) {
    int   ret = 1;
    FILE *fp  = fopen(file_path, "r");
    if(!fp)
        goto finally;
    if(fseek(fp, 0, SEEK_END) < 0)
        goto finally;
    long count = ftell(fp);
    if(count < 0)
        goto finally;
    if(fseek(fp, 0, SEEK_SET) < 0)
        goto finally;
    AC_ARRAY_DESTROY(*content);
    *content = AC_ARRAY_CREATE(Char, count);

    long total = 0;
    do {
        long read = fread(content->items, 1, count, fp);
        if(read == 0 || ferror(fp)) {
            goto finally;
        }
        total += read;
    } while(total < count);
    content->count = count;

    ret = 0;
finally:
    if(ret > 0)
        LOG_ERROR("Can't read file '%s' (errno %d: %s)", file_path, errno, strerror(errno));
    if(fp)
        fclose(fp);
    return ret;
}

static const char *
shift(int *argc, const char ***argv) {
    if(*argc <= 0) {
        return NULL;
    }
    const char *ret = **argv;
    (*argv)++;
    (*argc)--;
    return ret;
}

static Args
parse_args(int argc, const char **argv) {
    Args args      = {0};
    args.file_list = AC_ARRAY_CREATE(ConstCharPtr, 16);

    const char *program = shift(&argc, &argv);
    UNUSED(program);

    while(1) {
        const char *param = shift(&argc, &argv);
        if(param == NULL) {
            break;
        }
        AC_ARRAY_PUSH(args.file_list, param);
    }
    return args;
}

static char *
copy_string(const char *str, size_t len) {
    char *ret = NULL;
    if(len > 0) {
        ret = malloc((len + 1) * sizeof(char *));
        memcpy(ret, str, len);
        ret[len] = '\0';
    }
    return ret;
}

static Token *
token_new(TokenKind type, const char *start, const char *end, TokenLoc location) {
    Token *tok      = calloc(1, sizeof(Token));
    tok->type       = type;
    tok->pos        = start;
    tok->len        = end - start;
    tok->is_eol     = 0;
    tok->has_spaces = 0;

    tok->location = location;

    tok->next = NULL;
    tok->str  = NULL;

    AC_ARRAY_PUSH(gAllTokens, tok);

    return tok;
}

static Token *
token_new_from_str(TokenKind type, const char *start, TokenLoc location) {
    size_t      len = strlen(start);
    const char *end = start + len;
    Token      *ret = token_new(type, start, end, location);
    ret->str        = start;
    return ret;
}

static void
token_delete(Token *tok) {
    while(tok != NULL) {
        size_t pos = -1;
        AC_ARRAY_FIND(gAllTokens, tok, pos);
        if(pos != (size_t)-1) {
            gAllTokens.items[pos] = NULL;
        }
        Token *next = tok->next;
        tok->pos    = NULL;
        tok->len    = 0;
        tok->type   = TKN_UNKNOWN;
        if(tok->str) {
            free((void *)tok->str);
        }
        tok->str = NULL;
        free(tok);
        tok = next;
    }
}

static void
token_full_dump(Token *t) {
    printf(" > ");
    while(t && t->type != TKN_EOF) {
        printf(" %.*s", (int)t->len, t->pos);
        t = t->next;
    }
    if(t == NULL) {
        printf("$");
    }
    printf("\n");
}

static Token *
token_copy(Token *src) {
    Token *tok = calloc(1, sizeof(Token));
    *tok       = *src;
    tok->next  = NULL;
    src->str   = NULL;    // last copy keeps str ownership
    return tok;
}

static int
token_equals(Token *l, Token *r) {
    return (l && r && l->type == r->type && l->len == r->len && strncmp(l->pos, r->pos, l->len) == 0);
}

static int
token_equal(Token *tok, const char *tag, TokenKind type) {
#if 0
    if(!tok) {
        printf("Invalid token pointer.\n");
    }
    if(type != tok->type) {
        printf("Token type doesn't match. [%d] vs [%d]\n", (int)type, (int)tok->type);
    }
    int l1 = strlen(tag);
    int l2 = tok->len;
    if(l1 != l2) {
        printf("Token lenght doesn't match. [%d] vs [%d]\n", (int)l1, (int)l2);
    }
    if(strncmp(tok->pos, tag, tok->len) != 0) {
        printf("Token name doesn't match. [%s] vs [%.*s]\n", tag, (int)tok->len, tok->pos);
    }
#endif
    return (tok && type == tok->type && strlen(tag) == tok->len && strncmp(tok->pos, tag, tok->len) == 0);
}

static Token *
token_merge(Token *t1, Token *t2) {
    Token  copy;
    Token *cur = &copy;

    if(t1 == NULL || t1->type == TKN_EOF) {
        return t2;
    }

    while(t1 && t1->type != TKN_EOF) {
        cur->next = token_copy(t1);
        t1        = t1->next;
        cur       = cur->next;
        if(t2) {
            cur->location = t2->location;
        }
    }
    cur->next = t2;

    return copy.next;
}

static int
is_space(char c) {
    return (c == ' ' || c == '\t');
}
static int
is_digit(char c) {
    return (c >= '0' && c <= '9');
}
static int
is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int
is_ident_start(char c) {
    return is_alpha(c) || c == '_';
}

static int
is_ident(char c) {
    return is_digit(c) || is_alpha(c) || c == '_';
}

static int
is_punctuation(const char *p) {
    // TODO: first chech punctuation combinations
    static char *kw[] = {
        "<<=", ">>=", "...", "==", "!=", "<=", ">=", "->", "+=", "-=", "*=", "/=",
        "++",  "--",  "%=",  "&=", "|=", "^=", "&&", "||", "<<", ">>", "##",
    };
    size_t N = sizeof(kw) / sizeof(*kw);
    for(size_t i = 0; i < N; ++i) {
        char  *q   = kw[i];
        size_t len = strlen(q);
        if(strncmp(p, q, len) == 0) {
            return len;
        }
    }

    char c = *p;
    if(c == '!' || c == '"' || c == '#' || c == '$' || c == '%' || c == '&' || c == '\'' || c == '(' || c == ')' || c == '*' || c == '+' ||
       c == ',' || c == '-' || c == '.' || c == '/' || c == ':' || c == ';' || c == '<' || c == '=' || c == '>' || c == '?' || c == '@' ||
       c == '[' || c == '\\' || c == ']' || c == '^' || c == '_' || c == '`' || c == '{' || c == '|' || c == '}' || c == '~')
        return 1;
    return 0;
}

static int
get_number_len(size_t num) {
    size_t len = 1;
    while(num >= 10) {
        num /= 10;
        len++;
    }
    return len;
}

static void
report_token(Token *tok, const char *start, const char *msg) {
    const char *s;

    s = tok->pos;
    while(s > start && *s != '\n') {
        s--;
    }
    const char *line_start = s + 1;

    s = tok->pos;
    while(*s != '\0' && *s != '\n') {
        s++;
    }
    const char *line_end = s - 1;

    int    len    = line_end - line_start + 1;
    size_t line   = tok->location.line + 1;
    size_t column = tok->location.column;

    log_msg_(tok->location.filename, line, column + 1, "error:", msg);
    printf(" %lu | %.*s\n", line, len, line_start);
    size_t line_counter_len = get_number_len(line);
    for(size_t i = 0; i < line_counter_len + 2; i++) {
        printf(" ");
    }
    printf("| ");
    for(size_t i = 0; i < column; i++) {
        printf(" ");
    }
    printf("^\n");
}

static void
report_msg(const char *filename, const char *start, const char *pos, const char *msg) {
    size_t      line = 1;
    const char *s    = start;
    while(s < pos) {
        if(*s == '\n') {
            line++;
        }
        s++;
    }

    s = pos;
    while(s > start && *s != '\n') {
        s--;
    }
    const char *line_start = s + 1;
    s                      = pos;
    while(s && *s != '\n') {
        s++;
    }
    const char *line_end = s - 1;

    int    len    = line_end - line_start;
    size_t column = pos - line_start;
    log_msg_(filename, line, column, "error:", msg);
    printf(" %lu | %.*s\n", line, len, line_start);
    size_t line_counter_len = get_number_len(line);
    for(size_t i = 0; i < line_counter_len + 2; i++) {
        printf(" ");
    }
    printf("| ");
    for(size_t i = 0; i < column; i++) {
        printf(" ");
    }
    printf("^\n");
}

static void
report_error_token(Token *tok, const char *start, const char *msg) {
    report_token(tok, start, msg);
    abort();
}

static void
report_error(const char *filename, const char *start, const char *pos, const char *msg) {
    report_msg(filename, start, pos, msg);
    abort();
}

static Token *
tokenize_number(const char *pos, TokenLoc token_loc, TokenizerError *error) {
    Token      *token = NULL;
    const char *p     = pos;
    error->pos        = p;
    error->message    = NULL;

    // ([0-9]+(\.[0-9]*)?|\.[0-9]+)[eE][+-]?[0-9]+
    if(is_digit(p[0]) || (p[0] == '.' && is_digit(p[1]))) {
        const char *q       = p;
        int         has_dot = 0;
        int         has_exp = 0;
        int         has_sig = 0;
        do {
            if(p[0] == '.') {
                if(has_dot) {
                    error->message = "Invalid dot in decimal number.";
                    break;
                }
                if(has_exp) {
                    error->message = "Invalid dot in exponent.";
                    break;
                }
                has_dot = 1;
            } else if(p[0] == 'e' || p[0] == 'E') {
                if(has_exp) {
                    error->message = "Invalid exponent in scientific notation";
                    break;
                }
                has_exp = 1;
                if(p[1] != '\0') {
                    p++;
                    if(p[0] == '+' || p[0] == '-') {
                        if(has_sig) {
                            error->pos     = p;
                            error->message = "Invalid sign in scientific notation";
                            break;
                        }
                        has_sig = 1;
                        if(p[1] == '\0') {
                            error->pos     = p + 1;
                            error->message = "Unexpected end of file after exponent sign.";
                            break;
                        } else if(!is_digit(p[1])) {
                            error->pos     = p + 1;
                            error->message = "Invalid character after exponent sign.";
                            break;
                        }
                    } else if(!is_digit(p[0])) {
                        error->pos     = p;
                        error->message = "Invalid character after exponent.";
                        break;
                    }
                } else {
                    error->pos     = p + 1;
                    error->message = "Unexpected end of file after exponent.";
                    break;
                }
            } else if(!is_digit(*p)) {
                break;
            }
            p++;
        } while(1);

        if(error->message == NULL) {
            token = token_new(TKN_NUMBER, q, p, token_loc);
        }
    }
    return token;
}

static int
ensure_string_len(const char *pos, size_t len) {
    size_t i = 0;
    while(pos[i] != '\0' && pos[i] != '"' && i < len) {
        i++;
    }
    return i == len;
}

static size_t
read_escape_sequence(const char *pos) {
    const char *p   = pos;
    size_t      len = 0;
    if(*p == '\\') {
        p++;
        if(*p == '\0') {
            return 0;
        }

        if(*p == 'n') {
            len = 2;
        } else if(*p == 't') {
            len = 2;
        } else if(*p == 'r') {
            len = 2;
        } else if(*p == '\\') {
            len = 2;
        } else if(*p == '"') {
            len = 2;
        } else if(*p == '\'') {
            len = 2;
        } else if(*p == '0') {
            len = 2;
        } else if(*p == 'x' && ensure_string_len(p, 1 + 2)) {
            len = 2 + 2;
        } else if(*p == 'u' && ensure_string_len(p, 1 + 4)) {
            len = 2 + 4;
        } else if(*p == 'U' && ensure_string_len(p, 1 + 8)) {
            len = 2 + 8;
        }
    }
    return len;
}

static Token *
tokenize_string(const char *pos, TokenLoc token_loc, TokenizerError *error) {
    Token      *token = NULL;
    const char *p     = pos;
    error->pos        = p;
    error->message    = NULL;

    int skip_len  = 0;
    int is_string = 0;
    if(*p == '"') {
        skip_len  = 1;
        is_string = 1;
    } else if(strncmp(p, "u8\"", 3) == 0) {
        skip_len  = 3;
        is_string = 1;
    } else if(strncmp(p, "u\"", 2) == 0) {
        skip_len  = 2;
        is_string = 1;
    } else if(strncmp(p, "L\"", 2) == 0) {
        skip_len  = 2;
        is_string = 1;
    } else if(strncmp(p, "U\"", 2) == 0) {
        skip_len  = 2;
        is_string = 1;
    }

    if(is_string == 1) {
        const char *q = p;
        p += skip_len;
        while(*p != '"') {
            if(*p == '\0') {
                error->pos     = p;
                error->message = "Unclosed string.";
                break;
            }
            if(*p == '\n' || *p == '\r') {
                error->pos     = p;
                error->message = "Break line inside string.";
                break;
            }
            if(p[0] == '\\' && p[1] == '\n') {
                p += 2;
                continue;
            }
            if(*p == '\\') {
                size_t len = read_escape_sequence(p);
                if(len > 0) {
                    p += len - 1;
                } else {
                    error->pos     = p;
                    error->message = string_format("Invalid escape senquence %d %d.", (int)p[0], (int)p[1]);
                    break;
                }
            }
            p++;
        }
        if(error->message == NULL) {
            p++;
            token = token_new(TKN_STRING, q, p, token_loc);
        }
    }
    return token;
}

static Token *
tokenize_literal(const char *pos, TokenLoc token_loc, TokenizerError *error) {
    Token      *token = NULL;
    const char *p     = pos;
    error->pos        = p;
    error->message    = NULL;

    if(*p == '\'') {
        const char *q = p;
        p++;
        while(*p != '\'') {
            if(*p == '\0') {
                error->pos     = p;
                error->message = "Unclosed literal.";
                break;
            }
            if(*p == '\n' || *p == '\r') {
                error->pos     = p;
                error->message = "Break line inside literal.";
                break;
            }
            if(*p == '\\') {
                size_t len = read_escape_sequence(p);
                if(len > 0) {
                    p += len - 1;
                } else {
                    error->pos     = p;
                    error->message = "Invalid escape senquence.";
                    break;
                }
            }
            p++;
        }
        if(error->message == NULL) {
            p++;
            token = token_new(TKN_NUMBER, q, p, token_loc);
        }
    }
    return token;
}

static Token *
tokenize(File *file) {
    // TODO: use utf8 to read characters
    Token    head      = {0};
    Token   *cur       = &head;
    TokenLoc token_loc = {0};

    token_loc.filename = file->name;
    const char *p      = file->content;
    while(*p) {
        if(strncmp(p, "//", 2) == 0) {
            p += 2;
            while(*p != 0 && *p != '\n') {
                p++;
            }
            continue;
        }

        if(strncmp(p, "/*", 2) == 0) {
            const char *q = p;
            p += 2;
            do {
                if(*p == '\0') {
                    report_error(file->name, file->content, q, "Unclosed block comment.");
                }
                if(*p == '\n') {
                    token_loc.line++;
                    token_loc.column = 0;
                }
                if(*p == '*') {
                    if(*(p + 1) != '\0' && *(p + 1) == '/') {
                        p += 2;
                        break;
                    }
                }
                p++;
            } while(1);
            continue;
        }

        if(is_space(*p)) {
            cur->has_spaces = 1;
            p++;
            token_loc.column++;
            continue;
        }

        // TODO: remove this check, that should be part of the pre cleaning
        if(p[0] == '\\' && p[1] == '\n') {
            p += 2;
            token_loc.column = 0;
            token_loc.line++;
            continue;
        }

        if(*p == '\n') {
            cur->is_eol = 1;
            p++;
            token_loc.column = 0;
            token_loc.line++;
            continue;
        }

        Token         *t     = NULL;
        TokenizerError error = {0};
        t                    = tokenize_number(p, token_loc, &error);
        if(t != NULL) {
            cur->next = t;
            cur       = cur->next;
            token_loc.column += cur->len;
            p += cur->len;
            continue;
        } else if(error.message != NULL) {
            report_error(file->name, file->content, error.pos, error.message);
        }

        t = tokenize_string(p, token_loc, &error);
        if(t != NULL) {
            cur->next = t;
            cur       = cur->next;
            token_loc.column += cur->len;
            p += cur->len;
            continue;
        } else if(error.message != NULL) {
            report_error(file->name, file->content, error.pos, error.message);
        }

        t = tokenize_literal(p, token_loc, &error);
        if(t != NULL) {
            cur->next = t;
            cur       = cur->next;
            token_loc.column += cur->len;
            p += cur->len;
            continue;
        } else if(error.message != NULL) {
            report_error(file->name, file->content, error.pos, error.message);
        }

        if(is_ident_start(*p)) {
            const char *q = p;
            p++;
            while(is_ident(*p)) {
                p++;
            }
            cur->next = token_new(TKN_ID, q, p, token_loc);
            cur       = cur->next;
            token_loc.column += cur->len;
            continue;
        }

        size_t len = is_punctuation(p);
        if(len > 0) {
            const char *q = p;
            p += (len);
            cur->next = token_new(TKN_PUNCTUATION, q, p, token_loc);
            cur       = cur->next;
            token_loc.column += cur->len;
            continue;
        }
        printf("Unexpected token '%c' -> %d | 0x%X\n", *p, (int)*p, (int)*p);
        report_error(file->name, file->content, p, "Unexpected token.");
    }
    cur->is_eol = 1;
    cur->next   = token_new(TKN_EOF, p, p, token_loc);
    return head.next;
}

int
macro_search(State *state, Token *def) {
    int pos = -1;
    for(size_t i = 0; i < (state->macros).count; i++) {
        if((state->macros).items[i]->len == def->len && strncmp(def->pos, ((state->macros).items[i]->pos), def->len) == 0) {
            pos = i;
            break;
        }
    }
    return pos;
}

static Token *
macro_param_search(MacroArgArray *args, Token *tok) {
    Token *ret = NULL;
    for(size_t i = 0; i < args->count; i++) {
        Token *m = args->items[i].macro;
        if(token_equals(m, tok)) {
            ret = args->items[i].code;
            break;
        } else if(token_equal(m, "...", TKN_PUNCTUATION) && token_equal(tok, "__VA_ARGS__", TKN_ID)) {
            ret = args->items[i].code;
            if(ret == NULL) {
                TokenLoc loc = {0};
                ret          = token_new(TKN_EOF, NULL, NULL, loc);
            }
            break;
        }
    }
    return ret;
}

#ifdef MACRO_EXPAND_DEBUG
static void
macro_param_dump(MacroArgArray *args) {
    for(size_t i = 0; i < args->count; i++) {
        Token *m = args->items[i].macro;
        printf(" - MACRO PARAM '%.*s'\n", (int)m->len, m->pos);

        Token *c = args->items[i].code;
        if(c == NULL) {
            printf("   - CODE PARAM > EMPTY\n");
        } else {
            printf("   - CODE PARAM ");
            token_full_dump(c);
        }
    }
}
#endif

MacroArgArray
macro_param_collect(Token **macro, Token **tok, const char *content) {
    MacroArgArray args = {0};
    Token        *it;

    if(!token_equal(*tok, "(", TKN_PUNCTUATION) || !token_equal(*macro, "(", TKN_PUNCTUATION)) {
        report_error_token(*macro, content, "Function-like macro requires parameters.");
    }

    // macro parameters
    it = *macro;
    while(!token_equal(it, ")", TKN_PUNCTUATION)) {
        it = it->next;
        if(token_equal(it, ",", TKN_PUNCTUATION) || token_equal(it, ")", TKN_PUNCTUATION)) {
            continue;
        }
        MacroArg arg = {.macro = token_copy(it), .code = NULL};
        AC_ARRAY_PUSH(args, arg);
    }
    *macro = it->next;

    // code parameters
    it             = (*tok)->next;
    int force_rest = 0;
    for(size_t i = 0; i < args.count; ++i) {
        if(force_rest != 0) {
            report_error_token(args.items[i].macro, content, "Macro can not define parameters after variable arguments.");
        }
        force_rest = token_equal(args.items[i].macro, "...", TKN_PUNCTUATION);
        if(force_rest && token_equal(it, ")", TKN_PUNCTUATION)) {
            continue;
        }
        Token  param_code = {0};
        Token *copy_code  = &param_code;
        while((!token_equal(it, ",", TKN_PUNCTUATION) || force_rest == 1) && !token_equal(it, ")", TKN_PUNCTUATION)) {
            copy_code->next = token_copy(it);
            copy_code       = copy_code->next;
            it              = it->next;
        }
        args.items[i].code = param_code.next;

        if(!token_equal(it, ")", TKN_PUNCTUATION)) {
            it = it->next;
        }
    }
    *tok = it->next;

    return args;
}

static Token *
macro_replace_object(State *state, Token *m, Token *tok) {
    Token *ret = NULL;

    if(strncmp(m->pos, "__FILE__", 8) == 0) {
        char *buff = string_format("\"%s\"", tok->location.filename);
        ret        = token_new_from_str(TKN_ID, buff, (TokenLoc){0});
    } else if(strncmp(m->pos, "__LINE__", 8) == 0) {
        char *buff = string_format("%d", tok->location.line + 1);
        ret        = token_new_from_str(TKN_NUMBER, buff, (TokenLoc){0});
    } else if(strncmp(m->pos, "__DATE__", 8) == 0) {
        size_t len = strlen(state->date);
        ret        = token_new(TKN_ID, state->date, state->date + len, (TokenLoc){0});
    } else if(strncmp(m->pos, "__TIME__", 8) == 0) {
        size_t len = strlen(state->time);
        ret        = token_new(TKN_ID, state->time, state->time + len, (TokenLoc){0});
    }

    if(ret != NULL) {
        ret->next = token_new(TKN_EOF, NULL, NULL, (TokenLoc){0});
    }

    return ret;
}

static Token *
macro_expand(State *state, Token *macro, Token *tok, const char *content) {
    // static size_t counter = 0;
    // counter++;
    // if(counter > 100) {
    //     report_error_token(tok, content, "Macro expand too many times, check for a loop.");
    // }

    if(macro->has_spaces == 0 && token_equal(macro->next, "(", TKN_PUNCTUATION)) {
        // function-like
        Token *m = macro->next;
        Token *c = tok->next;

        MacroArgArray args = macro_param_collect(&m, &c, content);

#ifdef MACRO_EXPAND_DEBUG
        printf("\n\nFULL MACRO");
        token_full_dump(m);
        macro_param_dump(&args);
        printf("\n");
#endif

        Token  copy;
        Token *cur = &copy;
        while(m && m->type != TKN_EOF) {
            if(token_equal(m, ",", TKN_PUNCTUATION) && token_equal(m->next, "##", TKN_PUNCTUATION) &&
               token_equal(m->next->next, "__VA_ARGS__", TKN_ID)) {
                Token  dots        = {.type = TKN_PUNCTUATION, .pos = "...", .len = 3, .next = NULL};
                Token *replacement = macro_param_search(&args, &dots);

                if(replacement == NULL) {
                    m = m->next->next;
                    continue;
                }
            }
            if(token_equal(m, "##", TKN_PUNCTUATION) && token_equal(m->next, "__VA_ARGS__", TKN_ID)) {
                m = m->next;
                continue;
            }
            if(m->type == TKN_ID && token_equal(m->next, "##", TKN_PUNCTUATION) && m->next->next->type == TKN_ID) {
                // TODO: Merge all tree tokens in one
                LOG_INFO("Add support to merge identifiers ('%.*s' '%.*s' '%.*s')\n", (int)m->len, m->pos, (int)m->next->len, m->next->pos,
                         (int)m->next->next->len, m->next->next->pos);
            }
            Token *replacement = macro_param_search(&args, m);
            if(replacement != NULL) {
#ifdef MACRO_EXPAND_DEBUG
                printf(" - REPLACE TOKEN '%.*s'", (int)m->len, m->pos);
                token_full_dump(replacement);
#endif
                replacement->location = tok->location;
                cur->next             = token_merge(replacement, NULL);
                if(replacement->type == TKN_EOF) {
                    token_delete(replacement);
                }
            } else {
#ifdef MACRO_EXPAND_DEBUG
                printf(" - COPY TOKEN '%.*s'", (int)m->len, m->pos);
#endif
                cur->next           = token_copy(m);
                cur->next->location = tok->location;
            }
            m = m->next;
            while(cur->next != NULL) {
                cur = cur->next;
            }
#ifdef MACRO_EXPAND_DEBUG
            token_full_dump(copy.next);
#endif
        }
        cur->next = c;
#ifdef MACRO_EXPAND_DEBUG
        token_full_dump(copy.next);
        printf("\n");
#endif
        return copy.next;
    } else {
        // object-like
        Token *ret = macro_replace_object(state, macro, tok);
        if(ret != NULL) {
            Token *back = token_merge(ret, tok->next);
            token_delete(ret);
            return back;
        }
        return token_merge(macro->next, tok->next);
    }
    return NULL;
}

static char *
file_exists(const char *path, const char *name) {
    size_t path_len = 0;
    size_t name_len = 0;
    if(path != NULL) {
        path_len = strlen(path);
    }
    if(name != NULL) {
        name_len = strlen(name);
    }

    if((path_len + name_len) == 0) {
        return NULL;
    }

    size_t size = path_len + name_len + 2;
    size_t len  = 0;
    char  *ptr  = (char *)malloc(size);
    for(size_t i = 0; i < path_len; ++i) {
        ptr[len++] = path[i];
    }
    if(path_len > 0) {
        ptr[len++] = '/';
    }
    for(size_t i = 0; i < name_len; ++i) {
        ptr[len++] = name[i];
    }
    ptr[len++] = '\0';

    // TODO: Try to open the file, if success return the string, return NULL otherwise

    return ptr;
}

Token *
preprocess(Token *tok, File *file, State *state) {
    /**
     * [ ] folder support on includes
     * [ ] avoid replace self references in macros
     */

    Token *cur = tok;

    Token  copy = {};
    Token *ccur = &copy;

    int ignore_and_skip = 0;
    int else_visited    = 0;
    while(cur && cur->type != TKN_EOF) {
        // printf(" -> %.*s %.*s\n", (int)cur->len, cur->pos, (int)cur->next->len, cur->next->pos);
        if(token_equal(cur, "#", TKN_PUNCTUATION)) {
            Token *command = cur->next;
            if(token_equal(command, "include", TKN_ID)) {
                Token *open = command->next;
                if(open->pos[0] != '\"' && open->pos[0] != '<') {
                    report_error_token(open, file->content, "Unrecognized include file delimiter.");
                }

                size_t      include_path_len = 0;
                const char *include_path     = open->pos;
                Token      *it               = open;
                while(!it->is_eol) {
                    include_path_len += it->len;
                    it = it->next;
                }
                include_path_len += it->len;
                Token *last = it;

                printf("-> INCLUDE %.*s\n", (int)include_path_len, include_path);

                int pos;
                AC_ARRAY_FIND(state->included, include_path, pos);
                if(pos != -1) {
                    printf("-> SKIP INCLUDE %.*s\n", (int)include_path_len, include_path);
                    cur = last->next;
                    continue;
                } else {
                    // TODO: Adjust line count reference
                    char *filename = copy_string(include_path + 1, include_path_len - 2);
                    if(filename == NULL) {
                        report_error_token(open, file->content, "empty filename.");
                    }

                    // Search paths for the current file
                    char *found_path = NULL;
                    if(include_path[0] == '\"') {
                        found_path = file_exists(file->full_path, filename);
                    }
                    if(include_path[0] == '<' || found_path == NULL) {
                        for(size_t i = 0; i < state->include_dirs.count && found_path == NULL; i++) {
                            found_path = file_exists(state->include_dirs.items[i], filename);
                        }
                    }
                    if(found_path == NULL) {
                        // report_error_token(open, file->content, "file not found.");
                        LOG_ERROR("file not found.");
                        cur = last->next;
                        continue;
                    }

                    Token *include_tokens = tokenize_file(found_path, state);
                    free(found_path);
                    if(include_tokens != NULL) {
                        Token *it = include_tokens;
                        while(it->next != NULL && it->next->type != TKN_EOF) {
                            it = it->next;
                        }

                        it->next   = last->next;
                        last->next = include_tokens;
                        cur        = include_tokens;
                        continue;
                    } else {
                        LOG_WARN("No tokens found on: %.*s", (int)include_path_len, include_path);
                        cur = last->next;
                        continue;
                    }
                }

            } else if(token_equal(command, "define", TKN_ID)) {
                if(command->next == NULL || command->next->type != TKN_ID) {
                    report_error_token(command->next, file->content, "Expected a MACRO identifier.");
                }

                Token  macro_copy = {0};
                Token *macro      = &macro_copy;
                Token *it         = command->next;
                while(it->next && it->is_eol == 0) {
                    macro->next     = token_copy(it);
                    macro           = macro->next;
                    macro->location = (TokenLoc){0};
                    it              = it->next;
                }
                macro->next     = token_copy(it);
                macro           = macro->next;
                macro->location = (TokenLoc){0};
                cur             = it->next;

                macro->next = token_new(TKN_EOF, NULL, NULL, (TokenLoc){0});
                AC_ARRAY_PUSH(state->macros, macro_copy.next);

#ifdef MACRO_DEFINE_DEBUG
                printf("-> DEFINED");
                token_full_dump(macro_copy.next);
#endif
                continue;
            } else if(token_equal(command, "if", TKN_ID) || token_equal(command, "ifdef", TKN_ID) ||
                      token_equal(command, "ifndef", TKN_ID) || token_equal(command, "elif", TKN_ID)) {
                int skip_block = 0;

                if(token_equal(command, "ifdef", TKN_ID) || token_equal(command, "ifndef", TKN_ID)) {
                    if(command->is_eol) {
                        report_error_token(command, file->content, "macro name missing");
                    }
                    skip_block = (macro_search(state, command->next) == -1) == token_equal(command, "ifdef", TKN_ID);
                } else if(token_equal(command, "if", TKN_ID) || token_equal(command, "elif", TKN_ID)) {
                    if(token_equal(command, "elif", TKN_ID) && state->cond_block_level == 0) {
                        report_error_token(command, file->content, "#elif without #if");
                    }
                    if(command->is_eol) {
                        report_error_token(command, file->content, "expected value in expression");
                    }
                    Token *cond = command->next;
                    if(cond->type == TKN_NUMBER) {
                        skip_block = (atoi(cond->pos) == 0);
                    } else if(cond->type == TKN_ID) {
                        int pos = macro_search(state, cond);
                        if(pos != -1) {
                            Token *def = state->macros.items[pos];
                            Token *n   = def->next;
                            // The only valid option is being defined with a non zero number value
                            skip_block = !(!def->is_eol && n && n->type == TKN_NUMBER && atoi(n->pos) != 0);
                        } else {
                            skip_block = 1;
                        }
                    }
                }

                if(strncmp(command->pos, "if", 2) == 0) {
                    state->cond_block_level++;
                    ignore_and_skip = 0;
                    else_visited    = 0;
                }

                if(skip_block || ignore_and_skip) {
                    Token *save = NULL;
                    while(cur && cur->type != TKN_EOF) {
                        cur = cur->next;
                        if(token_equal(cur, "#", TKN_PUNCTUATION)) {
                            save = cur;
                            break;
                        }
                    }
                    if(save != NULL) {
                        cur = save;
                        continue;
                    }
                } else {
                    cur             = command->next;
                    ignore_and_skip = 1;
                }
            } else if(token_equal(command, "else", TKN_ID)) {
                if(else_visited > 0) {
                    report_error_token(command, file->content, "#else after #else");
                }
                else_visited = 1;
                if(state->cond_block_level == 0) {
                    report_error_token(command, file->content, "#endif without #if");
                }
                if(ignore_and_skip) {
                    Token *save = NULL;
                    while(cur && cur->type != TKN_EOF) {
                        cur = cur->next;
                        if(token_equal(cur, "#", TKN_PUNCTUATION)) {
                            save = cur;
                            break;
                        }
                    }
                    if(save != NULL) {
                        cur = save;
                        continue;
                    }
                } else {
                    cur = command;
                }
            } else if(token_equal(command, "endif", TKN_ID)) {
                if(state->cond_block_level == 0) {
                    report_error_token(command, file->content, "#endif without #if");
                }
                ignore_and_skip = 0;
                state->cond_block_level--;
                cur = command;
            } else if(token_equal(command, "undef", TKN_ID)) {
                Token *name = command->next;
                if(name->type != TKN_ID || !name->is_eol) {
                    report_token(name->next, file->content, "extra tokens at end of #undef directive");
                }

                int pos = macro_search(state, name);
                if(pos != -1) {
                    AC_ARRAY_REMOVE(state->macros, pos);
                    // } else {
                    //     LOG_WARN("There is no macro defined as '%.*s'.", (int)name->len, name->pos);
                }
                cur = name;
            } else if(token_equal(command, "pragma", TKN_ID)) {
                TODO("Implement #pragma");
            } else if(token_equal(command, "line", TKN_ID)) {
                TODO("Implement #line");
            } else if(token_equal(command, "error", TKN_ID)) {
                char buffer[256] = {};
                if(!command->is_eol) {
                    Token *message = command->next;
                    size_t len     = message->len > 255 ? 255 : message->len;
                    for(size_t i = 0; i < len; i++) {
                        buffer[i] = message->pos[i];
                    }
                }
                report_error_token(command, file->content, (const char *)buffer);
            } else {
                report_error_token(command, file->content, "Unknown preprocessor command.");
            }
        } else {
            int pos = macro_search(state, cur);
            if(pos != -1) {
                Token *macro    = state->macros.items[pos];
                Token *expanded = macro_expand(state, macro, cur, file->content);
                if(expanded == NULL) {
                    report_error_token(cur, file->content, "Unable to expand macro.");
                }
                cur = expanded;
                continue;
            }
            ccur->next = token_copy(cur);
            ccur       = ccur->next;
        }
        cur = cur->next;
    }
    return copy.next;
}

Token *
tokenize_file_content(File *file, State *state) {
    Token *tok = tokenize(file);
    Token *ret = preprocess(tok, file, state);
    token_delete(tok);
    return ret;
}

Token *
tokenize_file(const char *filename, State *state) {
    CharArray content = {0};
    if(read_file_content(filename, &content)) {
        return NULL;
    }

    File file = {.name = filename, .content = content.items, .content_size = content.count, .full_path = NULL};
    return tokenize_file_content(&file, state);
}

void
init_state(State *state) {
    const char *macro_names = "\
        #define __FILE__\n\
        #define __LINE__\n\
        #define __DATE__\n\
        #define __TIME__\n\
        #define __STDC__ 1\n\
        #define __STDC_VERSION__ 199409L\n\
        #define __STDC_HOSTED__ 1\n";

    time_t       now          = time(NULL);
    struct tm   *tm           = localtime(&now);
    static char *month_name[] = {"Jan", "Feb", "Mar", "Apr", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    state->date               = string_format("\"%s %02d %d\"", month_name[tm->tm_mon], tm->tm_mday, tm->tm_year + 1900);
    state->time               = string_format("\"%02d:%02d:%02d\"", tm->tm_hour, tm->tm_min, tm->tm_sec);

    size_t len = strlen(macro_names);

    File   file   = {.name = "system", .content = macro_names, .content_size = len, .full_path = NULL};
    Token *tokens = tokenize_file_content(&file, state);
    token_delete(tokens);
}

int
main(int argc, const char **argv) {
#ifdef EXECUTE_UNIT_TEST
    run_tests();
#endif

    if(argc < 2) {
        LOG_ERROR("Invalid number of parameters: %lu", argc);
        return 1;
    }

    Args args = parse_args(argc, argv);
    for(size_t i = 0; i < args.file_list.count; ++i) {
        LOG_INFO("Param %lu) %s", i, args.file_list.items[i]);

        CharArray content = {0};
        if(read_file_content(args.file_list.items[i], &content)) {
            return 1;
        }

        // LOG_INFO("File content [%lu]:\n%s", content.count, content.items);

        // 0) Clean up code (windows CRLF, unicode symbols, escape sequences) [Optional]
        // 1) Tokenize (Create linked list of elements)
        File   file = {.name = args.file_list.items[i], .content = content.items, .content_size = content.count, .full_path = NULL};
        Token *tok  = tokenize(&file);
        if(tok == NULL)
            return 1;
        printf("[ TOKENS ]\n");
        token_full_dump(tok);
        printf("\n----\n");

        // 2) Preprocess (Iterate over the tokens and manage the #<ident> items)

        State state = {0};
        init_state(&state);
        Token *out = preprocess(tok, &file, &state);
        printf("[ TOKENS ]\n");
        token_full_dump(out);
        printf("\n----\n");

        // 3) Generate code (Iterate over the tokens and generate ASM code for each)

        // 4) Destroy tokens
        token_delete(tok);
        token_delete(out);

        for(size_t m = 0; m < state.macros.count; m++) {
            Token *tmp = state.macros.items[m];
            token_delete(tmp);
        }
        state.macros.count = 0;
        if(state.date) {
            free((void *)state.date);
        }
        if(state.time) {
            free((void *)state.time);
        }

        size_t zombies = 0;
        for(size_t i = 0; i < gAllTokens.count; i++) {
            Token *tmp = gAllTokens.items[i];
            if(tmp != NULL) {
                zombies++;
                LOG_WARN("ZOMBIE: %lu (%p) -> '%.*s' type: %d (pos: %p) [index: %lu]\n", zombies, tmp, (int)tmp->len, tmp->pos,
                         (int)tmp->type, tmp->pos, i);
            }
        }

        AC_ARRAY_DESTROY(content);
    }
    return 0;
}