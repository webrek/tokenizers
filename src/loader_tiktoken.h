#ifndef TK_LOADER_TIKTOKEN_H
#define TK_LOADER_TIKTOKEN_H
#include "model.h"
tk_model *tk_load_tiktoken_file(const char *path, const char *pattern, char **err);
#endif
