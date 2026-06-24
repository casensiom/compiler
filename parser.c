

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct Token {
    const char *pos;
    size_t len;
    Token *next;
} Token;

typedef enum NodeType {
    NT_UNKNOWN,
    NT_IDENTIFIER,
    NT_CONSTANT,
    NT_STRING_LITERAL,
    NT_AUTO,               // "auto"
    NT_BREAK,              // "break"
    NT_CASE,               // "case"
    NT_CHAR,               // "char"
    NT_CONST,              // "const"
    NT_CONTINUE,           // "continue"
    NT_DEFAULT,            // "default"
    NT_DO,                 // "do"
    NT_DOUBLE,             // "double"
    NT_ELSE,               // "else"
    NT_ENUM,               // "enum"
    NT_EXTERN,             // "extern"
    NT_FLOAT,              // "float"
    NT_FOR,                // "for"
    NT_GOTO,               // "goto"
    NT_IF,                 // "if"
    NT_INT,                // "int"
    NT_LONG,               // "long"
    NT_REGISTER,           // "register"
    NT_RETURN,             // "return"
    NT_SHORT,              // "short"
    NT_SIGNED,             // "signed"
    NT_SIZEOF,             // "sizeof"
    NT_STATIC,             // "static"
    NT_STRUCT,             // "struct"
    NT_SWITCH,             // "switch"
    NT_TYPEDEF,            // "typedef"
    NT_UNION,              // "union"
    NT_UNSIGNED,           // "unsigned"
    NT_VOID,               // "void"
    NT_VOLATILE,           // "volatile"
    NT_WHILE,              // "while"
    NT_ELLIPSIS,           // "..."
    NT_RIGHT_ASSIGN,       // ">>="
    NT_LEFT_ASSIGN,        // "<<="
    NT_ADD_ASSIGN,         // "+="
    NT_SUB_ASSIGN,         // "-="
    NT_MUL_ASSIGN,         // "*="
    NT_DIV_ASSIGN,         // "/="
    NT_MOD_ASSIGN,         // "%="
    NT_AND_ASSIGN,         // "&="
    NT_XOR_ASSIGN,         // "^="
    NT_OR_ASSIGN,          // "|="
    NT_RIGHT_OP,           // ">>"
    NT_LEFT_OP,            // "<<"
    NT_INC_OP,             // "++"
    NT_DEC_OP,             // "--"
    NT_PTR_OP,             // "|"
    NT_AND_OP,             // "&&"
    NT_OR_OP,              // "||"
    NT_LE_OP,              // "<="
    NT_GE_OP,              // ">="
    NT_EQ_OP,              // "=="
    NT_NE_OP,              // "!="
    NT_SEMICOLON_SIGN,     // ";"
    NT_LCURL_BRACKET_SIGN, // "{"
    NT_RCURL_BRACKET_SIGN, // "}"
    NT_COMMA_SIGN,         // ","
    NT_COLON_SIGN,         // ":"
    NT_EQUALS_SIGN,        // "="
    NT_LPARENT_SIGN,       // "("
    NT_RPARENT_SIGN,       // ")"
    NT_LSQR_BRACKET_SIGN,  // "["
    NT_RSQR_BRACKET_SIGN,  // "]"
    NT_PERIOD_SIGN,        // "."
    NT_AMPERSAND_SIGN,     // "&"
    NT_EXCLAMATION_SIGN,   // "!"
    NT_TILDE_SIGN,         // "~"
    NT_MINUS_SIGN,         // "-"
    NT_PLUS_SIGN,          // "+"
    NT_ASTERISK_SIGN,      // "*"
    NT_SOLIDUS_SIGN,       // "/"
    NT_PERCENT_SIGN,       // "%"
    NT_LESS_THAN_SIGN,     // "<"
    NT_GREATER_THAN_SIGN,  // ">"
    NT_CIRCUMFLEX_SIGN,    // "^"
    NT_VERTICAL_SIGN,      // "|"
    NT_QUESTION_SIGN       // "?"
} NodeType;

typedef struct Node {

} Node;

void report_error();
int token_equal(Token *, const char *);

