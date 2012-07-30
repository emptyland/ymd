#include "libc.h"
#include "core.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#define L struct ymd_context *l

static const char *T_DIRENT = "posix.dirent";
struct ymd_dirent {
	DIR *core;
};

static int dirent_final(struct ymd_dirent *dir) {
	closedir(dir->core);
	return 0;
}

static int readdir_iter(L) {
	struct ymd_dirent *dir = mand_land(l->vm, ymd_bval(l, 0), T_DIRENT);
	struct dirent ent, *rv = NULL;
	memset(&ent, 0, sizeof(ent));
	if (readdir_r(dir->core, &ent, &rv) < 0 || rv != &ent)
		return 0;
	ymd_kstr(l, rv->d_name, -1);
	return 1;
}

static int desc2mode(const char *desc, mode_t *rv) {
	size_t k = strlen(desc), i = k;
	*rv = 0;
	while (i--) {
		if (desc[i] != '-')
			*rv |= 1 << (k - i - 1);
	}
	return 0;
}

/*static int mode2desc(mode_t mode, char *rv) {
	int i = 21;
	static const char *kflags = "xwr"; // 0,1,2
	while (i--) {
		if ((1 << i) & mode) {
			*rv++ = kflags[i % 3];
		}
	}
	*rv = 0;
	return 0;
}*/

static int libx_readdir(L) {
	const char *dname = kstr_of(l->vm, ymd_argv_get(l, 0))->land;
	struct ymd_dirent *dir;
	ymd_nafn(l, readdir_iter, "__readdir_iter__", 1);
	dir = ymd_mand(l, T_DIRENT, sizeof(*dir), (ymd_final_t)dirent_final);
	dir->core = opendir(dname);
	if (!dir->core) {
		ymd_pop(l, 2);
		return 0;
	}
	ymd_bind(l, 0); // bind struct ymd_dirent *
	return 1;
}

static int libx_fork(L) {
	ymd_int(l, fork());
	return 1;
}

static int libx_remove(L) {
	const char *path = kstr_of(l->vm, ymd_argv_get(l, 0))->land;
	ymd_int(l, remove(path));
	return 1;
}

static int libx_mkdir(L) {
	const char *path = kstr_of(l->vm, ymd_argv_get(l, 0))->land;
	mode_t mode = 0766;
	if (ymd_argv_chk(l, 1)->count >= 2) {
		if (desc2mode(kstr_of(l->vm, ymd_argv_get(l, 1))->land,
		              &mode) < 0) {
			ymd_int(l, -1);
			return 1;
		}
	}
	ymd_int(l, mkdir(path, mode));
	return 1;
}

static int libx_stat(L) {
	const char *name = kstr_of(l->vm, ymd_argv_get(l, 0))->land;
	struct stat x;
	memset(&x, 0, sizeof(x));
	if (lstat(name, &x) < 0)
		return 0;
	ymd_hmap(l, 16);
#define DEF(mem) ymd_int(l, x. mem); ymd_def(l, #mem)
	DEF(st_dev);
	DEF(st_ino);
	DEF(st_mode);
	DEF(st_nlink);
	DEF(st_uid);
	DEF(st_gid);
	DEF(st_rdev);
	DEF(st_size);
	DEF(st_atime);
	DEF(st_mtime);
	DEF(st_ctime);
	DEF(st_blksize);
	DEF(st_blocks);
#undef DEF
	return 1;
}

LIBC_BEGIN(OSPosix)
	LIBC_ENTRY(readdir)
	LIBC_ENTRY(fork)
	LIBC_ENTRY(remove)
	LIBC_ENTRY(mkdir)
	LIBC_ENTRY(stat)
LIBC_END

int ymd_load_os(struct ymd_mach *vm) {
	int rv;
	struct ymd_context *l = ioslate(vm);
	ymd_hmap(l, 0);
	ymd_kstr(l, "posix", -1);
	ymd_def(l, "platform");
	ymd_kstr(l, "0.1", -1);
	ymd_def(l, "version");
	rv = ymd_load_mem(l, "os", lbxOSPosix);
	ymd_putg(l, "os");
	return rv;
}

