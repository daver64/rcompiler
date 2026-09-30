#include "support.h"

int look=0;
TokenType token_type = TOKEN_EOF;
char token_text[1024] = {0};
int token_num_value = 0;
enum KeywordType token_keyword;

static const char *source_buf = NULL;
static size_t source_pos = 0;

void lexer_set_source(const char *text)
{
    source_buf = text;
    source_pos = 0;
    c_get_next_char(); // prime 'look' with the first character
}

Keyword keywords[] = {
    {"if", KEYWORD_IF},
    {"else", KEYWORD_ELSE},
    {"while", KEYWORD_WHILE},
    {"for", KEYWORD_FOR},
    {"return", KEYWORD_RETURN},
    {"break", KEYWORD_BREAK},
    {"continue", KEYWORD_CONTINUE},
    {"int", KEYWORD_INT},
    {"char", KEYWORD_CHAR},
    {"void", KEYWORD_VOID},
    {"struct", KEYWORD_STRUCT},
    {"union", KEYWORD_UNION},
    {"enum", KEYWORD_ENUM},
    {"typedef", KEYWORD_TYPEDEF},
    {"const", KEYWORD_CONST},
    {"static", KEYWORD_STATIC},
    {"extern", KEYWORD_EXTERN},
    {"register", KEYWORD_REGISTER},
    {"volatile", KEYWORD_VOLATILE},
    {"inline", KEYWORD_INLINE},
    {"noreturn", KEYWORD_NORETURN},
    {"alignas", KEYWORD_ALIGNAS},
    {"asm", KEYWORD_ASM}
};

int c_get_next_char()
{
    if(!source_buf || source_buf[source_pos] == '\0')
    {
        look = EOF;
    }
    else
    {
        look = (unsigned char)source_buf[source_pos];
        source_pos++;
    }
    return look;
}

// Look at the next character without consuming it (leaves 'look' untouched).
int c_peek_char()
{
    if(!source_buf || source_buf[source_pos] == '\0')
    {
        return EOF;
    }
    return (unsigned char)source_buf[source_pos];
}

int c_is_digit()
{
    return look >= '0' && look <= '9';
}

int c_is_alpha()
{
    return (look >= 'a' && look <= 'z') || (look >= 'A' && look <= 'Z') || look == '_';
}

int c_is_alnum()
{
    return c_is_alpha() || c_is_digit();
}

int c_is_whitespace()
{
    return look == ' ' || look == '\t' || look == '\n' || look == '\r';
}

void c_skip_whitespace()
{
    while(TRUE)
    {
        while(c_is_whitespace())
        {
            c_get_next_char();
        }
        if(look == '/' && c_peek_char() == '/')
        {
            while(look != '\n' && look != EOF)
            {
                c_get_next_char();
            }
            continue;
        }
        if(look == '/' && c_peek_char() == '*')
        {
            c_get_next_char(); // look becomes '*'
            c_get_next_char(); // advance past '*'
            while(!(look == '*' && c_peek_char() == '/') && look != EOF)
            {
                c_get_next_char();
            }
            if(look == '*')
            {
                c_get_next_char(); // look becomes '/'
                c_get_next_char(); // advance past '/'
            }
            continue;
        }
        break;
    }
}

int c_is_addop()
{
    return look == '+' || look == '-';
}

int c_is_mulop()
{
    return look == '*' || look == '/';
}

int c_is_paren()
{
    return look == '(' || look == ')';
}

int c_is_eof()
{
    return look == EOF;
}

int c_is_equals()
{
    return look == '=';
}

int c_is_semicolon()
{
    return look == ';';
}

int c_is_colon()
{
    return look == ':';
}

int c_is_comma()
{
    return look == ',';
}

int c_is_dot()
{
    return look == '.';
}

int c_is_question()
{
    return look == '?';
}

int c_is_exclamation()
{
    return look == '!';
}

int c_is_double_quote()
{
    return look == '"';
}

int c_is_single_quote()
{
    return look == '\'';
}

// Decodes a backslash escape (look=='\\' on entry), leaving 'look' on the
// char after the sequence. Returns the decoded byte value.
static int c_decode_escape()
{
    c_get_next_char(); // consume '\', look is now the escape letter
    int result;
    switch(look)
    {
        case 'n': result = '\n'; break;
        case 't': result = '\t'; break;
        case 'r': result = '\r'; break;
        case '0': result = '\0'; break;
        case '\\': result = '\\'; break;
        case '\'': result = '\''; break;
        case '"': result = '"'; break;
        default: result = look; break;
    }
    c_get_next_char(); // consume the escape letter
    return result;
}