// repeated code could be sustituted by these methods
int token_cursor_optional(Token **tok, const char *name) {
    if (token_equal(*tok, name)) {
        *tok = (*tok)->next;
        return 1;
    }
    return 0;
}
void token_cursor_mandatory(Token **tok, const char *name) {
    if (!token_equal(*tok, name)) {
        report_error();
    }
    *tok = (*tok)->next;
}

// # Parser

/*
 * All methods below define a rule from the ISO standard.
 * The ruleas are converted from ISO grammar (BNF) to a compact notation (EBNF), using this format:
 * A?      optional
 * A*      zero or more
 * A+      one or more
 * (A|B)   alternatives
 */

// ## EXPRESSIONS

/*
 * Definitions:
 *   D                    [0-9]
 *   L                    [a-zA-Z_]
 *   H                    [a-fA-F0-9]
 *   E                    [Ee][+-]?{D}+
 *   FS                   (f|F|l|L)
 *   IS                   (u|U|l|L)*
 *
 * IDENTIFIER:            {L}({L}|{D})*
 * CONSTANT HEX:          0[xX]{H}+{IS}?
 * CONSTANT OCT:          0{D}+{IS}?
 * CONSTANT DEC:          {D}+{IS}?
 * CONSTANT CHAR:         L?'(\\.|[^\\'])+'
 * CONSTANT FLOAT:        {D}+{E}{FS}?
 * CONSTANT FLOAT:        {D}*"."{D}+({E})?{FS}?
 * CONSTANT FLOAT:        {D}+"."{D}*({E})?{FS}?
 * STRING_LITERAL:        L?\"(\\.|[^\\"])*\"
 *
 */

// primary-expression = identifier | constant | string-literal+ | "(" expression ")"
Node *parse_postfix_expression(Token **token) {
    Node *ret = 0;
    Token *tok = *token;
    if (!tok) {
        report_error();
    }

    ret = parse_identifier(&tok);
    if (ret) {
        *token = tok;
        return ret;
    }

    ret = parse_constant(&tok);
    if (ret) {
        *token = tok;
        return ret;
    }

    ret = parse_string(&tok);
    if (ret) {
        while (1) {
            Node *next = parse_string(&tok);
            if (!next) {
                break;
            }
            ret = node_make_string(ret, next);
        }
        *token = tok;
        return ret;
    }

    if (token_equal(tok, "(")) {
        tok = tok->next;
        ret = parse_expression(&tok);
        if (!ret) {
            report_error();
        }

        if (!token_equal(tok, ")")) {
            report_error();
        }
        tok = tok->next;
    }

    return ret;
}

// postfix-expression = primary-expression ( "[" expression "]" | "(" argument-expression-list? ")" | "." identifier | "->" identifier | "++" | "--" | "(" type-name ")" "{" initializer-list ","? "}" )*
Node *parse_postfix_expression(Token **token) {
    Node *ret = 0;
    Token *tok = *token;
    if (!tok) {
        report_error();
    }

    ret = parse_primary_expression(&tok);
    if (!ret) {
        return 0;
    }

    while (1) {
        Node *next = 0;
        if (token_equal(tok, "[")) {
            tok = tok->next;
            Node *expr = parse_expression(&tok);
            if (!expr) {
                report_error();
            }
            if (!token_equal(tok, "]")) {
                report_error();
            }
            tok = tok->next;
            next = node_make_array_index(ret, expr);
        } else if (token_equal(tok, "(")) {
            tok = tok->next;
            Node *args = parse_argument_expression_list(&tok);
            if (!token_equal(tok, ")")) {
                report_error();
            }
            tok = tok->next;
            next = node_make_invoke_arguments(ret, args);
        } else if (token_equal(tok, ".")) {
            tok = tok->next;
            Node *property = parse_identifier(&tok);
            if (!property) {
                report_error();
            }
            next = node_make_member_access(ret, property);
        } else if (token_equal(tok, "->")) {
            tok = tok->next;
            Node *property = parse_identifier(&tok);
            if (!property) {
                report_error();
            }
            next = node_make_pointer_access(ret, property);
        } else if (token_equal_any(tok, "++")) {
            tok = tok->next;
            next = node_make_post_increment(ret);
        } else if (token_equal_any(tok, "--")) {
            tok = tok->next;
            next = node_make_post_decrement(ret);
        } else if (token_equal_any(tok, "(")) { // "(" type-name ")" "{" initializer-list ","? "}"
            tok = tok->next;
            Node *type_name = parse_type_name(&tok);
            if (!token_equal(tok, ")")) {
                report_error();
            }
            tok = tok->next;
            if (!token_equal(tok, "{")) {
                report_error();
            }
            tok = tok->next;

            Node *property = parse_initializer_list(&tok);
            if (token_equal(tok, ",")) {
                tok = tok->next;
            }

            if (!token_equal(tok, "}")) {
                report_error();
            }
            tok = tok->next;
            next = node_make_compound(type_name, property);
        }

        if (!next) {
            break;
        }
        ret = make_node_postfix(ret, next);
    };

    *token = tok;
    return ret;
}

