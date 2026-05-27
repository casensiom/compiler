#include "tokenizer.h"

#define EXECUTE_UNIT_TEST
#include "test/tests.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/errno.h>
#include <stdarg.h>
#include <string.h>

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
    char *ret = malloc((len + 1) * sizeof(char *));
    memcpy(ret, str, len);
    ret[len] = '\0';
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

    return tok;
}

static void
token_full_dump(Token *t) {
    printf(" > FULL DUMP (%p): ", t->pos);
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
    printf("^ %s\n", msg);
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
    printf("^ %s\n", msg);
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
    cur->next = token_new(TKN_EOF, p, p, token_loc);
    return head.next;
}

static Token *
file_tokenize(const char *filename) {
    CharArray content = {0};
    if(read_file_content(filename, &content)) {
        return NULL;
    }

    File file = {.name = filename, .content = content.items, .content_size = content.count, .full_path = NULL};
    return tokenize(&file);
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
static Token *
macro_param_search_by_name(MacroArgArray *args, const char *val, TokenKind type) {
    Token tok = {.type = type, .pos = val, .len = strlen(val)};
    return macro_param_search(args, &tok);
}

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

MacroArgArray
macro_param_collect(Token **macro, Token **tok, const char *content) {
    MacroArgArray args = {0};
    Token        *it   = *tok;
    Token        *it2  = *macro;

    if(!token_equal(it, "(", TKN_PUNCTUATION) || !token_equal(it2, "(", TKN_PUNCTUATION)) {
        report_error_token(it2, content, "Function-like macro requires parameters.");
    }

    while(!token_equal(it, ")", TKN_PUNCTUATION) && !token_equal(it2, ")", TKN_PUNCTUATION)) {
        it  = it->next;
        it2 = it2->next;
        if(!it || it->type == TKN_EOF || !it2 || it2->type == TKN_EOF) {
            report_error_token(it, content, "Unexpected error parsing macro parameters.");
        }

        printf("Collecting macro parameters!\n");
        Token  param_macro;
        Token *copy_macro = &param_macro;
        while(!token_equal(it2, ",", TKN_PUNCTUATION) && !token_equal(it2, ")", TKN_PUNCTUATION)) {
            printf("  '%.*s'\n", (int)it2->len, it2->pos);
            copy_macro->next = token_copy(it2);
            copy_macro       = copy_macro->next;
            it2              = it2->next;
        }
        int force_rest = 0;
        if(token_equal(copy_macro, "...", TKN_PUNCTUATION)) {
            force_rest = 1;
        }
        // TODO: Check if macro still has params left and report error!

        printf("Collecting code parameters!\n");
        Token  param_code;
        Token *copy_code = &param_code;
        while((force_rest == 0 && !token_equal(it, ",", TKN_PUNCTUATION) && !token_equal(it, ")", TKN_PUNCTUATION)) ||
              (force_rest == 1 && !token_equal(it, ")", TKN_PUNCTUATION))) {
            printf("  '%.*s'\n", (int)it->len, it->pos);
            copy_code->next = token_copy(it);
            copy_code       = copy_code->next;
            it              = it->next;
        }

        // This may not be an error if next macro param is "..."
        if(!token_equals(it, it2) && !token_equal(it2->next, "...", TKN_PUNCTUATION)) {
            report_error_token(it, content, "Macro parameters doesn't match definition.");
        }

        MacroArg arg = {.macro = param_macro.next, .code = param_code.next};
        AC_ARRAY_PUSH(args, arg);
    }

    if((!token_equal(it, ")", TKN_PUNCTUATION) || !token_equal(it2, ")", TKN_PUNCTUATION))) {
        report_error_token(it, content, "Macro parameters doesn't match definition.");
    }

    *tok   = it->next;
    *macro = it2->next;
    return args;
}

MacroArgArray
macro_param_collect2(Token **macro, Token **tok, const char *content) {
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
        token_full_dump(param_code.next);

        if(!token_equal(it, ")", TKN_PUNCTUATION)) {
            it = it->next;
        }
    }
    *tok = it->next;

    return args;
}

