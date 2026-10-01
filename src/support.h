#pragma once

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define FALSE 0
#define TRUE 1

enum KeywordType {
    KEYWORD_IF,
    KEYWORD_ELSE,
    KEYWORD_WHILE,
    KEYWORD_FOR,
    KEYWORD_RETURN,
    KEYWORD_BREAK,
    KEYWORD_CONTINUE,
    KEYWORD_INT,
    KEYWORD_CHAR,
    KEYWORD_VOID,
    KEYWORD_FLOAT,
    KEYWORD_DOUBLE,
    KEYWORD_STRUCT,
    KEYWORD_UNION,
    KEYWORD_ENUM,
    KEYWORD_TYPEDEF,
    KEYWORD_CONST,
    KEYWORD_STATIC,
    KEYWORD_EXTERN,
    KEYWORD_REGISTER,
    KEYWORD_VOLATILE,
    KEYWORD_INLINE,
    KEYWORD_NORETURN,
    KEYWORD_ALIGNAS,
    KEYWORD_ASM
} ;

typedef struct {
    const char *name;
    enum KeywordType type;
} Keyword;

extern Keyword keywords[];

typedef enum {
    TOKEN_EOF,
    TOKEN_KEYWORD,
    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_FLOAT_LITERAL,
    TOKEN_STRING,
    TOKEN_CHAR,
    TOKEN_OPERATOR
} TokenType;

extern TokenType token_type;
extern char token_text[1024];
extern int token_num_value;
extern double token_float_value;
extern enum KeywordType token_keyword;

// Points the lexer at an in-memory, null-terminated source buffer (the
// preprocessor's output) and primes 'look' with the first character.
void lexer_set_source(const char *text);

int c_get_next_char();
int c_peek_char();
int c_is_digit();
int c_is_alpha();
int c_is_alnum();
int c_is_whitespace();
void c_skip_whitespace();
int c_is_addop();
int c_is_mulop();
int c_is_paren();
int c_is_eof();
int c_is_equals();
int c_is_semicolon();
int c_is_colon();
int c_is_comma();
int c_is_dot();
int c_is_question();
int c_is_exclamation(); 
int c_is_double_quote();
int c_is_single_quote();
char *c_read_string();
char *c_read_char();
char *c_read_identifier();
char *c_read_number();
char *c_read_asm_block();
void c_read_operator();
Keyword *c_lookup_keyword(char *str);
int c_is_keyword(char *str);
void next_token();