// argument-expression-list = assignment-expression ("," assignment-expression)*
Node *parse_argument_expression_list(Token **token) {
    Token *tok = *token;
    if (!tok) {
        report_error();
    }

    Node *ret = parse_assignment_expression(&tok);
    while (tok && token_equal(tok, ",")) {
        tok = tok->next;
        ret = make_node_argument_expression_list(ret, parse_assignment_expression(&tok));
    }
    *token = tok;
    return ret;
}

// unary-expression = postfix-expression | "++" unary-expression | "--" unary-expression | unary-operator cast-expression | "sizeof" unary-expression | "sizeof" "(" type-name ")"
Node *parse_unary_expression(Token **token) {
    Node *ret = 0;
    Token *tok = *token;
    if (!tok) {
        report_error();
    }

    if (token_equal_any(tok, "++", "--")) {
        NodeType type = node_type_from_token(tok);
        tok = tok->next;
        ret = parse_unary_expression(&tok);
        if (!ret) {
            report_error();
        }
        *token = tok;
        return ret;
    }

    if (token_equal(tok, "sizeof")) {
        tok = tok->next;
        if (token_equal(tok, "(")) {
            ret = parse_type_name(&tok);
            if (!token_equal(tok, ")")) {
                report_error();
            }
            tok = tok->next;
        } else {
            ret = parse_unary_expression(&tok);
        }
        if (!ret) {
            report_error();
        }
        *token = tok;
        return ret;
    }

    ret = parse_postfix_expression(&tok);
    if (ret) {
        *token = tok;
        return ret;
    }

    ret = parse_unary_expression(&tok);
    if (ret) {
        Node *cast_expr = parse_cast_expression(&tok);
        if (!cast_expr) {
            report_error();
        }
        *token = tok;
        return make_node_unary(ret, cast_expr);
    }

    return ret;
}

// unary-operator = "&" | "*" | "+" | "-" | "~" | "!"
Node *parse_unary_operator(Token **token) {
    Token *tok = *token;
    if (!tok) {
        report_error();
    }

    Node *ret = 0;
    if (token_equal_any(tok, "&", "*", "+", "-", "~", "!")) {
        ret = make_node_unary(tok);
        tok = tok->next;
    }

    *token = tok;
    return ret;
}

// cast-expression = unary-expression | "(" type-name ")" cast-expression
Node *parse_cast_expression(Token **token) {
    Token *tok = *token;
    if (!tok) {
        report_error();
    }

    Node *ret = parse_unary_expression(&tok);
    if (!ret && token_equal(tok, "(")) {
        tok = tok->next;

        Node *type_name = parse_type_name(&tok);
        if (!token_equal(tok, ")")) {
            report_error();
        }
        tok = tok->next;

        Node *cast_expr = parse_cast_expression(&tok);
        if (!cast_expr) {
            report_error();
        }
        ret = make_node_cast(type_name, cast_expr);
    }

    *token = tok;
    return ret;
}

