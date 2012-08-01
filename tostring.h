#ifndef YMD_TOSTRING_H
#define YMD_TOSTRING_H

#include "zstream.h"
#include <string.h>
#include <assert.h>

struct variable;

// format a variable to formated string:
const char *tostring(struct zostream *ctx, const struct variable *var);

#endif // YMD_TOSTRING_H

