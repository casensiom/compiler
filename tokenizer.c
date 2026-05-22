#include "tokenizer.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/errno.h>
#include <stdarg.h>
#include <string.h>

#define TRACE_ERROR(level, fmt, ...)                             \
    do                                                           \
    {                                                            \
        log_msg_(__FILE__, __LINE__, level, fmt, ##__VA_ARGS__); \
        abort();                                                 \
    } while (0);

#define FATAL(fmt, ...) TRACE_ERROR("[FATAL]", fmt, ##__VA_ARGS__)
#define TODO(fmt, ...) TRACE_ERROR("[TODO]", fmt, ##__VA_ARGS__)
#define UNREACHABLE(fmt, ...) TRACE_ERROR("[UNREACHABLE]", fmt, ##__VA_ARGS__)
#define UNUSED(var) (void)var

#define LOG_DEBUG(fmt, ...) log_msg(stdout, "[DEBUG]", fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) log_msg(stdout, "[INFO]", fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) log_msg(stderr, "[WARN]", fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) log_msg(stderr, "[ERROR]", fmt, ##__VA_ARGS__)

void log_msg(FILE *out, const char *level, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(out, "%s ", level);
    vfprintf(out, fmt, args);
    va_end(args);
    fprintf(out, "\n");
}

void log_msg_(const char *file, int line, int column, const char *level, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "%s:%d:%d %s ", file, line, column, level);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

static int
read_file_content(const char *file_path, CharArray *content)
{
    int ret = 1;
    FILE *fp = fopen(file_path, "r");
    if (!fp)                        goto finally;
    if (fseek(fp, 0, SEEK_END) < 0) goto finally;
    long count = ftell(fp);
    if (count < 0)                  goto finally;
    if (fseek(fp, 0, SEEK_SET) < 0) goto finally;
    AC_ARRAY_DESTROY(*content);
    *content = AC_ARRAY_CREATE(Char, count);
    
    long total = 0;
    do {
        long read = fread(content->items, 1, count, fp);
        if(read == 0 || ferror(fp)) {
            goto finally;
        }
        total += read;
    }while(total < count);
    content->count = count;
    
    ret = 0;
finally:
    if (ret > 0)  LOG_ERROR("Can't read file '%s' (errno %d: %s)", file_path, errno, strerror(errno));
    if (fp)       fclose(fp);
    return ret;
}

static const char *
shift(int *argc, const char ***argv)
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

static Args
parse_args(int argc, const char **argv)
{
    Args args = {0};
    args.list = AC_ARRAY_CREATE(FileName, 16);

    const char *program = shift(&argc, &argv);
    UNUSED(program);

    while (1)
    {
        const char *param = shift(&argc, &argv);
        if (param == NULL)
        {
            break;
        }
        AC_ARRAY_PUSH(args.list, param);
    }
    return args;
}

Token *
new_token(TokenKind type, const char *start, const char *end)
{
    Token *tok = calloc(1, sizeof(Token));
    tok->type = type;
    tok->pos = start;
    tok->len = end - start;
    tok->next = NULL;
    return tok;
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
    return is_alpha(c) || c == '_' || c == '#';
}

static int
is_ident(char c) {
    return is_digit(c) || is_alpha(c) || c == '_';
}
static int
is_punctuation(char c) {
    // return c == '<' || c == '>' || c == '='|| c == '!'|| c == '&'|| c == '|'|| c == '%'|| c == '+'|| c == '-'|| c == '/' || c == '*' || c == '.';
    return c == '!' || c == '"' || c== '#' || c == '$' || c == '%' ||
        c == '&' || c == '\'' || c== '(' || c == ')' || c == '*' ||
        c == '+' || c == ',' || c== '-' || c == '.' || c == '/' ||
        c == ':' || c == ';' || c== '<' || c == '=' || c == '>' ||
        c == '?' || c == '@' || c== '[' || c == '\\' || c == ']' ||
        c == '^' || c == '_' || c== '`' || c == '{' || c == '|' ||
        c == '}' || c ==  '~';
}

static void
report_error(const char *filename, const char *start, const char *pos, const char *msg) {

    size_t line = 1;
    const char *s = start;
    while(s < pos) {
        if(*s == '\n') {
            line++;
        }
        s++;
    }
    
    s = pos;
    while(s>start && *s != '\n') {
        s--;
    }
    const char *line_start = s;
    s = pos;
    while(s && *s != '\n') {
        s++;
    }
    const char *line_end = s;
    
    
    int len = line_end - line_start;
    int column = pos - line_start;
    log_msg_(filename, line, column, "error:", msg);
    printf("%.*s\n", len, line_start);
    for(int i = 0; i < column - 1; i++) {
        printf(" ");
    }
    printf("^ %s\n", msg);



    abort();
}

static Token *
tokenize(File *file) {

    // TODO: use utf8 to read characters
    Token head = {0};
    Token *cur = &head;

    const char *p = file->content;
    while(*p) {
        if(strncmp(p, "//" , 2) == 0) {
            p += 2;
            while(*p != '\n') {
                p++;
            }
            continue;
        }

        if(strncmp(p, "/*" , 2) == 0) {
            const char *q = p;
            p += 2;
            do {
                while(*p != 0 && *p != '*') {
                    p++;
                }
                if(*p == '/') {
                    p++;
                    break;
                } else if(*p == 0) {
                    // Report error at saved_pos
                    report_error(file->name, file->content, q, "Unclosed block comment.");
                }
            } while(1);
            continue;
        }

        if(is_space(*p)) {
            p++;
            continue;
        }

        if(*p == '\n') {
            p++;
            continue;
        }

        if(is_digit(p[0]) || (p[0] == '.' && is_digit(p[1]))) {
            const char *q = p; 
            // TODO: Be more strict, support scientific notation and check valid values
            while(is_digit(*p) || p[0] == '.') {
                p++;
            }
            cur->next = new_token(TKN_NUMBER, q, p);
            cur = cur->next;
            continue;
        }

        if(p[0] == '"') {
            const char *q = p;
            p++;
            while(*p != '"') {
                if(*p == '\0') {
                    report_error(file->name, file->content, q, "Unclosed string.");
                }
                if(*p == '\n') {
                    report_error(file->name, file->content, q, "Break line inside string.");
                }
                if (*p == '\\') {
                    p++;
                }
                p++;
            }
            p++;
            cur->next = new_token(TKN_STRING, q, p);
            cur = cur->next;
            continue;
        }
        if(p[0] == '\'') {
            const char *q = p;
            p++;
            while(*p != '\'') {
                if(*p == '\0') {
                    report_error(file->name, file->content, q, "Unclosed literal.");
                }
                if(*p == '\n') {
                    report_error(file->name, file->content, q, "Break line inside literal.");
                }
                if (*p == '\\') {
                    // report_error(file->name, file->content, q, "Parse escape sequence in literal");
                    p++;
                }
                p++;
            }
            p++;
            cur->next = new_token(TKN_NUMBER, q, p);
            cur = cur->next;
            continue;
        }
        
        if(is_ident_start(*p)) {
            const char *q = p;
            p++;
            while(is_ident(*p)) {
                p++;
            }
            cur->next = new_token(TKN_IDENT, q, p);
            cur = cur->next;
            continue;
        }
        
        if(is_punctuation(*p)) {
            const char *q = p;
            p++;
            cur->next = new_token(TKN_PUNCTUATION, q, p);
            cur = cur->next;
            continue;
        }


        printf("Unexpected token '%c' -> %d | 0x%X\n", *p, (int)*p, (int)*p);
        report_error(file->name, file->content, p, "Unexpected token.");

    }
    cur->next = new_token(TKN_EOF, p, p);
    return head.next;
}

int main(int argc, const char **argv)
{
    if (argc < 2)
    {
        LOG_ERROR("Invalid number of parameters: %lu", argc);
        return 1;
    }

    Args args = parse_args(argc, argv);
    for (size_t i = 0; i < args.list.count; ++i)
    {
        LOG_INFO("Param %lu) %s", i, args.list.items[i]);

        CharArray content;
        if(read_file_content(args.list.items[i], &content)) {
            return 1;
        }

        // LOG_INFO("File content [%lu]:\n%s", content.count, content.items);
        
        // 0) Clean up code (windows CRLF, unicode symbols, escape sequences) [Optional]
        // 1) Tokenize (Create linked list of elements)
        File file = { .name = args.list.items[i], .content = content.items, .content_size = content.count, .full_path = NULL};
        Token *tok = tokenize(&file);
        if(tok == NULL) return 1;
        // 2) Preprocess (Iterate over the tokens and manage the #<ident> items)
        // 3) Generate code (Iterate over the tokens and generate ASM code for each)
        

        printf(" -- TOKENS --\n");
        Token *it = tok;
        while(it != NULL) {
            printf(" - '%.*s' Type: %d\n", (int)it->len, it->pos, (int)it->type);
            it = it->next;
        }
        printf(" ----\n");


        // Next steps, generalize and support features
        AC_ARRAY_DESTROY(content);
    }
    return 0;
}