static Token *
macro_expand(Token *macro, Token *tok, const char *content) {
    static size_t counter = 0;
    UNUSED(content);
    if(macro->has_spaces) {
        // object-like
        return token_merge(macro->next, tok->next);
    } else if(token_equal(macro->next, "(", TKN_PUNCTUATION)) {
        // function-like
        counter++;
        if(counter > 100) {
            report_error_token(tok, content, "Macro expand too many times, check for a loop.");
        }
        Token *m = macro->next;
        Token *c = tok->next;

        MacroArgArray args = macro_param_collect2(&m, &c, content);

        printf("\n\nFULL MACRO");
        token_full_dump(m);
        macro_param_dump(&args);
        printf("\n");

        Token  copy;
        Token *cur = &copy;
        while(m && m->type != TKN_EOF) {
            if(token_equal(m, "##", TKN_PUNCTUATION)) {
                m = m->next;
                continue;
            }
            if(token_equal(m, ",", TKN_PUNCTUATION) && token_equal(m->next, "##", TKN_PUNCTUATION) &&
               token_equal(m->next->next, "__VA_ARGS__", TKN_ID)) {
                Token *replacement = macro_param_search_by_name(&args, "...", TKN_PUNCTUATION);

                if(replacement == NULL) {
                    m = m->next->next;
                    continue;
                }
            }
            Token *replacement = macro_param_search(&args, m);
            if(replacement != NULL) {
                printf(" - REPLACE TOKEN '%.*s'", (int)m->len, m->pos);
                token_full_dump(replacement);
                cur->next = token_merge(replacement, NULL);
            } else {
                printf(" - COPY TOKEN '%.*s'", (int)m->len, m->pos);
                cur->next = token_copy(m);
            }
            m = m->next;
            while(cur->next != NULL) {
                cur = cur->next;
            }
            token_full_dump(copy.next);
        }
        cur->next = c;
        token_full_dump(copy.next);
        printf("\n");
        return copy.next;
    }
    return NULL;
}