char *c_read_string()
{
    if(!c_is_double_quote())
    {
        return NULL;
    }
    c_get_next_char(); // Skip the opening double quote
    static char buffer[1024];
    int i = 0;
    while(look != '"' && look != EOF)
    {
        if(look == '\\')
        {
            buffer[i++] = (char)c_decode_escape();
        }
        else
        {
            buffer[i++] = look;
            c_get_next_char();
        }
    }
    buffer[i] = '\0';
    if(look == '"')
    {
        c_get_next_char(); // Skip the closing double quote
    }
    return buffer;
}

char *c_read_char()
{
    if(!c_is_single_quote())
    {
        return NULL;
    }
    c_get_next_char(); // Skip the opening single quote
    static char buffer[4]; // To accommodate escape sequences like '\n'
    int i = 0;
    while(look != '\'' && look != EOF)
    {
        if(look == '\\')
        {
            buffer[i++] = (char)c_decode_escape();
        }
        else
        {
            buffer[i++] = look;
            c_get_next_char();
        }
    }
    buffer[i] = '\0';
    if(look == '\'')
    {
        c_get_next_char(); // Skip the closing single quote
    }
    return buffer;
}

char *c_read_identifier()
{
    if(!c_is_alpha())
    {
        return NULL;
    }
    static char buffer[1024];
    int i = 0;
    while(c_is_alnum())
    {
        buffer[i++] = look;
        c_get_next_char();
    }
    buffer[i] = '\0';
    return buffer;
}

char *c_read_number()
{
    if(!c_is_digit())
    {
        return NULL;
    }
    static char buffer[64];
    int i = 0;
    while(c_is_digit())
    {
        buffer[i++] = look;
        c_get_next_char();
    }
    buffer[i] = '\0';
    return buffer;
}

// Reads a 1 or 2 char operator/punctuation token, leaving 'look' on the char after it.
void c_read_operator()
{
    static const char *two_char_ops[] = {
        "==", "!=", "<=", ">=", "&&", "||", "++", "--",
        "+=", "-=", "*=", "/=", "->", NULL
    };
    char buf[3];
    buf[0] = (char)look;
    buf[1] = '\0';
    int next = c_peek_char();
    for(int i = 0; two_char_ops[i] != NULL; i++)
    {
        if(two_char_ops[i][0] == look && two_char_ops[i][1] == next)
        {
            c_get_next_char(); // look becomes the second char
            buf[1] = (char)look;
            buf[2] = '\0';
            break;
        }
    }
    c_get_next_char(); // advance past the operator
    strcpy(token_text, buf);
}

Keyword *c_lookup_keyword(char *str)
{
    for(int i = 0; i < sizeof(keywords)/sizeof(keywords[0]); i++)
    {
        if(strcmp(str, keywords[i].name) == 0)
        {
            return &keywords[i];
        }
    }
    return NULL;
}

int c_is_keyword(char *str)
{
    return c_lookup_keyword(str) != NULL;
}

void next_token()
{
    c_skip_whitespace();
    token_text[0] = '\0';
    token_num_value = 0;

    if(c_is_eof())
    {
        token_type = TOKEN_EOF;
        return;
    }
    if(c_is_alpha())
    {
        strcpy(token_text, c_read_identifier());
        Keyword *kw = c_lookup_keyword(token_text);
        if(kw)
        {
            token_type = TOKEN_KEYWORD;
            token_keyword = kw->type;
        }
        else
        {
            token_type = TOKEN_IDENTIFIER;
        }
        return;
    }
    if(c_is_digit())
    {
        strcpy(token_text, c_read_number());
        token_num_value = atoi(token_text);
        token_type = TOKEN_NUMBER;
        return;
    }
    if(c_is_double_quote())
    {
        char *str = c_read_string();
        strcpy(token_text, str ? str : "");
        token_type = TOKEN_STRING;
        return;
    }
    if(c_is_single_quote())
    {
        char *ch = c_read_char();
        strcpy(token_text, ch ? ch : "");
        token_type = TOKEN_CHAR;
        return;
    }
    c_read_operator();
    token_type = TOKEN_OPERATOR;
}

