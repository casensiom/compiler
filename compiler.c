static const char *macro_names = "#define __FILE__\n\
        #define __LINE__\n\
        #define __DATE__\n\
        #define __TIME__\n\
        #define __STDC__ 1\n\
        #define __STDC_VERSION__ 199409L\n\
        #define __STDC_HOSTED__ 1\n\
        #define __x86_64__ 1\n\
        #define __GNUC__ 4\n";

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/errno.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h> /* Windows: _getcwd */
#else
#include <unistd.h> /* POSIX: getcwd */
#endif

#include "array.h"

#define UNUSED(var) (void)var
#define LOG_DEBUG(fmt, ...) log_msg(stdout, "[DEBUG]", fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) log_msg(stdout, "[INFO]", fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) log_msg(stderr, "[WARN]", fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) log_msg(stderr, "[ERROR]", fmt, ##__VA_ARGS__)

#undef LOG_DEBUG
#define LOG_DEBUG(fmt, ...)

typedef char Char;
AC_ARRAY_DEFINE(Char);

typedef char *CharPtr;
AC_ARRAY_DEFINE(CharPtr);

typedef const char *ConstCharPtr;
AC_ARRAY_DEFINE(ConstCharPtr);

typedef struct Args {
    ConstCharPtrArray file_list;
    ConstCharPtrArray include_dir;
} Args;

typedef struct File {
    const char *filename;
    const char *fullpath;
    const char *content;
    size_t content_size;
    struct File *prev;
} File;
typedef File *FilePtr;
AC_ARRAY_DEFINE(FilePtr);

typedef enum TokenKind {
    TKN_UNKNOWN,
    TKN_ID,
    TKN_PUNCT,
    TKN_STR,
    TKN_NUM,
    TKN_EOF
} TokenKind;

typedef enum OpType {
    OP_SUM,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    // OP_BAND,
    // OP_BOR,
    // OP_AND,
    // OP_OR,
    OP_EQ
} OpType;

typedef struct TokenLocation {
    File *file;
    size_t line;
    size_t column;
} TokenLocation;

typedef struct Token {
    TokenKind type;
    TokenLocation location;
    const char *pos;
    size_t len;
    int is_eol;
    int has_spaces;

    struct Token *next;
    const char *str;
} Token;
typedef Token *TokenPtr;
AC_ARRAY_DEFINE(TokenPtr);

typedef struct ConditionScope {
    Token last;
    int valid_block;
    int else_visited;
    int permits_elif;
} ConditionScope;
AC_ARRAY_DEFINE(ConditionScope);

typedef struct Macro {
    int is_func;
    int is_variadic;
    Token *name;
    TokenPtrArray args;
    Token *body;
} Macro;
typedef Macro *MacroPtr;
AC_ARRAY_DEFINE(MacroPtr);

typedef struct CompilerInfo {
    const char *date;
    const char *time;
    const char *pwd;
    size_t system_path_idx;
} CompilerInfo;

typedef struct State {
    FilePtrArray included;
    ConstCharPtrArray search_paths;
    ConditionScopeArray cond_scopes;
    MacroPtrArray macros;
    TokenPtrArray tokens;
    CompilerInfo info;
} State;

typedef struct TokenizerError {
    const char *pos;
    const char *message;
} TokenizerError;

Token *process_file_content(State *state, File file);
Token *preprocess(State *state, Token *start);
static void file_release_content(File *file);
static void file_delete(File *file);
static int file_search(State *state, const char *file_path, File *file, int search_local);
static int file_read_content(File *file);

// -- Log --

static void log_msg(FILE *out, const char *level, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(out, "%s ", level);
    vfprintf(out, fmt, args);
    va_end(args);
    fprintf(out, "\n");
}

static void get_line_at(const char *content, const char *pos, const char **start, const char **end) {
    *start = NULL;
    *end = NULL;
    if (!content || !pos) {
        return;
    }
    const char *it = content;
    const char *line_start = it;
    while (it && *it != '\0' && it < pos) {
        if (*it == '\n') {
            line_start = it;
        }
        ++it;
    }
    while (it && *it != '\0' && *it != '\n') {
        ++it;
    }
    const char *line_end = it;

    if (line_start != content)
        line_start++;
    *start = line_start;
    *end = line_end;
}

static int get_number_len(size_t num) {
    size_t len = 1;
    while (num >= 10) {
        num /= 10;
        len++;
    }
    return len;
}

static void report_message_internal(TokenLocation loc, const char *pos, const char *msg, va_list args) {
    const char *line_start = NULL;
    const char *line_end = NULL;

    if (!loc.file || !pos) {
        LOG_ERROR("Invalid token location: '%s'", msg);
        abort();
    }

    get_line_at(loc.file->content, pos, &line_start, &line_end);
    int line_len = line_end - line_start;

#if 0
    LOG_DEBUG("POINTER: (%p) [%p <= %p <= %p] %.*s", loc.file->content, line_start, pos, line_end, line_len, line_start);
    LOG_DEBUG("MESSAGE: %s", message); LOG_DEBUG("LOCATION: line = %lu, column = %lu", loc.line, loc.column);
    LOG_DEBUG("LENGHT: %d", line_len);
    LOG_DEBUG("CONTENT: %s", loc.file->content);
#endif

    size_t column = loc.column + 1;
    printf("%s:%lu:%lu: ", loc.file->filename, loc.line + 1, column);
    vprintf(msg, args);
    printf("\n");
    printf(" %lu | %.*s\n", column, line_len, line_start);
    printf("%*s\n", get_number_len(column) + (int)column + 4, "^");
}

void report_message(TokenLocation loc, const char *pos, const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    report_message_internal(loc, pos, msg, args);
    va_end(args);
}

static void report_error(TokenLocation loc, const char *pos, const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    report_message_internal(loc, pos, msg, args);
    va_end(args);

    abort();
}

// -- String --

static char *string_copy(const char *str, size_t len) {
    char *ret = NULL;
    if (len > 0) {
        ret = malloc((len + 1) * sizeof(char *));
        memcpy(ret, str, len);
        ret[len] = '\0';
    }
    return ret;
}

static char *string_format(const char *fmt, ...) {
    char *ret = NULL;
    va_list args;
    va_start(args, fmt);
    vasprintf(&ret, fmt, args);
    va_end(args);
    return ret;
}

static int string_ensure_len(const char *pos, size_t len) {
    size_t i = 0;
    while (pos[i] != '\0' && pos[i] != '"' && i < len) {
        i++;
    }
    return i == len;
}

static const char *string_search_char(const char *str, char c, int occurrences) {
    const char *p = str;
    const char *last = NULL;

    while (p && *p != '\0' && occurrences != 0) {
        if (*p == c) {
            last = p;
            if (occurrences > 0)
                occurrences--;
        }
        p++;
    }

    return last;
}

// -- Path --

static char *path_current_dir() {
#ifdef _WIN32
    return _getcwd(NULL, 0);
#else
    return getcwd(NULL, 0);
#endif
}

