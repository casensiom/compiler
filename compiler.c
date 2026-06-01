#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/errno.h>
#include <time.h>

#include "array.h"

#define UNUSED(var) (void)var
#define LOG_DEBUG(fmt, ...) log_msg(stdout, "[DEBUG]", fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) log_msg(stdout, "[INFO]", fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) log_msg(stderr, "[WARN]", fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) log_msg(stderr, "[ERROR]", fmt, ##__VA_ARGS__)

typedef char Char;
AC_ARRAY_DEFINE(Char);

typedef char *CharPtr;
AC_ARRAY_DEFINE(CharPtr);

typedef const char *ConstCharPtr;
AC_ARRAY_DEFINE(ConstCharPtr);

typedef struct Args
{
    ConstCharPtrArray file_list;
} Args;

typedef struct ConditionScope
{
    int skip;
    int else_visited;
    int ignore_and_skip;
} ConditionScope;
AC_ARRAY_DEFINE(ConditionScope);

typedef struct File
{
    const char *filename;
    const char *dirname;
    const char *content;
    size_t content_size;
    struct File *prev;
} File;
AC_ARRAY_DEFINE(File);

typedef enum TokenKind
{
    TKN_UNKNOWN,
    TKN_ID,
    TKN_PUNCT,
    TKN_STR,
    TKN_NUM,
    TKN_EOF
} TokenKind;

typedef struct TokenLocation
{
    File *file;
    size_t line;
    size_t column;
} TokenLocation;

