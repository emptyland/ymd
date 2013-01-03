#include "flags.h"
#include "print.h"
#include <string.h>
#include <stdlib.h>

static void flag_remove(int *argc, char ***argv, int i) {
	while (i++ < *argc)
		(*argv)[i - 1] = (*argv)[i];
	--(*argc);
}

static const char *flag_input_info(const struct ymd_flag_entry *p) {
	if (p->parser == &FlagBool)
		return "[yes|no]";
	if (p->parser == &FlagTriBool)
		return "[yes|no|auto]";
	if (p->parser == &FlagString)
		return "[string]";
	if (p->parser == &FlagInt)
		return "[integer]";
	return "";
}

static int flag_usage(const struct ymd_flag_entry *p, const char *usage,
		const char *arg0) {
	const char *x = strrchr(arg0, '/');
	if (x)
		arg0 = x + 1;
	if (usage)
		ymd_printf("Usage:\n%s\n", usage);
	ymd_printf("${[purple]%s}$ Options:\n", arg0);
	while (p->flag) {
		ymd_printf("  --${[!green]%s}$=${[!yellow]%s}$\n      %s\n",
				p->flag,
				flag_input_info(p),
				p->desc);
		++p;
	}
	exit(0);
	return 0;
}

int ymd_flags_parse(const struct ymd_flag_entry *entries, const char *usage,
		int *argc, char ***argv, int remove) {
	int i;
	for (i = 1; i < *argc; ++i) {
		const struct ymd_flag_entry *p = entries;
		const char *argz = (*argv)[i];

		if (strstr(argz, "--") != argz)
			continue;

		argz += 2; // Skip "--" prefix
		while (p->flag) {
			if (strcmp(argz, "help") == 0)
				return flag_usage(entries, usage, (*argv)[0]);
			// Check the arg prefix
			if (strstr(argz, p->flag) == argz) {
				(*p->parser)(argz, p->data);
				if (remove) {
					flag_remove(argc, argv, i);
					--i;
				}
				break;
			}
			++p;
		}
	}
	return 0;
}

void ymd_flags_free(void *p) {
	free(p);
}

int FlagBool(const char *z, void *data) {
	const char *p = strchr(z, '=');
	if (!p) {
		*(int *)data = 1;
		return -1;
	}
	++p;
	if (strcmp(p, "yes") == 0)
		*(int *)data = 1;
	else
		*(int *)data = 0;
	return 0;
}

int FlagTriBool(const char *z, void *data) {
	const char *p = strchr(z, '=');
	if (!p) {
		*(int *)data = 1;
		return -1;
	}
	++p;
	if (strcmp(p, "yes") == 0)
		*(int *)data = FLAG_YES;
	else if (strcmp(p, "auto") == 0)
		*(int *)data = FLAG_AUTO;
	else
		*(int *)data = FLAG_NO;
	return 0;
}

int FlagString(const char *z, void *data) {
	const char *p = strchr(z, '=');
	if (!p) {
		strcpy(data, "");
		return -1;
	}
	strncpy(data, p + 1, MAX_FLAG_STRING_LEN);
	return 0;
}

int FlagStringMalloced(const char *z, void *data) {
	const char *p = strchr(z, '=');
	if (!p) {
		*(void **)data = NULL;
		return -1;
	}
	*(void **)data = strdup(p + 1);
	return 0;
}

int FlagInt(const char *z, void *data) {
	const char *p = strchr(z, '=');
	if (!p)
		return 0;
	*(int *)data = atoi(p + 1);
	return 0;
}