static int path_is_absolute(const char *file_path) {
    UNUSED(file_path);
    if (!file_path) {
        return 0;
    }

#ifdef _WIN32
    return (file_path[0] >= 'A' && file_path[0] <= '>' && file_path[1] == ':');
#else
    return (file_path[0] == '/');
#endif

    return 0;
}

static char *path_join(const char *path, const char *file) {
    size_t len1 = strlen(path);
    size_t len2 = strlen(file);

    if (len1 == 0) {
        return strdup(file);
    } else if (len2 == 0) {
        return strdup(path);
    } else {
        if (path[len1] == '/') {
            return string_format("%s%s", path, file);
        } else {
            return string_format("%s/%s", path, file);
        }
    }

    return NULL;
}

// -- Token --

static Token *token_new(TokenKind type, const char *s, const char *e, TokenLocation loc) {
    Token *ret = (Token *)calloc(1, sizeof(Token));
    ret->type = type;
    ret->location = loc;
    ret->pos = s;
    ret->len = (size_t)(e - s);
    ret->is_eol = 0;
    ret->has_spaces = 0;
    ret->next = NULL;
    ret->str = NULL;

    return ret;
}

static Token *token_from_string(TokenKind type, const char *str, TokenLocation loc) {
    size_t len = strlen(str);
    Token *ret = token_new(type, str, str + len, loc);
    ret->str = str;
    return ret;
}

static Token *token_copy(Token *orig) {
    Token *ret = (Token *)calloc(1, sizeof(Token));
    *ret = *orig;
    ret->next = NULL;
    orig->str = NULL; // last copy keeps str ownership
    return ret;
}

static Token *token_clone(Token *orig) {
    Token copy = {0};
    Token *it = &copy;
    while (orig) {
        it = it->next = token_copy(orig);
        orig = orig->next;
    }
    return copy.next;
}

static Token *token_join(Token *left, Token *right) {
    Token *ret = (Token *)calloc(1, sizeof(Token));
    ret->next = NULL;
    ret->str = string_format("%.*s%.*s", (int)left->len, left->pos, (int)right->len, right->pos);
    ret->pos = ret->str;
    ret->len = strlen(ret->str); // left->len + right->len;
    ret->type = left->type;
    ret->location = left->location;
    ret->is_eol = right->is_eol;
    ret->has_spaces = right->has_spaces;
    return ret;
}

static Token *token_stringify(Token *orig) {
    Token *ret = (Token *)calloc(1, sizeof(Token));
    ret->next = NULL;
    ret->str = string_format("\"%.*s\"", (int)orig->len, orig->pos);
    ret->pos = ret->str;
    ret->len = strlen(ret->str); // orig->len + 2;
    ret->type = TKN_STR;
    ret->location = orig->location;
    ret->is_eol = orig->is_eol;
    ret->has_spaces = orig->has_spaces;
    return ret;
}

static void token_delete(Token *tok) {
    while (tok != NULL) {
        Token *next = tok->next;
        if (tok->str) {
            free((void *)tok->str);
        }
        tok->type = TKN_UNKNOWN;
        tok->location = (TokenLocation){0};
        tok->pos = NULL;
        tok->len = 0;
        tok->is_eol = 0;
        tok->has_spaces = 0;
        tok->next = NULL;
        tok->str = NULL;
        free(tok);
        tok = next;
    }
}

static int token_equals(const Token *l, const Token *r) {
    return (l->len == r->len && strncmp(l->pos, r->pos, r->len) == 0);
}

static int token_equal(Token *tok, const char *str) {
#if 0
    if (!tok) {
        printf("Invalid token pointer.\n");
    }
    if (!str) {
        printf("Invalid comparator label.\n");
    }
    int l1 = strlen(str);
    int l2 = tok->len;
    if (l1 != l2) {
        printf("Token lenght doesn't match. [%d] vs [%d]\n", (int)l1, (int)l2);
    }
    if (strncmp(tok->pos, str, tok->len) != 0) {
        printf("Token name doesn't match. [%s] vs [%.*s]\n", str, (int)tok->len, tok->pos);
    }
#endif
    return (tok && str && (strlen(str) == tok->len) && (strncmp(tok->pos, str, tok->len) == 0));
}

static void token_dump_full(Token *t) {
    printf(" > ");
    while (t && t->type != TKN_EOF) {
        printf(" %.*s", (int)t->len, t->pos);
        t = t->next;
    }
    if (t == NULL) {
        printf("$");
    }
    printf("\n");
}

static Token *token_copy_until_eol(Token **tok) {
    if (!tok) {
        return NULL;
    }

    Token head = {0};
    Token *cur = &head;
    while (*tok) {
        cur = cur->next = token_copy(*tok);
        if ((*tok)->is_eol) {
            break;
        }
        *tok = (*tok)->next;
    }

    cur = cur->next = token_new(TKN_EOF, NULL, NULL, (TokenLocation){0});
    return head.next;
}

static Token *token_last(Token *start) {
    Token *it = start;
    Token *found = NULL;
    while (it) {
        if (!it->next || it->type == TKN_EOF || (it->next && it->next->type == TKN_EOF)) {
            found = it;
            break;
        }
        it = it->next;
    }
    return found;
}

static Token *token_link(Token *start, Token *next) {
    Token *found = token_last(start);
    if (found) {
        token_delete(found->next);
        found->next = next;
    }
    return found;
}

// -- Macro --

static Macro *macro_new() {
    Macro *ret = (Macro *)calloc(1, sizeof(Macro));
    return ret;
}
static void macro_delete(Macro *macro) {
    if (!macro)
        return;

    macro->is_func = 0;
    macro->is_variadic = 0;

    token_delete(macro->name);
    macro->name = NULL;

    token_delete(macro->body);
    macro->body = NULL;

    AC_ARRAY_FOREACH(TokenPtr, it, macro->args) {
        token_delete(*it);
    }
    macro->args.count = 0;

    free(macro);
}

static Macro *macro_read(State *state, Token **tok) {
    UNUSED(state);
    // process macro
    Token *name = *tok;
    int is_func = !name->has_spaces && !name->is_eol && token_equal(name->next, "(");
    int is_variadic = 0;
    TokenPtrArray args = {0};
    Token *body = NULL;
    Token *before_body = NULL;
    int skip_body = 0;

    // collect macro arguments
    Token *it = name->next;
    while (is_func && it && it->type != TKN_EOF) {
        if (it->type == TKN_ID) {
            AC_ARRAY_PUSH(args, token_copy(it));
        } else if (token_equal(it, ")")) {
            skip_body = it->is_eol;
            before_body = it;
            it = it->next; // stop
            break;
        } else if (token_equal(it, "...")) {
            is_variadic = 1;
        } else if (token_equal(it, "(") || token_equal(it, ",")) {
            // skip
        } else {
            report_error(it->location, it->pos, "Expected identifier, found '%.*s'", (int)it->len, it->pos);
        }
        it = it->next;
    }

    // collect macro body
    if (name->is_eol) {
        it = name;
    } else if (is_func && skip_body) {
        it = before_body;
    } else {
        if (it && it->type != TKN_EOF) {
            body = token_copy_until_eol(&it);
        } else {
            report_error(name->location, name->pos, "Expected macro body.");
        }
    }

    Macro *m = macro_new();
    *m = (Macro){.name = token_copy(name), .is_func = is_func, .is_variadic = is_variadic, .args = args, .body = body};

    *tok = it;
    return m;
}