typedef struct Token
{
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

typedef struct Macro
{
    Token *start;
} Macro;
AC_ARRAY_DEFINE(Macro);

typedef struct CompilerInfo
{
    const char *date;
    const char *time;
} CompilerInfo;

typedef struct State
{
    FileArray included;
    ConditionScopeArray cond_scopes;
    MacroArray macros;
    TokenPtrArray tokens;
    CompilerInfo info;
} State;

Token *process_file_content(State *state, File file);

void log_msg(FILE *out, const char *level, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(out, "%s ", level);
    vfprintf(out, fmt, args);
    va_end(args);
    fprintf(out, "\n");
}

static char *string_copy(const char *str, size_t len)
{
    char *ret = NULL;
    if (len > 0)
    {
        ret = malloc((len + 1) * sizeof(char *));
        memcpy(ret, str, len);
        ret[len] = '\0';
    }
    return ret;
}

static char *string_format(const char *fmt, ...)
{
    char *ret = NULL;
    va_list args;
    va_start(args, fmt);
    vasprintf(&ret, fmt, args);
    va_end(args);
    return ret;
}

Token *token_new(TokenKind type, const char *s, const char *e,
                 TokenLocation loc)
{
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

Token *token_from_string(TokenKind type, const char *str, TokenLocation loc)
{
    size_t len = strlen(str);
    Token *ret = token_new(type, str, str + len, loc);
    ret->str = str;
    return ret;
}

Token *token_copy(Token *orig)
{
    Token *ret = (Token *)calloc(1, sizeof(Token));
    *ret = *orig;
    ret->next = NULL;
    orig->str = NULL; // last copy keeps str ownership
    return ret;
}

void token_delete(Token *tok)
{
    while (tok != NULL)
    {
        Token *next = tok->next;
        if (tok->str)
        {
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

int token_equal(Token *tok, const char *str)
{
    return (tok && str && strncmp(tok->pos, str, tok->len) &&
            str[tok->len] == '\0');
}

void token_line(Token *tok, const char **start, const char **end)
{
    const char *it = tok->location.file->content;
    const char *line_start = it;
    while (it && *it != '\0' && it < tok->pos)
    {
        if (*it == '\n')
        {
            line_start = it;
        }
        ++it;
    }
    const char *line_end = it;
    while (it && *it != '\0' && *it != '\n')
    {
        ++it;
    }

    *start = line_start;
    *end = line_end;
}

// process_file_content
void state_init(State *state)
{
    const char *macro_names = "\
        #define __FILE__\n\
        #define __LINE__\n\
        #define __DATE__\n\
        #define __TIME__\n\
        #define __STDC__ 1\n\
        #define __STDC_VERSION__ 199409L\n\
        #define __STDC_HOSTED__ 1\n";

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    static char *month_name[] = {"Jan", "Feb", "Mar", "Apr", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    state->info.date = string_format("\"%s %02d %d\"", month_name[tm->tm_mon], tm->tm_mday, tm->tm_year + 1900);
    state->info.time = string_format("\"%02d:%02d:%02d\"", tm->tm_hour, tm->tm_min, tm->tm_sec);

    size_t len = strlen(macro_names);

    File file = {.filename = "system", .content = macro_names, .content_size = len, .dirname = NULL, .prev = NULL};
    Token *tokens = process_file_content(state, file);
    token_delete(tokens);
}
void state_cleanup(State *state)
{
    // TODO: Remove elements from arrays
    state->included.count = 0;
    state->cond_scopes.count = 0;
    state->macros.count = 0;
    state->tokens.count = 0;

    if (state->info.date)
        free((void *)state->info.date);
    if (state->info.time)
        free((void *)state->info.time);
    state->info.date = NULL;
    state->info.time = NULL;
}

static int get_number_len(size_t num)
{
    size_t len = 1;
    while (num >= 10)
    {
        num /= 10;
        len++;
    }
    return len;
}

void report_error_internal(const char *filename, const char *line_start,
                           int line_len, size_t line, size_t column,
                           const char *message, va_list args)
{
    printf("%s:%lu:%lu: ", filename, line, column);
    vprintf(message, args);
    printf("\n");
    printf(" %lu | %.*s\n", line, line_len, line_start);
    printf("%*s\n", get_number_len(column) + (int)column + 4, "^");
}

void report_error(const char *filename, const char *line_start, int line_len,
                  size_t line, size_t column, const char *message, ...)
{
    va_list args;
    va_start(args, message);
    report_error_internal(filename, line_start, line_len, line, column, message,
                          args);
    va_end(args);

    abort();
}

void token_report_error(Token *tok, const char *message, ...)
{
    const char *line_start = NULL;
    const char *line_end = NULL;
    token_line(tok, &line_start, &line_end);
    int line_len = line_end - line_start;
    size_t line = tok->location.line + 1;
    size_t column = tok->location.column + 1;
    const char *filename = tok->location.file->filename;

    va_list args;
    va_start(args, message);
    report_error_internal(filename, line_start, line_len, line, column, message,
                          args);
    va_end(args);

    abort();
}

static Token *
tokenize(State *state, File *file)
{
    Token head = {0};
    Token *cur = &head;
    TokenLocation loc = {0};

    loc.file = file;
    const char *p = file->content;
    while (*p)
    {
    }
    return head.next;
}

Token *manage_include(State *state, Token *tok)
{
    while (!tok->is_eol)
    {
        tok = tok->next;
    }
    return tok;
}

Token *manage_define(State *state, Token *tok)
{
    while (!tok->is_eol)
    {
        tok = tok->next;
    }
    return tok;
}

Token *manage_undef(State *state, Token *tok)
{
    while (!tok->is_eol)
    {
        tok = tok->next;
    }
    return tok;
}

Token *manage_cond_block(State *state, Token *tok)
{
    while (!tok->is_eol)
    {
        tok = tok->next;
    }
    return tok;
}

Token *manage_line(State *state, Token *tok)
{
    while (!tok->is_eol)
    {
        tok = tok->next;
    }
    return tok;
}

Token *manage_pragma(State *state, Token *tok)
{
    while (!tok->is_eol)
    {
        tok = tok->next;
    }
    return tok;
}

Macro *macro_search(State *state, Token *tok)
{
    return NULL;
}

Token *macro_expand(State *state, Macro *macro, Token *tok)
{
    return tok->next;
}

Token *preprocess(State *state, Token *start)
{
    Token head = {};
    Token *copy = &head;
    Token *cur = start;
    Token *prev = 0;
    while (cur && cur->type != TKN_EOF)
    {
        if (token_equal(cur, "#"))
        {
            Token *command = cur->next;
            if (!command)
            {
                token_report_error(cur, "Expected preprocess command.");
            }

            if (token_equal(command, "include"))
            {
                cur = manage_include(state, command->next);
            }
            else if (token_equal(command, "define"))
            {
                cur = manage_define(state, command->next);
            }
            else if (token_equal(command, "undef"))
            {
                cur = manage_undef(state, command->next);
            }
            else if (token_equal(command, "if") || token_equal(command, "ifdef") ||
                     token_equal(command, "ifndef") || token_equal(command, "elif") ||
                     token_equal(command, "else") || token_equal(command, "endif"))
            {
                cur = manage_cond_block(state, command->next);
            }
            else if (token_equal(command, "line"))
            {
                cur = manage_line(state, command->next);
            }
            else if (token_equal(command, "error"))
            {
                token_report_error(command->next, "error: %.*s",
                                   (int)command->next->len, command->next->pos);
            }
            else if (token_equal(command, "pragma"))
            {
                cur = manage_pragma(state, command->next);
            }
            else
            {
                token_report_error(cur, "Unknown preprocess command.");
            }
        }
        else
        {
            if (cur->type == TKN_ID)
            {
                Macro *m = macro_search(state, cur);
                if (!m)
                {
                    cur = macro_expand(state, m, cur);
                    continue;
                }
                else
                {
                    copy = copy->next = token_copy(cur);
                    prev = cur;
                }
            }
        }
        cur = cur->next;
    }
    return head.next;
}

static int read_file_content(const char *file_path, File *file)
{

    file->content = NULL;
    file->content_size = 0;
    file->filename = file_path; // basename(file_path);
    // file->dirname = dirname(file_path);
    //  TODO: manage file and path split

    int ret = 1;
    FILE *fp = fopen(file_path, "r");
    if (!fp)
        goto finally;
    if (fseek(fp, 0, SEEK_END) < 0)
        goto finally;
    long count = ftell(fp);
    if (count < 0)
        goto finally;
    if (fseek(fp, 0, SEEK_SET) < 0)
        goto finally;

    char *content = (char *)malloc(count * sizeof(char));

    long total = 0;
    do
    {
        long read = fread(content + total, 1, count, fp);
        if (read == 0 || ferror(fp))
        {
            goto finally;
        }
        total += read;
    } while (total < count);

    file->content = content;
    file->content_size = count;

    ret = 0;
finally:
    if (ret > 0)
        LOG_ERROR("Can't read file '%s' (errno %d: %s)", file_path, errno,
                  strerror(errno));
    if (fp)
        fclose(fp);
    return ret;
}

static const char *shift(int *argc, const char ***argv)
{
    if (*argc <= 0)
    {
        return NULL;
    }
    const char *ret = **argv;
    (*argv)++;
    (*argc)--;
    return ret;
}

static Args parse_args(int argc, const char **argv)
{
    Args args = {0};
    args.file_list = AC_ARRAY_CREATE(ConstCharPtr, 16);

    const char *program = shift(&argc, &argv);
    UNUSED(program);

    while (1)
    {
        const char *param = shift(&argc, &argv);
        if (param == NULL)
        {
            break;
        }
        AC_ARRAY_PUSH(args.file_list, param);
    }
    return args;
}

Token *process_file_content(State *state, File file)
{
    AC_ARRAY_PUSH(state->included, file);
    Token *tok = tokenize(state, &(state->included.items[state->included.count - 1]));
    if (tok == NULL)
        return NULL;
    Token *ret = preprocess(state, tok);
    token_delete(tok);
    return ret;
}

Token *process_file(State *state, const char *filename, int search_local)
{
    // TODO: Manage search paths
    File file;
    if (read_file_content(filename, &file))
        return NULL;

    return process_file_content(state, file);
}

int main(int argc, const char **argv)
{
    if (argc < 2)
    {
        LOG_ERROR("Invalid number of parameters: %lu", argc);
        return 1;
    }

    Args args = parse_args(argc, argv);
    for (size_t i = 0; i < args.file_list.count; ++i)
    {
        State state;
        state_init(&state);
        LOG_INFO("Param %lu) %s", i, args.file_list.items[i]);

        Token *tok = process_file(&state, args.file_list.items[i], 1);
        if (tok == NULL)
        {
            return 1;
        }

        // TODO: generate asm code
        state_cleanup(&state);
    }
    return 0;
}