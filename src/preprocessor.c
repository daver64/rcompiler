#include "preprocessor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_INCLUDE_DEPTH 20

typedef struct Macro {
    char name[64];
    char value[1024];
    struct Macro *next;
} Macro;

static Macro *macro_head = NULL;

static const char *macro_lookup(const char *name)
{
    for(Macro *m = macro_head; m; m = m->next)
    {
        if(strcmp(m->name, name) == 0)
        {
            return m->value;
        }
    }
    return NULL;
}

static void macro_define(const char *name, const char *value)
{
    for(Macro *m = macro_head; m; m = m->next)
    {
        if(strcmp(m->name, name) == 0)
        {
            strncpy(m->value, value, sizeof(m->value) - 1);
            m->value[sizeof(m->value) - 1] = '\0';
            return;
        }
    }
    Macro *m = malloc(sizeof(Macro));
    strncpy(m->name, name, sizeof(m->name) - 1);
    m->name[sizeof(m->name) - 1] = '\0';
    strncpy(m->value, value, sizeof(m->value) - 1);
    m->value[sizeof(m->value) - 1] = '\0';
    m->next = macro_head;
    macro_head = m;
}

static char *read_whole_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if(!f)
    {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(size + 1);
    size_t n = fread(buf, 1, size, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static char *read_whole_stream(FILE *f)
{
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    int c;
    while((c = fgetc(f)) != EOF)
    {
        if(len + 1 >= cap)
        {
            cap *= 2;
            buf = realloc(buf, cap);
        }
        buf[len++] = (char)c;
    }
    buf[len] = '\0';
    return buf;
}

// Fills `out` with the directory portion of `path` (empty if there is none).
static void dirname_of(const char *path, char *out, size_t out_size)
{
    const char *slash = strrchr(path, '/');
    if(!slash)
    {
        out[0] = '\0';
        return;
    }
    size_t len = slash - path;
    if(len >= out_size)
    {
        len = out_size - 1;
    }
    memcpy(out, path, len);
    out[len] = '\0';
}

// Macro-expands one line, copying string/char literal contents verbatim so a
// macro name that happens to appear inside quotes isn't touched. Expansions
// are themselves re-scanned for further macro references (depth-limited to
// guard against self-referential macros).
static void expand_and_emit_line(const char *line, FILE *output, int depth)
{
    if(depth > 32)
    {
        fprintf(stderr, "preprocessor: macro expansion too deep (possible self-reference)\n");
        exit(1);
    }
    const char *p = line;
    while(*p)
    {
        if(*p == '"' || *p == '\'')
        {
            char quote = *p;
            fputc(*p, output);
            p++;
            while(*p && *p != quote)
            {
                if(*p == '\\' && *(p + 1))
                {
                    fputc(*p, output);
                    p++;
                }
                fputc(*p, output);
                p++;
            }
            if(*p == quote)
            {
                fputc(*p, output);
                p++;
            }
        }
        else if(isalpha((unsigned char)*p) || *p == '_')
        {
            const char *start = p;
            while(isalnum((unsigned char)*p) || *p == '_')
            {
                p++;
            }
            char word[128];
            size_t n = p - start;
            if(n >= sizeof(word))
            {
                n = sizeof(word) - 1;
            }
            memcpy(word, start, n);
            word[n] = '\0';
            const char *replacement = macro_lookup(word);
            if(replacement)
            {
                expand_and_emit_line(replacement, output, depth + 1);
            }
            else
            {
                fputs(word, output);
            }
        }
        else
        {
            fputc(*p, output);
            p++;
        }
    }
}

static void preprocess_text(const char *text, const char *base_dir, FILE *output, int depth)
{
    if(depth > MAX_INCLUDE_DEPTH)
    {
        fprintf(stderr, "preprocessor: include depth exceeded (possible cycle)\n");
        exit(1);
    }

    const char *p = text;
    while(*p)
    {
        const char *line_start = p;
        const char *nl = strchr(p, '\n');
        size_t line_len = nl ? (size_t)(nl - line_start) : strlen(line_start);
        char line[2048];
        size_t copy_len = line_len < sizeof(line) - 1 ? line_len : sizeof(line) - 1;
        memcpy(line, line_start, copy_len);
        line[copy_len] = '\0';

        char *trimmed = line;
        while(*trimmed == ' ' || *trimmed == '\t')
        {
            trimmed++;
        }

        if(trimmed[0] == '#')
        {
            char *directive = trimmed + 1;
            while(*directive == ' ' || *directive == '\t')
            {
                directive++;
            }

            if(strncmp(directive, "include", 7) == 0)
            {
                char *rest = directive + 7;
                while(*rest == ' ' || *rest == '\t')
                {
                    rest++;
                }
                char include_path[512];
                int is_system = 0;
                if(*rest == '"')
                {
                    rest++;
                    char *end = strchr(rest, '"');
                    if(!end)
                    {
                        fprintf(stderr, "preprocessor: malformed #include\n");
                        exit(1);
                    }
                    size_t n = end - rest;
                    memcpy(include_path, rest, n);
                    include_path[n] = '\0';
                }
                else if(*rest == '<')
                {
                    is_system = 1;
                    rest++;
                    char *end = strchr(rest, '>');
                    if(!end)
                    {
                        fprintf(stderr, "preprocessor: malformed #include\n");
                        exit(1);
                    }
                    size_t n = end - rest;
                    memcpy(include_path, rest, n);
                    include_path[n] = '\0';
                }
                else
                {
                    fprintf(stderr, "preprocessor: malformed #include\n");
                    exit(1);
                }

                char full_path[1024];
                if(!is_system && base_dir[0] != '\0')
                {
                    snprintf(full_path, sizeof(full_path), "%s/%s", base_dir, include_path);
                }
                else
                {
                    snprintf(full_path, sizeof(full_path), "%s", include_path);
                }

                char *included_text = read_whole_file(full_path);
                if(!included_text && base_dir[0] != '\0')
                {
                    // No real system include path search - fall back to the
                    // including file's directory (covers <foo.h> too).
                    snprintf(full_path, sizeof(full_path), "%s/%s", base_dir, include_path);
                    included_text = read_whole_file(full_path);
                }
                if(!included_text)
                {
                    fprintf(stderr, "preprocessor: cannot open include file '%s'\n", include_path);
                    exit(1);
                }
                char included_dir[900];
                dirname_of(full_path, included_dir, sizeof(included_dir));
                preprocess_text(included_text, included_dir, output, depth + 1);
                free(included_text);
            }
            else if(strncmp(directive, "define", 6) == 0)
            {
                char *rest = directive + 6;
                while(*rest == ' ' || *rest == '\t')
                {
                    rest++;
                }
                char name[64];
                int i = 0;
                while(*rest && (isalnum((unsigned char)*rest) || *rest == '_') && i < 63)
                {
                    name[i++] = *rest++;
                }
                name[i] = '\0';
                while(*rest == ' ' || *rest == '\t')
                {
                    rest++;
                }
                char value[1024];
                strncpy(value, rest, sizeof(value) - 1);
                value[sizeof(value) - 1] = '\0';
                size_t vlen = strlen(value);
                while(vlen > 0 && (value[vlen - 1] == ' ' || value[vlen - 1] == '\t' || value[vlen - 1] == '\r'))
                {
                    value[--vlen] = '\0';
                }
                macro_define(name, value);
            }
            // any other directive (#pragma, #ifdef, ...) is silently skipped;
            // not supported by this simple preprocessor.
        }
        else
        {
            expand_and_emit_line(line, output, 0);
            fputc('\n', output);
        }

        if(!nl)
        {
            break;
        }
        p = nl + 1;
    }
}

char *preprocess_file(const char *path)
{
    char *text;
    char base_dir[900] = "";
    if(path)
    {
        text = read_whole_file(path);
        if(!text)
        {
            fprintf(stderr, "cannot open '%s'\n", path);
            exit(1);
        }
        dirname_of(path, base_dir, sizeof(base_dir));
    }
    else
    {
        text = read_whole_stream(stdin);
    }

    char *output_buf;
    size_t output_size;
    FILE *output = open_memstream(&output_buf, &output_size);
    preprocess_text(text, base_dir, output, 0);
    fflush(output);
    fclose(output);
    free(text);
    return output_buf;
}