static int macro_search_index(State *state, Token *tok) {
    int ret = -1;
    AC_ARRAY_FOREACH(MacroPtr, it, state->macros) {
        if (token_equals((*it)->name, tok)) {
            ret = (int)(it - state->macros.items);
            break;
        }
    }
    return ret;
}

static Macro *macro_search(State *state, Token *tok) {
    Macro *m = NULL;
    int pos = macro_search_index(state, tok);
    if (pos != -1) {
        m = state->macros.items[pos];
    }
    return m;
}

static TokenPtrArray macro_collect_args(State *state, Macro *macro, Token **tok) {
    UNUSED(state);
    TokenPtrArray params = {0};

    if (!macro || !token_equals(*tok, macro->name)) {
        report_error((*tok)->location, (*tok)->pos, "Unexpected macro replacement.");
    }

    if (!macro->is_func) {
        return params;
    }

    if ((*tok)->type != TKN_ID) {
        report_error((*tok)->location, (*tok)->pos, "Expected macro name.");
    }
    *tok = (*tok)->next;
    if (!token_equal(*tok, "(")) {
        report_error((*tok)->location, (*tok)->pos, "Expected parentesis after macro name.");
    }
    *tok = (*tok)->next;
    int level = 1;

    Token copy = {0};
    Token *p = &copy;
    while (*tok && (*tok)->type != TKN_EOF) {
        if (token_equal(*tok, "(")) {
            level++;
        }
        if (token_equal(*tok, ")")) {
            level--;
            if (level == 0) {
                break;
            }
        }

        if (token_equal(*tok, ",")) {
            if (macro->is_variadic && params.count >= macro->args.count) {
                // fulfill the variadics
                p = p->next = token_copy(*tok);
            } else {
                AC_ARRAY_PUSH(params, copy.next); // TODO: should expand it
                copy.next = NULL;
                p = &copy;
            }
        } else {
            p = p->next = token_copy(*tok);
        }
        *tok = (*tok)->next;
    }
    if (copy.next) {
        AC_ARRAY_PUSH(params, copy.next); // TODO: should expand it
    }
    return params;
}

static int search_param(Token *t, TokenPtrArray *array) {
    int idx = -1;
    AC_ARRAY_FOREACH(TokenPtr, c, *array) {
        if (token_equals(t, *c)) {
            idx = (int)(c - array->items);
            break;
        }
    }
    return idx;
}

static Token *find_replacement(Macro *macro, TokenPtrArray *params, Token *it) {
    Token *ret;
    int pos = search_param(it, &(macro->args));
    if (pos != -1 && pos < (int)params->count) {
        ret = token_clone(params->items[pos]);
    } else {
        ret = token_copy(it);
    }
    return ret;
}

static Token *search_replacement(Macro *macro, TokenPtrArray *params, Token *it) {
    Token *ret;
    int pos = search_param(it, &(macro->args));
    if (pos != -1 && pos < (int)params->count) {
        ret = params->items[pos];
    } else {
        ret = it;
    }
    return ret;
}

static Token *macro_expand(State *state, Macro *macro, Token **tok) {
    if (!(*tok)) {
        report_error(macro->name->location, macro->name->pos, "Invalid macro usage.");
    }

    Token *it = *tok;
    if (token_equal(it, "__FILE__")) {
        return token_from_string(TKN_STR, string_format("\"%s\"", it->location.file->filename), it->location);
    } else if (token_equal(it, "__LINE__")) {
        return token_from_string(TKN_NUM, string_format("%lu", it->location.line), it->location);
    } else if (token_equal(it, "__COLUMN__")) {
        return token_from_string(TKN_NUM, string_format("%lu", it->location.column), it->location);
    } else if (token_equal(it, "__DATE__")) {
        return token_from_string(TKN_STR, string_format("\"%s\"", state->info.date), it->location);
    } else if (token_equal(it, "__TIME__")) {
        return token_from_string(TKN_STR, string_format("\"%s\"", state->info.time), it->location);
    }

    Token head = {0};
    Token *copy = &head;
    TokenPtrArray params = macro_collect_args(state, macro, tok);

    it = macro->body;
    while (it) {
        if (it->type == TKN_EOF) {
            break;
        }

        Token *candidate = NULL;
        if (token_equal(it, "#")) {
            it = it->next;
            Token *expanded = find_replacement(macro, &params, it);
            candidate = token_stringify(expanded);
            token_delete(expanded);
            // if (token_equals(candidate, it)) {
            //     report_error(it->location, it->pos, "Unexpected stringify token, found '%.*s', expected definition identifier.", (int)it->len, it->pos);
            // }
        } else if (token_equal(it->next, "##")) {
            if (token_equal(it, ",") && token_equal(it->next->next, "__VA_ARGS__")) {
                if (params.count > macro->args.count) {
                    Token *first = token_copy(it);
                    first->next = token_clone(params.items[macro->args.count]);
                    candidate = first;
                    it = it->next->next;
                } else {
                    it = it->next->next->next;
                    continue;
                }
            } else {
                if (it->type == TKN_PUNCT || it->next->next->type == TKN_PUNCT) {
                    report_error(it->next->location, it->next->pos, "Invalid join command.");
                }
                Token *left = search_replacement(macro, &params, it);
                Token *right = search_replacement(macro, &params, it->next->next);
                candidate = token_join(left, right);
                it = it->next->next;
            }
        } else if (token_equal(it, "__VA_ARGS__")) {
            if (params.count > macro->args.count) {
                candidate = token_clone(params.items[macro->args.count]);
            }
        } else if (it->type == TKN_ID) {
            candidate = find_replacement(macro, &params, it);
        } else {
            candidate = token_copy(it);
        }

        copy->next = candidate;
        while (copy->next) {
            copy = copy->next;
        }

        it = it->next;
    }

    AC_ARRAY_FOREACH(TokenPtr, c, params) {
        token_delete(*c);
    }
    AC_ARRAY_DESTROY(params);

    return head.next;
}

// -- State --

