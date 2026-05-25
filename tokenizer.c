#include "tokenizer.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/errno.h>
#include <stdarg.h>
#include <string.h>

#define TRACE_ERROR(level, fmt, ...)                             \
    do                                                           \
    {                                                            \
        log_msg_(__FILE__, __LINE__, 0, level, fmt, ##__VA_ARGS__); \
        abort();                                                 \
    } while (0)

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

static char *
copy_string(const char *str, size_t len) {
    char *ret = malloc((len + 1) * sizeof(char *));
    memcpy(ret, str, len);
    ret[len] = '\0';
    return ret;
}

static Token *
token_new(TokenKind type, const char *start, const char *end)
{
    Token *tok = calloc(1, sizeof(Token));
    tok->type = type;
    tok->pos = start;
    tok->len = end - start;
    tok->is_eol = 0;
    tok->has_spaces = 0;
    tok->next = NULL;

    return tok;
}

// static void
// token_dump(Token *t) {
//     printf(" > TOKEN(%p): %.*s [%d]\n", t->pos, (int)t->len, t->pos, t->type);
// }

static Token *
token_copy(Token *src) 
{
    Token *tok = calloc(1, sizeof(Token));
    *tok = *src;
    return tok;
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
is_punctuation(char c) {
    // TODO: first chech punctuation combinations
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
report_msg(const char *filename, const char *start, const char *pos, const char *msg) {

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

}

static void
report_error(const char *filename, const char *start, const char *pos, const char *msg) {
    report_msg(filename, start, pos, msg);
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
            while(*p != 0 && *p != '\n') {
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
            cur->has_spaces = 1;
            p++;
            continue;
        }

        // TODO: remove this check, that should be part of the pre cleaning
        if(p[0] == '\\' && p[1] == '\n') {
            p += 2;
            continue;
        }


        if(*p == '\n') {
            cur->is_eol = 1;
            p++;
            continue;
        }

        if(is_digit(p[0]) || (p[0] == '.' && is_digit(p[1]))) {
            const char *q = p; 
            // TODO: Be more strict, support scientific notation and check valid values
            while(is_digit(*p) || p[0] == '.') {
                p++;
            }
            cur->next = token_new(TKN_NUMBER, q, p);
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
            cur->next = token_new(TKN_STRING, q, p);
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
            cur->next = token_new(TKN_NUMBER, q, p);
            cur = cur->next;
            continue;
        }
        
        if(is_ident_start(*p)) {
            const char *q = p;
            p++;
            while(is_ident(*p)) {
                p++;
            }
            cur->next = token_new(TKN_ID, q, p);
            cur = cur->next;
            continue;
        }
        
        if(is_punctuation(*p)) {
            const char *q = p;
            p++;
            cur->next = token_new(TKN_PUNCTUATION, q, p);
            cur = cur->next;
            continue;
        }
        printf("Unexpected token '%c' -> %d | 0x%X\n", *p, (int)*p, (int)*p);
        report_error(file->name, file->content, p, "Unexpected token.");
    }
    cur->next = token_new(TKN_EOF, p, p);
    return head.next;
}

static Token *
file_tokenize(const char *filename) {
    CharArray content = {0};
    if(read_file_content(filename, &content)) {
        return NULL;
    }

    File file = { .name = filename, .content = content.items, .content_size = content.count, .full_path = NULL};
    return tokenize(&file);
}

int
search_definition(State *state, Token *def) {
    int pos = -1;
    for (size_t i = 0; i < (state->macros).count; i++) { 
        if ((state->macros).items[i]->len == def->len && 
            strncmp(def->pos, ((state->macros).items[i]->pos), def->len) == 0) { 
                pos = i;
                break;
        }
    }
    return pos;
}

Token *
preprocess(Token *tok, File *file, State *state) 
{
    Token *cur = tok;
    
    Token copy = {};
    Token *ccur = &copy;

    while(cur && cur->type != TKN_EOF) {
        // token_dump(cur);
        if(token_equal(cur, "#", TKN_PUNCTUATION)) {
            
            Token *command = cur->next;
            // token_dump(command);

            if(token_equal(command, "include", TKN_ID)) {
                Token *last = NULL;
                Token *open = command->next;
                size_t include_path_len = 0;
                const char *include_path = open->pos;
                Token *it = open;
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
                    Token *include_tokens = NULL;
                    char *filename = copy_string(include_path+1, include_path_len - 2);
                    include_tokens = file_tokenize(filename);
                    if(include_tokens != NULL) {
                        Token *it = include_tokens;
                        while(it->next != NULL && it->next->type != TKN_EOF) {
                            it = it->next;
                        }
                        //TODO: remove EOF token
                        it->next = last->next;
                        cur = include_tokens;
                        continue;
                    } else {
                        LOG_ERROR("Unable to parse include file: %.*s", (int)include_path_len, include_path);
                    }
                }

            } else if(token_equal(command, "define", TKN_ID)) {
                // dump_token(command);
                Token *name = command->next;
                if(name->type != TKN_ID) {
                    report_error(file->name, file->content, name->pos, "Expected a MARO identifier.");
                }
                
                Token *it = cur;
                printf("-> DEFINED:");
                while(it->is_eol == 0) {
                    printf(" %.*s", (int)it->len, it->pos);
                    it = it->next;
                }
                printf(" %.*s", (int)it->len, it->pos);
                printf("\n");
                cur = it->next;
                it->next = token_new(TKN_EOF, NULL, NULL);

                // TODO: Check if already exists and warn if needed
                AC_ARRAY_PUSH(state->macros, name);
                continue;
            } else if(token_equal(command, "if", TKN_ID)) { 
                int remove = 0;
                Token *cond = command->next;
                if(cond->type == TKN_NUMBER) {
                    if(atoi(cond->pos) == 0) {
                        remove = 1;
                    }
                } else if(cond->type == TKN_ID) {
                    int pos = search_definition(state, cond);
                    if(pos != -1) {
                        Token *def = state->macros.items[pos];
                        if(!def->is_eol && def->next->type == TKN_NUMBER && atoi(def->next->pos) == 0) {
                            remove = 1;
                        }
                    }
                }
                
                state->cond_block_level++;
                if(remove) {
                    while(cur->type != TKN_EOF) {
                        cur = cur->next;
                        if(token_equal(cur, "#", TKN_PUNCTUATION) &&
                            token_equal(cur->next, "endif", TKN_ID)) {
                                state->cond_block_level--;
                                cur = cur->next;
                                break;
                        }
                    }
                }

            } else if(token_equal(command, "ifdef", TKN_ID) ||
                    token_equal(command, "ifndef", TKN_ID)) { 

                // TODO("Implement #ifdef");
                int must_be_defined = 0;
                if(token_equal(command, "ifdef", TKN_ID)) {
                    must_be_defined = 1;
                }
                Token *def = command->next;
                int pos = search_definition(state, def);
                cur = def;
                
                // skip the entire block
                state->cond_block_level++;
                if((must_be_defined == 0 && pos != -1) || 
                    (must_be_defined == 1 && pos == -1)) {

                    while(cur->type != TKN_EOF) {
                        cur = cur->next;
                        if(token_equal(cur, "#", TKN_PUNCTUATION) &&
                            token_equal(cur->next, "endif", TKN_ID)) {

                                state->cond_block_level--;
                                cur = cur->next;
                                break;
                        }
                    }
                }
            } else if(token_equal(command, "elif", TKN_ID)) { 
                TODO("Implement #elif");
            } else if(token_equal(command, "else", TKN_ID)) { 
                TODO("Implement #else");
            } else if(token_equal(command, "endif", TKN_ID)) { 
                // TODO("Implement #endif");
                cur = command;
                if(state->cond_block_level > 0) {
                    state->cond_block_level--;
                } else {
                    report_error(file->name, file->content, command->pos, "No conditional block related with this close.");
                }
            } else if(token_equal(command, "undef", TKN_ID)) {
                Token *name = command->next;
                if(name->type != TKN_ID || !name->is_eol) {
                    report_msg(file->name, file->content, name->next->pos, "extra tokens at end of #undef directive");
                }
                
                int pos = search_definition(state, name);
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
                TODO("Implement #error");
            } else {
                report_error(file->name, file->content, command->pos, "Unknown preprocessor command.");
            }
        } else {
            // TODO! Check if the token is a macro and replace it!

            ccur->next = token_copy(cur);
            ccur = ccur->next;
        }

        cur = cur->next;
    }
    return copy.next;
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

        CharArray content = {0};
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
        
        State state = {0};
        Token *out = preprocess(tok, &file, &state);
        
        // 3) Generate code (Iterate over the tokens and generate ASM code for each)

        // DUMP 
        printf("[ TOKENS ]\n");
        Token *it = out;
        while(it != NULL) {
            printf("%.*s ", (int)it->len, it->pos);
            it = it->next;
        }
        printf("\n----\n");
        printf("[ MACROS ]\n");
        for(size_t i = 0; i < state.macros.count; ++i) {
            Token *t = state.macros.items[i];
            printf(" > %.*s\n", (int)t->len, t->pos);
        }

        // TODO: Destroy tokens

        AC_ARRAY_DESTROY(content);
    }
    return 0;
}