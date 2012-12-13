#ifndef YMD_FLAGS_H
#define YMD_FLAGS_H

#include <stddef.h>

struct ymd_flag_entry {
	const char *flag; // Name of option
	const char *desc; // Description
	void *data;       // Pointer to data
	int (*parser)(const char *, void *); // Parser function
};

#define FLAG_END { NULL, NULL, NULL, NULL, },

#define MAX_FLAG_STRING_LEN 160
enum ymd_tribool {
	FLAG_NO,
	FLAG_YES,
	FLAG_AUTO,
};

// Parse command line options.
//
int ymd_flags_parse(const struct ymd_flag_entry *entries, const char *usage,
		int *argc, char ***argv, int remove);

// Free malloced memory
void ymd_flags_free(void *p);

// Flag parsers:
// Values:
// yes = 1
// no  = 0
int FlagBool(const char *z, void *data);

// Value in enum ymd_tribool
int FlagTriBool(const char *z, void *data);

// Copy value in `data'
int FlagString(const char *z, void *data);

// Malloc and copy value in `data'
int FlagStringMalloced(const char *z, void *data);

int FlagInt(const char *z, void *data);

#endif // YMD_FLAGS_H