static void state_init(State *state) {

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    static char *month_name[] = {"Jan", "Feb", "Mar", "Apr", "Jun", "Jul",
                                 "Aug", "Sep", "Oct", "Nov", "Dec"};
    state->info.date = string_format("\"%s %02d %d\"", month_name[tm->tm_mon], tm->tm_mday, tm->tm_year + 1900);
    state->info.time = string_format("\"%02d:%02d:%02d\"", tm->tm_hour, tm->tm_min, tm->tm_sec);
    state->info.pwd = path_current_dir();

    size_t len = strlen(macro_names);
    const char *filename = strdup(__FILE__);
    File file = {.content = strdup(macro_names),
                 .content_size = len,
                 .fullpath = filename,
                 .filename = filename,
                 .prev = NULL};
    Token *tokens = process_file_content(state, file);
    token_delete(tokens);

    // WARN: Remove these system paths, or find them in a better way
    AC_ARRAY_PUSH(state->search_paths, "/usr/include");
    AC_ARRAY_PUSH(state->search_paths, "/usr/local/include");
    AC_ARRAY_PUSH(state->search_paths, "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include");
    AC_ARRAY_PUSH(state->search_paths, state->info.pwd);
    state->info.system_path_idx = state->search_paths.count;
}

static void state_cleanup(State *state) {
    AC_ARRAY_FOREACH(FilePtr, it, state->included) {
        file_delete(*it);
    }
    AC_ARRAY_FOREACH(MacroPtr, it, state->macros) {
        macro_delete(*it);
    }
    AC_ARRAY_FOREACH(TokenPtr, it, state->tokens) {
        token_delete(*it);
    }
    AC_ARRAY_FOREACH(ConstCharPtr, it, state->search_paths) {
        *it = NULL;
    }
    state->included.count = 0;
    state->cond_scopes.count = 0;
    state->macros.count = 0;
    state->tokens.count = 0;
    state->search_paths.count = 0;

    if (state->info.date)
        free((void *)state->info.date);
    if (state->info.time)
        free((void *)state->info.time);
    if (state->info.pwd)
        free((void *)state->info.pwd);
    state->info.date = NULL;
    state->info.time = NULL;
    state->info.pwd = NULL;
    state->info.system_path_idx = 0;
}

void state_dump(State *state) {
    AC_ARRAY_FOREACH(MacroPtr, it, state->macros) {
        int idx = (int)(it - state->macros.items);
        LOG_INFO("Macro %d) '%.*s'", idx, (int)(*it)->name->len, (*it)->name->pos);
        if ((*it)->args.count > 0) {
            LOG_INFO("    ARGS:");
            AC_ARRAY_FOREACH(TokenPtr, a, (*it)->args) {
                LOG_INFO("    - '%.*s'", (int)(*a)->len, (*a)->pos);
            }
        }

        LOG_INFO("    BODY:");
        token_dump_full((*it)->body);
    }
}

// -- Tokenizer --

static size_t read_escape_sequence(const char *pos) {
    const char *p = pos;
    size_t len = 0;
    if (*p != '\\') {
        return len;
    }
    p++;
    if (*p == '\0')
        return 0;

    if (*p == 'n') {
        len = 2;
    } else if (*p == 't') {
        len = 2;
    } else if (*p == 'r') {
        len = 2;
    } else if (*p == '\\') {
        len = 2;
    } else if (*p == '"') {
        len = 2;
    } else if (*p == '\'') {
        len = 2;
    } else if (*p == '0') {
        len = 2;
    } else if (*p == 'x' && string_ensure_len(p, 1 + 2)) {
        len = 2 + 2;
    } else if (*p == 'u' && string_ensure_len(p, 1 + 4)) {
        len = 2 + 4;
    } else if (*p == 'U' && string_ensure_len(p, 1 + 8)) {
        len = 2 + 8;
    } else if (isdigit(*p)) {
        len = 2;
        if (isdigit(*(p++))) {
            len++;
        }
        if (isdigit(*(p++))) {
            len++;
        }
    }
    return len;
}

static int is_punctuation(const char *p) {
    static char *kw[] = {
        "<<=", ">>=", "...", "==", "!=", "<=", ">=", "->", "+=", "-=", "*=",
        "/=", "++", "--", "%=", "&=", "|=", "^=", "&&", "||", "<<", ">>", "##"};

    size_t N = sizeof(kw) / sizeof(*kw);
    for (size_t i = 0; i < N; ++i) {
        char *q = kw[i];
        size_t len = strlen(q);
        if (strncmp(p, q, len) == 0) {
            return len;
        }
    }

    char c = *p;
    if (c == '!' || c == '"' || c == '#' || c == '$' || c == '%' || c == '&' ||
        c == '\'' || c == '(' || c == ')' || c == '*' || c == '+' || c == ',' ||
        c == '-' || c == '.' || c == '/' || c == ':' || c == ';' || c == '<' ||
        c == '=' || c == '>' || c == '?' || c == '@' || c == '[' || c == '\\' ||
        c == ']' || c == '^' || c == '_' || c == '`' || c == '{' || c == '|' ||
        c == '}' || c == '~')
        return 1;
    return 0;
}

static Token *tokenize_number(const char *pos, TokenLocation loc, TokenizerError *error) {
    Token *token = NULL;
    const char *p = pos;
    error->pos = p;
    error->message = NULL;

    if (isdigit(p[0]) || (p[0] == '.' && isdigit(p[1]))) {
        const char *q = p;
        while (isalnum(p[0]) || (p[0] == '_') || (p[0] == '.')) {
            p++;
        }

        token = token_new(TKN_NUM, q, p, loc);
    }
    return token;
}

Token *tokenize_number2(const char *pos, TokenLocation loc, TokenizerError *error) {
    Token *token = NULL;
    const char *p = pos;
    error->pos = p;
    error->message = NULL;

    // ([0-9]+(\.[0-9]*)?|\.[0-9]+)[eE][+-]?[0-9]+
    if (isdigit(p[0]) || (p[0] == '.' && isdigit(p[1]))) {
        const char *q = p;
        int has_dot = 0;
        int has_exp = 0;
        int has_sig = 0;
        do {
            if (p[0] == '.') {
                if (has_dot) {
                    error->pos = p;
                    error->message = "Invalid dot in decimal number.";
                    break;
                }
                if (has_exp) {
                    error->pos = p;
                    error->message = "Invalid dot in exponent.";
                    break;
                }
                has_dot = 1;
            } else if (p[0] == 'e' || p[0] == 'E') {
                if (has_exp) {
                    error->pos = p;
                    error->message = "Invalid exponent in scientific notation";
                    break;
                }
                has_exp = 1;
                if (p[1] != '\0') {
                    p++;
                    if (p[0] == '+' || p[0] == '-') {
                        if (has_sig) {
                            error->pos = p;
                            error->message = "Invalid sign in scientific notation";
                            break;
                        }
                        has_sig = 1;
                        if (p[1] == '\0') {
                            error->pos = p + 1;
                            error->message = "Unexpected end of file after exponent sign.";
                            break;
                        } else if (!isdigit(p[1])) {
                            error->pos = p + 1;
                            error->message = "Invalid character after exponent sign.";
                            break;
                        }
                    } else if (!isdigit(p[0])) {
                        error->pos = p;
                        error->message = "Invalid character after exponent.";
                        break;
                    }
                } else {
                    error->pos = p + 1;
                    error->message = "Unexpected end of file after exponent.";
                    break;
                }
            } else if (!isdigit(*p)) {
                break;
            }
            p++;
        } while (1);

        if (error->message == NULL) {
            token = token_new(TKN_NUM, q, p, loc);
        }
    }
    return token;
}