// multiplicative-expression = cast-expression ( ("*" | "/" | "%") cast-expression )*
Node *parse_multiplicative_expression(Token **token) {
    Token *tok = *token;
    if (!tok) {
        report_error();
    }

    Node *ret = parse_cast_expression(&tok);
    while (tok && (token_equal_any(tok, "*", "/", "%"))) {
        NodeType type = node_type_from_token(tok);
        tok = tok->next;
        ret = make_node_binary(ret, type, parse_cast_expression(&tok));
    }
    *token = tok;
    return ret;
}

// additive-expression = multiplicative-expression ( ("+" | "-") multiplicative-expression )*
Node *parse_additive_expression(Token **token) {
    Token *tok = *token;
    if (!tok) {
        report_error();
    }

    Node *ret = parse_multiplicative_expression(&tok);
    while (tok && (token_equal_any(tok, "+", "-"))) {
        int is_plus = token_equal(tok, "+");
        tok = tok->next;
        ret = make_node_binary(ret, is_plus ? NT_PLUS_SIGN : NT_MINUS_SIGN, parse_multiplicative_expression(&tok));
    }
    *token = tok;
    return ret;
}

// shift-expression = additive-expression ( ("<<" | ">>") additive-expression )*
Node *parse_shift_expression(Token **token) {
    Token *tok = *token;
    if (!tok) {
        report_error();
    }

    Node *ret = parse_additive_expression(&tok);
    while (tok && (token_equal_any(tok, "<<", ">>"))) {
        int is_left = token_equal(tok, "<<");
        tok = tok->next;
        ret = make_node_binary(ret, is_left ? NT_LEFT_OP : NT_RIGHT_OP, parse_additive_expression(&tok));
    }
    *token = tok;
    return ret;
}

// relational-expression = shift-expression ( ("<" | ">" | "<=" | ">=") shift-expression )*
Node *parse_relational_expression(Token **token) {
    Token *tok = *token;
    if (!tok) {
        report_error();
    }

    Node *ret = parse_shift_expression(&tok);
    while (tok && (token_equal_any(tok, "<", ">", "<=", ">="))) {
        NodeType type = node_type_from_token(tok);
        tok = tok->next;
        ret = make_node_binary(ret, type, parse_shift_expression(&tok));
    }
    *token = tok;
    return ret;
}

// equality-expression = relational-expression ( ("==" | "!=") relational-expression )*
Node *parse_equality_expression(Token **token) {
    Token *tok = *token;
    if (!tok) {
        report_error();
    }

    Node *ret = parse_relational_expression(&tok);
    while (tok && (token_equal_any(tok, "==", "!="))) {
        int is_equal = token_equal(tok, "==");
        tok = tok->next;
        ret = make_node_binary(ret, is_equal ? NT_EQ_OP : NT_NE_OP, parse_relational_expression(&tok));
    }
    *token = tok;
    return ret;
}

// and-expression = equality-expression ( "&" equality-expression )*
Node *parse_and_expression(Token **token) {
    Token *tok = *token;
    if (!tok) {
        report_error();
    }

    Node *ret = parse_equality_expression(&tok);
    while (tok && token_equal(tok, "&")) {
        tok = tok->next;
        ret = make_node_binary(ret, NT_AMPERSAND_SIGN, parse_equality_expression(&tok));
    }
    *token = tok;
    return ret;
}

// exclusive-or-expression = and-expression ( "^" and-expression )*
Node *parse_exclusive_or_expressio(Token **token) {
    Token *tok = *token;
    if (!tok) {
        report_error();
    }

    Node *ret = parse_and_expression(&tok);
    while (tok && token_equal(tok, "^")) {
        tok = tok->next;
        ret = make_node_binary(ret, NT_CIRCUMFLEX_SIGN, parse_and_expression(&tok));
    }
    *token = tok;
    return ret;
}

// inclusive-or-expression = exclusive-or-expression ( "|" exclusive-or-expression )*
Node *parse_inclusive_or_expression(Token **token) {
    Token *tok = *token;
    if (!tok) {
        report_error();
    }

    Node *ret = parse_exclusive_or_expression(&tok);
    while (tok && token_equal(tok, "|")) {
        tok = tok->next;
        ret = make_node_binary(ret, NT_VERTICAL_SIGN, parse_exclusive_or_expression(&tok));
    }
    *token = tok;
    return ret;
}