Token *
preprocess(Token *tok, File *file, State *state) {
    /**
     * [ ] folder support on includes
     * [x] remove extra comma on empty variatic args
     * [ ] avoid replace self references in macros
     */

    Token *cur = tok;

    Token  copy = {};
    Token *ccur = &copy;

    while(cur && cur->type != TKN_EOF) {
        // token_dump(cur);
        if(token_equal(cur, "#", TKN_PUNCTUATION)) {
            Token *command = cur->next;
            // token_dump(command);

            if(token_equal(command, "include", TKN_ID)) {
                Token      *last             = NULL;
                Token      *open             = command->next;
                size_t      include_path_len = 0;
                const char *include_path     = open->pos;
                Token      *it               = open;
                while(!it->is_eol) {
                    include_path_len += it->len;
                    it = it->next;
                }
                include_path_len += it->len;
                last = it;

                printf("-> INCLUDE %.*s\n", (int)include_path_len, include_path);
                cur = last;

                int pos;
                AC_ARRAY_FIND(state->included, include_path, pos);
                if(pos != -1) {
                    printf("-> SKIP INCLUDE %.*s\n", (int)include_path_len, include_path);
                } else {
                    // TODO: Adjust line count reference
                    char  *filename       = copy_string(include_path + 1, include_path_len - 2);
                    Token *include_tokens = file_tokenize(filename);
                    if(include_tokens != NULL) {
                        Token *it = include_tokens;
                        while(it->next != NULL && it->next->type != TKN_EOF) {
                            it = it->next;
                        }
                        // TODO: remove EOF token
                        it->next = last->next;
                        cur      = include_tokens;
                        continue;
                    } else {
                        LOG_ERROR("Unable to parse include file: %.*s", (int)include_path_len, include_path);
                    }
                }

            } else if(token_equal(command, "define", TKN_ID)) {
                // TODO: Copy all macro token elements
                Token *name = command->next;
                if(name->type != TKN_ID) {
                    report_error_token(name, file->content, "Expected a MARO identifier.");
                }

                Token *it = cur;
                printf("-> DEFINED:");
                while(it->is_eol == 0) {
                    printf(" %.*s", (int)it->len, it->pos);
                    it = it->next;
                }
                printf(" %.*s", (int)it->len, it->pos);
                printf("\n");
                token_full_dump(cur);

                cur               = it->next;
                TokenLoc location = {.filename = file->name, .line = -1, .column = -1};
                it->next          = token_new(TKN_EOF, NULL, NULL, location);

                // TODO: Check if already exists and warn if needed
                AC_ARRAY_PUSH(state->macros, name);
                continue;
            } else if(token_equal(command, "if", TKN_ID) || token_equal(command, "ifdef", TKN_ID) ||
                      token_equal(command, "ifndef", TKN_ID)) {
                int skip_block = 0;

                if(token_equal(command, "if", TKN_ID)) {
                    Token *cond = command->next;
                    if(cond->type == TKN_NUMBER) {
                        if(atoi(cond->pos) == 0) {
                            skip_block = 1;
                        }
                    } else if(cond->type == TKN_ID) {
                        int pos = macro_search(state, cond);
                        if(pos != -1) {
                            Token *def = state->macros.items[pos];
                            if(!def->is_eol && def->next->type == TKN_NUMBER && atoi(def->next->pos) == 0) {
                                skip_block = 1;
                            }
                        }
                    }
                } else {
                    Token *def = command->next;
                    int    pos = macro_search(state, def);
                    if(token_equal(command, "ifdef", TKN_ID)) {
                        skip_block = (pos == -1);
                    } else {
                        skip_block = (pos != -1);
                    }
                }

                state->cond_block_level++;
                printf("COND BLOCK LEVEL UP: %lu\n", state->cond_block_level);
                if(skip_block) {
                    while(cur->type != TKN_EOF) {
                        cur = cur->next;
                        if(token_equal(cur, "#", TKN_PUNCTUATION) && token_equal(cur->next, "endif", TKN_ID)) {
                            state->cond_block_level--;
                            cur = cur->next;
                            break;
                        }
                    }
                } else {
                    cur = command->next;
                }
            } else if(token_equal(command, "elif", TKN_ID)) {
                TODO("Implement #elif");
            } else if(token_equal(command, "else", TKN_ID)) {
                TODO("Implement #else");
            } else if(token_equal(command, "endif", TKN_ID)) {
                cur = command;
                if(state->cond_block_level > 0) {
                    state->cond_block_level--;
                    printf("COND BLOCK LEVEL DOWN: %lu\n", state->cond_block_level);
                } else {
                    report_error_token(command, file->content, "No conditional block related with this close.");
                }
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
                report_error_token(command, file->content, "Preprocessor error.");
            } else {
                report_error_token(command, file->content, "Unknown preprocessor command.");
            }
        } else {
            int pos = macro_search(state, cur);
            if(pos != -1) {
                Token *macro    = state->macros.items[pos];
                Token *expanded = macro_expand(macro, cur, file->content);
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
        // Token *it = tok;
        // while(it != NULL) {
        //     printf("%.*s ", (int)it->len, it->pos);
        //     it = it->next;
        // }
        printf("\n----\n");

        // 2) Preprocess (Iterate over the tokens and manage the #<ident> items)

        State  state = {0};
        Token *out   = preprocess(tok, &file, &state);
        printf("[ TOKENS ]\n");
        token_full_dump(out);
        // it = out;
        // while(it != NULL) {
        //     printf("%.*s ", (int)it->len, it->pos);
        //     it = it->next;
        // }
        printf("\n----\n");
        // printf("[ MACROS ]\n");
        // for(size_t i = 0;i < state.macros.count;++i) {
        //     Token *t = state.macros.items[i];
        //     printf(" > %.*s\n", (int)t->len, t->pos);
        // }

        // 3) Generate code (Iterate over the tokens and generate ASM code for each)

        // TODO: Destroy tokens

        AC_ARRAY_DESTROY(content);
    }
    return 0;
}