static Token *tokenize_string(const char *pos, TokenLocation loc, TokenizerError *error) {
    Token *token = NULL;
    const char *p = pos;
    error->pos = p;
    error->message = NULL;

    int skip_len = 0;
    int is_string = 0;
    if (*p == '"') {
        skip_len = 1;
        is_string = 1;
    } else if (strncmp(p, "u8\"", 3) == 0) {
        skip_len = 3;
        is_string = 1;
    } else if (strncmp(p, "u\"", 2) == 0) {
        skip_len = 2;
        is_string = 1;
    } else if (strncmp(p, "L\"", 2) == 0) {
        skip_len = 2;
        is_string = 1;
    } else if (strncmp(p, "U\"", 2) == 0) {
        skip_len = 2;
        is_string = 1;
    }

    if (is_string != 1) {
        return NULL;
    }
    const char *q = p;
    p += skip_len;
    while (*p != '"') {
        if (*p == '\0') {
            error->pos = p;
            error->message = "Unclosed string.";
            break;
        }
        if (*p == '\n' || *p == '\r') {
            error->pos = p;
            error->message = "Break line inside string.";
            break;
        }
        if (p[0] == '\\' && p[1] == '\n') {
            p += 2;
            continue;
        }
        if (*p == '\\') {
            size_t len = read_escape_sequence(p);
            if (len > 0) {
                p += len - 1;
            } else {
                error->pos = p;
                error->message = string_format("Invalid escape senquence %d %d.", (int)p[0], (int)p[1]);
                break;
            }
        }
        p++;
    }
    if (error->message == NULL) {
        p++;
        token = token_new(TKN_STR, q, p, loc);
    }
    return token;
}

static Token *tokenize_literal(const char *pos, TokenLocation loc, TokenizerError *error) {
    Token *token = NULL;
    const char *p = pos;
    error->pos = p;
    error->message = NULL;

    if (*p == '\'') {
        const char *q = p;
        p++;
        while (*p != '\'') {
            if (*p == '\0') {
                error->pos = p;
                error->message = "Unclosed literal.";
                break;
            }
            if (*p == '\n' || *p == '\r') {
                error->pos = p;
                error->message = "Break line inside literal.";
                break;
            }
            if (*p == '\\') {
                size_t len = read_escape_sequence(p);
                if (len > 0) {
                    p += len - 1;
                } else {
                    error->pos = p;
                    error->message = "Invalid escape senquence.";
                    break;
                }
            }
            p++;
        }
        if (error->message == NULL) {
            p++;
            token = token_new(TKN_NUM, q, p, loc);
        }
    }
    return token;
}

static Token *tokenize_ident(const char *pos, TokenLocation loc, TokenizerError *error) {
    Token *token = NULL;
    const char *p = pos;
    error->pos = p;
    error->message = NULL;

    if (isalpha(*p) || *p == '_') {
        const char *q = p;
        p++;
        while (isdigit(*p) || isalpha(*p) || *p == '_') {
            p++;
        }

        if (error->message == NULL) {
            token = token_new(TKN_ID, q, p, loc);
        }
    }
    return token;
}

static Token *tokenize_punctuation(const char *pos, TokenLocation loc, TokenizerError *error) {
    Token *token = NULL;
    const char *p = pos;
    error->pos = p;
    error->message = NULL;

    int len = is_punctuation(p);
    if (len > 0) {
        const char *q = p;
        p += (len);
        token = token_new(TKN_PUNCT, q, p, loc);
    }
    return token;
}

static Token *tokenize(State *state, File *file) {
    UNUSED(state);

    Token head = {0};
    Token *cur = &head;
    TokenLocation loc = {0};

    loc.file = file;
    const char *p = file->content;
    while (*p) {
        if (strncmp(p, "//", 2) == 0) {
            p += 2;
            loc.column += 2;
            while (*p != 0 && *p != '\n') {
                p++;
                loc.column++;
            }
            continue;
        }

        if (strncmp(p, "/*", 2) == 0) {
            const char *q = p;
            p += 2;
            loc.column += 2;
            do {
                if (*p == '\0') {
                    report_error(loc, q, "Unclosed block comment.");
                }
                if (*p == '\n') {
                    loc.line++;
                    loc.column = 0;
                }
                if (*p == '*') {
                    if (*(p + 1) != '\0' && *(p + 1) == '/') {
                        p += 2;
                        loc.column += 2;
                        break;
                    }
                }
                p++;
                loc.column++;
            } while (1);
            continue;
        }
        if (p[0] == '\\' && p[1] == '\n') // new line skipping
        {
            p += 2;
            loc.column = 0;
            loc.line++;
            continue;
        }
        if (*p == '\n') {
            cur->is_eol = 1;
            p++;
            loc.column = 0;
            loc.line++;
            // LOG_DEBUG("Found new line at %d", (int)(p - file->content));
            // LOG_DEBUG("Location:  line = %lu, column = %lu", loc.line, loc.column);
            continue;
        }
        if (isspace(*p)) {
            cur->has_spaces = 1;
            p++;
            loc.column++;
            continue;
        }

        TokenizerError error = {0};
        Token *t = NULL;
        if (!t && !error.message)
            t = tokenize_number(p, loc, &error);
        if (!t && !error.message)
            t = tokenize_string(p, loc, &error);
        if (!t && !error.message)
            t = tokenize_literal(p, loc, &error);
        if (!t && !error.message)
            t = tokenize_ident(p, loc, &error);
        if (!t && !error.message)
            t = tokenize_punctuation(p, loc, &error);

        if (t != NULL) {
            cur = cur->next = t;
            p += cur->len;
            loc.column += cur->len;
            continue;
        } else if (error.message != NULL) {
            report_error(loc, error.pos, error.message);
        } else {
            report_error(loc, p, "Unexpected token '%c' -> %d | 0x%X (offset: %d)", *p, (int)*p, (int)*p, p - file->content);
        }
    }

    cur->is_eol = 1;
    cur->next = token_new(TKN_EOF, p, p, loc);
    return head.next;
}

// --Preprocessor--