// logical-and-expression = inclusive-or-expression ( "&&" inclusive-or-expression )*
Node *parse_logical_and_expression(Token **token) {
    Token *tok = *token;
    if (!tok) {
        report_error();
    }

    Node *ret = parse_inclusive_or_expression(&tok);
    while (tok && token_equal(tok, "&&")) {
        tok = tok->next;
        ret = make_node_binary(ret, NT_AND_OP, parse_inclusive_or_expression(&tok));
    }
    *token = tok;
    return ret;
}

// logical-or-expression = logical-and-expression ( "||" logical-and-expression )*
Node *parse_logical_or_expression(Token **token) {
    Token *tok = *token;
    if (!tok) {
        report_error();
    }

    Node *ret = parse_logical_and_expression(&tok);
    while (tok && token_equal(tok, "||")) {
        tok = tok->next;
        ret = make_node_binary(ret, NT_OR_OP, parse_logical_and_expression(&tok));
    }
    *token = tok;
    return ret;
}

// conditional-expression = logical-or-expression ( "?" expression ":" conditional-expression )?
Node *parse_conditional_expression(Token **token) {
    Token *tok = *token;
    if (!tok) {
        report_error();
    }

    Node *ret = parse_logical_or_expression(&tok);
    if (token_equal(tok, "?")) {
        tok = tok->next;

        Node *expr = parse_expression(&tok);
        if (!expr) {
            report_error();
        }

        if (!token_equal(tok, ":")) {
            report_error();
        }
        tok = tok->next;

        Node *cond = parse_conditional_expression(&tok);
        if (!cond) {
            report_error();
        }
        ret = make_node_conditional(ret, expr, cond);
    }

    *token = tok;
    return ret;
}

// assignment-expression = conditional-expression | unary-expression assignment-operator assignment-expression
Node *parse_assignment_expression(Token **token) {
    Token *tok = *token;
    if (!tok) {
        report_error();
    }

    Node *ret = parse_conditional_expression(&tok);
    if (!ret) {
        Node *unary = parse_unary_expression(&tok);
        if (!unary) {
            report_error();
        }
        Node *op = parse_assignment_operator(&tok);
        if (!op) {
            report_error();
        }
        Node *assigment = parse_assignment_expression(&tok);
        if (!assigment) {
            report_error();
        }
        ret = make_node_binary(unary, op, assigment);
    }

    *token = tok;
    return ret;
}

// assignment-operator = "=" | "*=" | "/=" | "%=" | "+=" | "-=" | "<<=" | ">>=" | "&=" | "^=" | "|="
Node *parse_assignment_operator(Token **token) {
    Token *tok = *token;
    if (!tok) {
        report_error();
    }

    Node *ret = 0;
    if (token_equal_any(tok, "=", "*=", "/=", "%=", "+=", "-=", "<<=", ">>=", "&=", "^=", "|=")) {
        ret = make_node_assignment(tok);
        tok = tok->next;
    }

    *token = tok;
    return ret;
}

// expression = assignment-expression ( "," assignment-expression )*
Node *parse_expression(Token **token) {
    Token *tok = *token;
    if (!tok) {
        report_error();
    }

    Node *ret = parse_assignment_expression(&tok);
    while (tok && token_equal(tok, ",")) {
        tok = tok->next;
        ret = make_node(ret, parse_assignment_expression(&tok));
    }
    if (!ret) {
        report_error();
    }
    *token = tok;
    return ret;
}

// constant-expression = conditional-expression
Node *parse_constant_expression(Token **token) {
    Token *tok = *token;
    if (!tok) {
        report_error();
    }

    Node *ret = parse_conditional_expression(&tok);

    *token = tok;
    return ret;
}

// Node *parse_(Token **token) {
//     Token *tok = *token;
//    if(!tok) {
//        report_error();
//    }
//     Node *ret = 0;

//     *token = tok;
//     return ret;
// }