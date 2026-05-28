#ifndef _TOKENIZER_H_
#define _TOKENIZER_H_

#include "array.h"

typedef char Char;
AC_ARRAY_DEFINE(Char);

typedef char *CharPtr;
AC_ARRAY_DEFINE(CharPtr);

typedef const char *ConstCharPtr;
AC_ARRAY_DEFINE(ConstCharPtr);

typedef struct Args {
    ConstCharPtrArray file_list;
} Args;

typedef enum TokenKind { TKN_UNKNOWN, TKN_START, TKN_ID, TKN_KEYWORD, TKN_NUMBER, TKN_STRING, TKN_PUNCTUATION, TKN_EOF } TokenKind;

typedef struct TokenLoc {
    size_t      line;
    size_t      column;
    const char *filename;
} TokenLoc;

typedef struct Token {
    TokenKind   type;
    const char *pos;
    size_t      len;
    int         is_eol;
    int         has_spaces;

    TokenLoc location;

    struct Token *next;
    // --
    struct Token *from_macro;    // if expanded from object-like macro
    char         *value;         // if composed macro ##
} Token;
typedef Token *TokenPtr;
AC_ARRAY_DEFINE(TokenPtr);

typedef struct MacroArg {
    Token *macro;
    Token *code;
} MacroArg;
AC_ARRAY_DEFINE(MacroArg);

typedef struct File {
    const char *name;
    const char *full_path;
    const char *content;
    size_t      content_size;
} File;

typedef struct FileContent {
    const char *name;
    size_t      line_start;
    size_t      line_end;
} FileContent;

typedef struct State {
    size_t        cond_block_level;
    TokenPtrArray macros;
    CharPtrArray  included;
    CharPtrArray  include_dirs;
    // VariableArray vars;
    // MethodArray methods;
} State;

typedef struct TokenizerError {
    const char *pos;
    const char *message;
} TokenizerError;

static Token *tokenize_literal(const char *pos, TokenLoc token_loc, TokenizerError *error);
static Token *tokenize_string(const char *pos, TokenLoc token_loc, TokenizerError *error);
static Token *tokenize_number(const char *pos, TokenLoc token_loc, TokenizerError *error);
static void   token_delete(Token *tok);

#endif