Token *manage_include(State *state, Token *command, Token **copy) {
    Token *tok = command->next;
    const char *filename = NULL;

    // get file name
    Token *include_file = token_copy_until_eol(&tok);
    int is_local = (include_file->type == TKN_STR);
    const char *start = NULL;
    int len = 0;
    if (is_local) {
        start = include_file->pos + 1;
        len = include_file->len - 2;
    } else if (token_equal(include_file, "<")) {
        Token *it = include_file->next;
        start = it->pos;
        while (it && it->type != TKN_EOF && !it->is_eol) {
            len += it->len;
            it = it->next;
        }
    }
    if (!start || len <= 0) {
        report_error(include_file->location, include_file->pos, "Unexpected file name definition on include command.");
    }
    filename = string_copy(start, len);
    token_delete(include_file);

    // search for file
    File file = {0};
    if (!file_search(state, filename, &file, is_local)) {
        report_error(command->location, command->pos, "Unable to find the file to include.");
    }

    // check if already included
    int is_included = 0;
    for (size_t i = 0; i < state->included.count && !is_included; i++) {
        is_included = (strcmp(state->included.items[i]->fullpath, file.fullpath) == 0);
    }
    // TODO: Also check if pragma once
    if (is_included) {
        file_release_content(&file);
        return tok;
    }
    LOG_DEBUG("%*s > INCLUDE: %s (level: %lu)", (int)state->cond_scopes.count, "", filename, state->cond_scopes.count);

    // load file
    if (!file_read_content(&file)) {
        file_release_content(&file);
        report_error(command->location, command->pos, "Error loading content from include file.");
    }

    // process file
    Token *included = process_file_content(state, file);
    LOG_DEBUG("%*s < INCLUDED: %s (level: %lu) <- %s symbols", (int)state->cond_scopes.count, "", filename, state->cond_scopes.count, (!included || included->type == TKN_EOF) ? "has" : "empty");

    if (!included || included->type == TKN_EOF) {
        return tok;
    }

    Token *last = token_last(included);
    if (last != NULL) {
        (*copy)->next = included;
        *copy = last;
    }

    return tok;
}

Token *manage_define(State *state, Token *command) {
    if (command->next == NULL || command->next->type != TKN_ID) {
        report_error(command->next->location, command->next->pos, "Expected a MACRO identifier.");
    }

    Token *tok = command->next;
    Macro *m = macro_read(state, &tok);
    AC_ARRAY_PUSH(state->macros, m);

    return tok;
}

Token *manage_undef(State *state, Token *command) {
    Token *tok = command->next;
    if (!tok->is_eol || tok->type != TKN_ID) {
        report_error(tok->location, tok->pos, "ERROR: Macro name missing.");
    }

    int pos = macro_search_index(state, tok);
    if (pos != -1) {
        AC_ARRAY_REMOVE(state->macros, pos);
        // } else {
        //     LOG_WARN("There is no macro defined as '%.*s'.", (int)tok->len, tok->pos);
    }
    return tok;
}

int cond_block_eval_recursive(State *state, Token *cur, int in) {
    if (!cur || cur->type == TKN_EOF) {
        return in; // end recursive calls
    }
    if (token_equal(cur, "!")) {
        return !cond_block_eval_recursive(state, cur->next, in);
    } else if (token_equal(cur, "defined")) {
        if (token_equal(cur->next, "(") &&
            cur->next->next->type == TKN_ID &&
            token_equal(cur->next->next->next, ")")) {
            in = (macro_search_index(state, cur->next->next) != -1);
        } else {
            report_error(cur->next->location, cur->next->pos, "Unexpected token '%.*s', was expecting '( ID )'", (int)cur->next->len, cur->next->pos);
        }
        return cond_block_eval_recursive(state, cur->next->next->next->next, in);
    } else if (cur->type == TKN_NUM) {
        in = atoi(cur->pos);
        return cond_block_eval_recursive(state, cur->next, in);
    } else if (cur->type == TKN_PUNCT) {
        if (token_equal(cur, "&&")) {
            return in && cond_block_eval_recursive(state, cur->next, 0);
        } else if (token_equal(cur, "||")) {
            return in || cond_block_eval_recursive(state, cur->next, 0);
        } else if (token_equal(cur, "+")) {
            return in + cond_block_eval_recursive(state, cur->next, 0);
        } else if (token_equal(cur, "-")) {
            return in - cond_block_eval_recursive(state, cur->next, 0);
        } else if (token_equal(cur, "==")) {
            return in == cond_block_eval_recursive(state, cur->next, 0);
        } else if (token_equal(cur, "!=")) {
            return in != cond_block_eval_recursive(state, cur->next, 0);
        } else if (token_equal(cur, "<")) {
            return in < cond_block_eval_recursive(state, cur->next, 0);
        } else if (token_equal(cur, ">")) {
            return in > cond_block_eval_recursive(state, cur->next, 0);
        } else if (token_equal(cur, "<=")) {
            return in <= cond_block_eval_recursive(state, cur->next, 0);
        } else if (token_equal(cur, ">=")) {
            return in >= cond_block_eval_recursive(state, cur->next, 0);
        } else if (token_equal(cur, "(")) {
            return cond_block_eval_recursive(state, cur->next, in);
        } else if (token_equal(cur, ")")) {
            return cond_block_eval_recursive(state, cur->next, in);
        } else {
            AC_ARRAY_FOREACH(FilePtr, f, state->included) {
                LOG_INFO("included: %s", (*f)->fullpath);
            }
            report_error(cur->location, cur->pos, "Unexpected conditional token '%.*s'", (int)cur->len, cur->pos);
        }
    } else if (cur->type == TKN_ID) {
        Macro *m = macro_search(state, cur);
        if (m) {
            if (m->body) {
                Token *expanded = macro_expand(state, m, &cur);
                in = cond_block_eval_recursive(state, expanded, 0);
                token_delete(expanded);
            } else {
                in = 1;
            }
        } else {
            if (!cur->has_spaces && token_equal(cur->next, "(")) {
                report_error(cur->location, cur->pos, "function-like macro '%.*s' is not defined", (int)cur->len, cur->pos);
            } else {
                in = 0;
            }
        }
        return cond_block_eval_recursive(state, cur->next, in);
    } else {
        report_error(cur->location, cur->pos, "Unexpected token '%.*s'", (int)cur->len, cur->pos);
    }
    return in;
}

int cond_block_eval(State *state, Token *command, Token *cond) {
    int evaluated = 0;
    if (token_equal(command, "ifdef")) {
        evaluated = (macro_search_index(state, cond) != -1);
    } else if (token_equal(command, "ifndef")) {
        evaluated = (macro_search_index(state, cond) == -1);
    } else if (token_equal(command, "if") || token_equal(command, "elif")) {
        evaluated = cond_block_eval_recursive(state, cond, 0);
    } else {
        report_error(command->location, command->pos, "Unexpected token '%.*s', only expected #if, #elif, #ifdef or #ifndef", (int)command->len, command->pos);
    }
    return evaluated;
}

Token *cond_block_skip(Token *tok) {
    int level = 0;
    LOG_DEBUG("  >> SKIP BLOCK", level);
    Token *prev = tok;
    while (tok && tok->type != TKN_EOF) {
        if (token_equal(tok, "#")) {
            Token *command = tok->next;
            if (token_equal(command, "if") || token_equal(command, "ifdef") || token_equal(command, "ifndef")) {
                level++;
                LOG_DEBUG("    >> UP '%.*s' (%d)", (int)command->len, command->pos, level);
            } else if (token_equal(command, "endif") || token_equal(command, "else") || token_equal(command, "elif")) {
                if (level == 0) {
                    LOG_DEBUG("    >> END '%.*s' (%d)", (int)command->len, command->pos, level);
                    break;
                } else if (token_equal(command, "endif")) {
                    LOG_DEBUG("    >> DOWN '%.*s' (%d)", (int)command->len, command->pos, level);
                    level--;
                }
            }
        }
        prev = tok;
        tok = tok->next;
    }
    return prev;
}

