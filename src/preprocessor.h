#pragma once

// Expands #include and #define directives (simple, object-like macros only,
// no conditionals) and returns a newly malloc'd, fully preprocessed source
// buffer that the lexer can be pointed at via lexer_set_source(). The caller
// must free() the result. `path` may be NULL to read from stdin.
char *preprocess_file(const char *path);
