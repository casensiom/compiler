#ifndef _TOKENIZER_H_
#define _TOKENIZER_H_

#include "array.h"

typedef char Char;
AC_ARRAY_DEFINE(Char);

typedef CharArray String;
AC_ARRAY_DEFINE(String);

typedef  const char * FileName;
AC_ARRAY_DEFINE(FileName);

typedef struct Args {
    FileNameArray list;
} Args;

typedef enum TokenKind {
    TKN_START,
    TKN_IDENT,
    TKN_KEYWORD,
    TKN_NUMBER,
    TKN_STRING,
    TKN_PUNCTUATION,
    TKN_EOF
} TokenKind;

typedef struct Token {
    TokenKind type;
    const char *pos;
    size_t len;
    size_t line;
    size_t column;
    const char *filename;

    struct Token *next;
} Token;

typedef struct File {
    const char *name;
    const char *full_path;
    const char *content;
    size_t content_size;
} File;

typedef struct FileContent {
    const char *name;
    size_t line_start;
    size_t line_end;
} FileContent;


#endif