Token *manage_if_block(State *state, Token *command) {
    LOG_DEBUG("");
    int permits = token_equal(command, "if");
    Token *tok = command->next;
    Token *cond = token_copy_until_eol(&tok);
    int val = cond_block_eval(state, command, cond);
    token_delete(cond);

    ConditionScope cond_block = {.last = *command, .valid_block = (val != 0), .else_visited = 0, .permits_elif = permits};
    AC_ARRAY_PUSH(state->cond_scopes, cond_block);
    LOG_DEBUG(" -> cond block (%p) '%.*s' (at level %lu) else_visited: %d, permits_elif: %d, valid_block: %d", command, (int)command->len, command->pos, state->cond_scopes.count, cond_block.else_visited, cond_block.permits_elif, cond_block.valid_block);

    if (!val) {
        tok = cond_block_skip(tok);
    }
    LOG_DEBUG("   -> continue with token '%.*s'", (int)tok->len, tok->pos);
    return tok;
}

Token *manage_elif_block(State *state, Token *command) {
    if (state->cond_scopes.count == 0) {
        report_error(command->location, command->pos, "#elif without #if block");
    }

    ConditionScope scope = state->cond_scopes.items[state->cond_scopes.count - 1];
    Token *tok = command->next;
    Token *cond = token_copy_until_eol(&tok);
    int cond_val = 0;
    int val = !scope.valid_block && (cond_val = cond_block_eval(state, command, cond));
    token_delete(cond);

    LOG_DEBUG(" -> cond block (%p) '%.*s' (at level %lu) else_visited: %d, permits_elif: %d, valid_block: %d, cond_value: %d", command, (int)command->len, command->pos, state->cond_scopes.count, scope.else_visited, scope.permits_elif, scope.valid_block, cond_val);

    state->cond_scopes.items[state->cond_scopes.count - 1].valid_block |= (val != 0);
    state->cond_scopes.items[state->cond_scopes.count - 1].last = *command;

    if (!val) {
        tok = cond_block_skip(tok);
    }
    LOG_DEBUG("   -> continue with token '%.*s'", (int)tok->len, tok->pos);
    return tok;
}

Token *manage_else_block(State *state, Token *command) {
    if (state->cond_scopes.count == 0) {
        report_error(command->location, command->pos, "#else without #if block");
    }
    ConditionScope scope = state->cond_scopes.items[state->cond_scopes.count - 1];
    if (scope.else_visited) {
        report_error(command->location, command->pos, "#else without #if block");
    }

    Token *tok = command;
    int val = scope.valid_block;
    state->cond_scopes.items[state->cond_scopes.count - 1].last = *command;

    LOG_DEBUG(" -> cond block (%p) '%.*s' (at level %lu) else_visited: %d, permits_elif: %d, valid_block: %d", command, (int)command->len, command->pos, state->cond_scopes.count, scope.else_visited, scope.permits_elif, scope.valid_block);

    if (val) {
        tok = cond_block_skip(tok);
    }
    LOG_DEBUG("   -> continue with token '%.*s'", (int)tok->len, tok->pos);
    return tok;
}

Token *manage_endif_block(State *state, Token *command) {
    if (state->cond_scopes.count == 0) {
        report_error(command->location, command->pos, "#endif without #if block");
    }

    Token *tok = command;
    ConditionScope scope;
    AC_ARRAY_POP(state->cond_scopes, scope);
    UNUSED(scope);

    LOG_DEBUG(" -> cond block (%p) '%.*s' (at level %lu)", command, (int)command->len, command->pos, state->cond_scopes.count);

    return tok;
}

Token *manage_line(State *state, Token *command) {
    UNUSED(state);
    Token *tok = command->next;
    while (tok && !tok->is_eol && !(tok->type == TKN_EOF)) {
        tok = tok->next;
    }
    return tok;
}

Token *manage_error(State *state, Token *command) {
    UNUSED(state);
    Token *it = command;
    while (it && !it->is_eol) {
        it = it->next;
    }
    if (it == command) {
        report_error(it->location, it->pos, "error:");
    } else {
        report_error(it->location, it->pos, "error: %.*s", (int)it->len, it->pos);
    }
    return NULL;
}

Token *manage_warning(State *state, Token *command) {
    UNUSED(state);
    Token *tok = command;
    Token *message = token_copy_until_eol(&tok);
    LOG_WARN("warning:");
    token_dump_full(message);
    return tok;
}

Token *manage_pragma(State *state, Token *command) {
    UNUSED(state);
    Token *tok = command->next;
    while (tok && !tok->is_eol && !(tok->type == TKN_EOF)) {
        tok = tok->next;
    }
    return tok;
}

Token *preprocess(State *state, Token *start) {
    Token head = {0};
    Token *copy = &head;
    Token *cur = start;
    Token *prev = 0;
    UNUSED(prev);

    while (cur && cur->type != TKN_EOF) {
        if (token_equal(cur, "#")) {
            Token *command = cur->next;
            if (!command) {
                report_error(cur->location, cur->pos, "Expected preprocess command.");
            }

            if (token_equal(command, "include")) {
                cur = manage_include(state, command, &copy);
            } else if (token_equal(command, "define")) {
                cur = manage_define(state, command);
            } else if (token_equal(command, "undef")) {
                cur = manage_undef(state, command);
            } else if (token_equal(command, "if")) {
                cur = manage_if_block(state, command);
            } else if (token_equal(command, "ifdef")) {
                cur = manage_if_block(state, command);
            } else if (token_equal(command, "ifndef")) {
                cur = manage_if_block(state, command);
            } else if (token_equal(command, "elif")) {
                cur = manage_elif_block(state, command);
            } else if (token_equal(command, "else")) {
                cur = manage_else_block(state, command);
            } else if (token_equal(command, "endif")) {
                cur = manage_endif_block(state, command);
            } else if (token_equal(command, "line")) {
                cur = manage_line(state, command);
            } else if (token_equal(command, "warning")) {
                cur = manage_warning(state, command);
            } else if (token_equal(command, "error")) {
                cur = manage_error(state, command);
            } else if (token_equal(command, "pragma")) {
                cur = manage_pragma(state, command);
            } else {
                report_error(cur->location, cur->pos, "Unknown preprocess command.");
            }
            cur = cur->next;
        } else {
            if (cur->type == TKN_ID) {
                Macro *m = macro_search(state, cur);
                if (m) {
                    Token *expanded = macro_expand(state, m, &cur);
                    if (expanded) {
                        token_link(expanded, cur->next);
                        cur->next = expanded;
                    }
                    cur = cur->next;
                    continue;
                }
            }
            LOG_DEBUG("TOKEN: '%.*s'", (int)cur->len, cur->pos);
            copy = copy->next = token_copy(cur);
            prev = cur;
            cur = cur->next;
        }
    }
    copy->next = token_new(TKN_EOF, NULL, NULL, (TokenLocation){0});
    return head.next;
}

// -- Args --

static const char *arg_shift(int *argc, const char ***argv) {
    if (*argc <= 0) {
        return NULL;
    }
    const char *ret = **argv;
    (*argv)++;
    (*argc)--;
    return ret;
}

static Args arg_parse(int argc, const char **argv) {
    Args args = {0};
    args.file_list = AC_ARRAY_CREATE(ConstCharPtr, 16);
    args.include_dir = AC_ARRAY_CREATE(ConstCharPtr, 16);

    const char *program = arg_shift(&argc, &argv);
    LOG_INFO("Current program: %s", program);

    while (1) {
        const char *param = arg_shift(&argc, &argv);
        if (param == NULL) {
            break;
        }
        if (strncmp(param, "-I", 2) == 0) {
            const char *val = arg_shift(&argc, &argv);
            AC_ARRAY_PUSH(args.include_dir, val);
            continue;
        }
        AC_ARRAY_PUSH(args.file_list, param);
    }
    return args;
}

// -- File management --

static int file_exists(const char *file_path) {
#if _WIN32
    LOG_ERROR("Method 'file_exists' not yet implemented for windows!");
    abort();
#else
    return (access(file_path, F_OK) == 0);
#endif
    return 0;
}

static File *file_copy(File *file) {
    File *ret = (File *)malloc(sizeof(File));
    ret->content_size = file->content_size;
    ret->content = file->content;
    ret->fullpath = file->fullpath;
    ret->filename = file->filename;
    return ret;
}

static void file_release_content(File *file) {
    file->content_size = 0;
    if (file->content) {
        free((void *)file->content);
        file->content = NULL;
    }

    if (file->fullpath) {
        free((void *)file->fullpath);
        file->fullpath = NULL;
    }
    file->filename = NULL; // filename is reference to fullpath
}

static void file_delete(File *file) {
    file_release_content(file);
    free(file);
}

static int file_search(State *state, const char *file_path, File *file, int search_local) {
    // TODO: Organize better the search, use this pseudo-code:
    // if is_local
    //   if is_absolute -> load
    //   elif exist(current_path + file_path) -> load
    // else
    //   foreach(sys_path)
    //     if exists(sys_path + filepath) -> load

    int ret = 0;

    file->filename = NULL;

    if (path_is_absolute(file_path)) {
        if (!file_exists(file_path)) {
            return ret;
        }

        file->fullpath = strdup(file_path);
        for (size_t i = 0; i < state->search_paths.count; i++) {
            size_t len = strlen(state->search_paths.items[i]);
            if (strncmp(file->fullpath, state->search_paths.items[i], len) == 0) {
                file->filename = file->fullpath + len;
                break;
            }
        }

        if (file->filename == NULL) {
            file->filename = string_search_char(file->fullpath, '/', -1) + 1;
        }

        ret = 1;
    } else {
        // Implements search in local and/or system paths
        // state->search_paths has all the include paths:
        // - The paths sould be ordered first should be the sistem paths, then the current path (pwd), then the inherited ones
        // - The loop must traverse the items in reverse order, the more recent added the first.
        // - There should be an index pointing to the current path (pwd)
        // - If the flag 'search_local' is active then the loop starts at state->search_paths.count-1, otherwise it starts at index
        size_t start = state->info.system_path_idx - 1;
        if (search_local) {
            start = state->search_paths.count - 1;
        }

        file_release_content(file);
        for (int i = (int)start; i >= 0; i--) {
            const char *current_path = state->search_paths.items[i];

            file->fullpath = path_join(current_path, file_path);
            file->filename = file->fullpath + strlen(current_path) + 1;
            if (file_exists(file->fullpath)) {
                ret = 1;
                break;
            }
            file_release_content(file);
        }
    }
    return ret;
}

static int file_read_content(File *file) {
    FILE *fp = NULL;
    int ret = 0;

    if (file->fullpath == NULL) {
        goto finally;
    }

    fp = fopen(file->fullpath, "r");
    if (!fp)
        goto finally;
    if (fseek(fp, 0, SEEK_END) < 0)
        goto finally;
    long count = ftell(fp);
    if (count < 0)
        goto finally;
    if (fseek(fp, 0, SEEK_SET) < 0)
        goto finally;

    char *content = (char *)malloc((count + 1) * sizeof(char));

    long total = 0;
    do {
        long read = fread(content + total, 1, count, fp);
        if (read == 0 || ferror(fp)) {
            goto finally;
        }
        total += read;
    } while (total < count);
    content[count] = '\0';

    file->content = content;
    file->content_size = count;

    ret = 1;
finally:
    if (!ret) {
        if (file->filename) {
            LOG_ERROR("Can't read file '%s' (errno %d: %s)", file->filename, errno, strerror(errno));
        } else {
            LOG_ERROR("File properties are not set, can't load an undefined file.");
        }
        file_delete(file);
    }
    if (fp)
        fclose(fp);
    return ret;
}

// -- Compiler program --

Token *process_file_content(State *state, File file) {
    File *copy = file_copy(&file);
    AC_ARRAY_PUSH(state->included, copy);
    Token *tok = tokenize(state, copy);
    if (tok == NULL)
        return NULL;
    // token_dump_full(tok);

    Token *ret = preprocess(state, tok);
    token_delete(tok);
    return ret;
}

Token *process_file(State *state, const char *filename, int search_local) {
    // TODO: Find a better way to load from file. Unify this method with manage_include
    File file = {0};
    if (!file_search(state, filename, &file, search_local)) {
        LOG_ERROR("Can't locate the file '%s' within the search paths [%lu in total]", filename, state->search_paths.count);
        return NULL;
    }

    if (!file_read_content(&file)) {
        return NULL;
    }

    return process_file_content(state, file);
}

int main(int argc, const char **argv) {
    if (argc < 2) {
        LOG_ERROR("Invalid number of parameters: %lu", argc);
        return 1;
    }

    Args args = arg_parse(argc, argv);
    for (size_t i = 0; i < args.file_list.count; ++i) {
        State state = {0};
        for (size_t i = 0; i < args.include_dir.count; i++) {
            AC_ARRAY_PUSH(state.search_paths, args.include_dir.items[i]);
        }
        state_init(&state);

        LOG_INFO("Param %lu) %s", i, args.file_list.items[i]);

        Token *tok = process_file(&state, args.file_list.items[i], 1);
        if (tok == NULL) {
            return 1;
        }

        LOG_INFO("");
        token_dump_full(tok);
        LOG_INFO("");

        if (state.cond_scopes.count > 0) {
            ConditionScope scope = state.cond_scopes.items[state.cond_scopes.count - 1];
            report_error(scope.last.location, scope.last.pos, "Open cond block! (%ul)", state.cond_scopes.count - 1);
        }

        // TODO: generate asm code
        state_cleanup(&state);
    }
    return 0;
}