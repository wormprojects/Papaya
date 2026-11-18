/**
 * Papaya desktop: "sdmc:/" paths -> the real file system. See pathmap.h.
 */

#include "pathmap.h"

/* pathmap.h is force-included (so its macros are already on): this file is
 * the one place that needs the real calls. */
#undef fopen
#undef stat
#undef mkdir
#undef opendir
#undef readdir
#undef chdir
#undef rename
#undef remove
#undef unlink
#undef access
#undef open

#include <errno.h>
#include <pwd.h>
#include <stdarg.h>
#include <limits.h>

const char* papayaHomeDir(void)
{
	static char home[PATH_MAX];

	if(home[0] == '\0')
	{
		const char*		env = getenv("HOME");
		struct passwd*	pw;

		if(env != NULL && env[0] == '/')
			snprintf(home, sizeof(home), "%s", env);
		else if((pw = getpwuid(getuid())) != NULL)
			snprintf(home, sizeof(home), "%s", pw->pw_dir);
		else
			snprintf(home, sizeof(home), "/");
	}
	return home;
}

const char* papayaDataDir(void)
{
	static char dir[PATH_MAX];

	if(dir[0] == '\0')
	{
		const char* env = getenv("PAPAYA_DATA_DIR");

		if(env != NULL && env[0] == '/')
			snprintf(dir, sizeof(dir), "%s", env);
#ifdef __APPLE__
		else
			snprintf(dir, sizeof(dir), "%s/Library/Application Support/Papaya",
			         papayaHomeDir());
#else
		else if(getenv("XDG_CONFIG_HOME") != NULL &&
		        getenv("XDG_CONFIG_HOME")[0] == '/')
			snprintf(dir, sizeof(dir), "%s/papaya", getenv("XDG_CONFIG_HOME"));
		else
			snprintf(dir, sizeof(dir), "%s/.config/papaya", papayaHomeDir());
#endif
	}
	return dir;
}

/* Is `rest` equal to `name` or does it continue with '/'? Returns what
 * follows (possibly ""), else NULL. */
static const char* under(const char* rest, const char* name)
{
	size_t n = strlen(name);

	if(strncmp(rest, name, n) != 0)
		return NULL;
	if(rest[n] == '\0')
		return rest + n;
	return rest[n] == '/' ? rest + n : NULL;
}

/* Where path really is; `buf` is used if it has to be rewritten. */
static const char* map(const char* path, char* buf, size_t len)
{
	const char* p;
	const char* t;
	const char* s;

	if(path == NULL || strncmp(path, "sdmc:", 5) != 0)
		return path;

	p = path + 5;
	while(*p == '/')
		p++;			/* "sdmc:/x", "sdmc://x" (dir + name) */

	if(*p == '\0')
	{
		snprintf(buf, len, "%s", papayaHomeDir());
		return buf;
	}

	if((t = under(p, "3ds")) != NULL)
	{
		while(*t == '/')
			t++;
		if(*t == '\0')
			snprintf(buf, len, "%s", papayaDataDir());
		else if((s = under(t, "papaya")) != NULL)
			snprintf(buf, len, "%s%s", papayaDataDir(), s);
		else if((s = under(t, "ctrmus")) != NULL)
			snprintf(buf, len, "%s/.old-ctrmus%s", papayaDataDir(), s);
		else
			snprintf(buf, len, "/%s", p);
		return buf;
	}

	snprintf(buf, len, "/%s", p);
	return buf;
}

#define MAPPED(var, path) \
	char var##Buf[PATH_MAX]; \
	const char* var = map(path, var##Buf, sizeof(var##Buf))

FILE* papaya_fopen(const char* path, const char* mode)
{
	MAPPED(p, path);
	return fopen(p, mode);
}

int papaya_stat(const char* path, struct stat* st)
{
	MAPPED(p, path);
	return stat(p, st);
}

int papaya_mkdir(const char* path, mode_t mode)
{
	MAPPED(p, path);
	return mkdir(p, mode);
}

DIR* papaya_opendir(const char* path)
{
	MAPPED(p, path);
	return opendir(p);
}

struct dirent* papaya_readdir(DIR* dir)
{
	static __thread struct dirent	copy;
	struct dirent*					ep = readdir(dir);
	struct stat						st;

	if(ep == NULL)
		return NULL;
	if(ep->d_type != DT_LNK && ep->d_type != DT_UNKNOWN)
		return ep;

	/* Follow the link (or look the entry up): it's what it points to. */
	copy = *ep;
	if(fstatat(dirfd(dir), ep->d_name, &st, 0) == 0)
		copy.d_type = S_ISDIR(st.st_mode) ? DT_DIR :
		              S_ISREG(st.st_mode) ? DT_REG : DT_UNKNOWN;
	else
		copy.d_type = DT_UNKNOWN;	/* dangling link */
	return &copy;
}

int papaya_chdir(const char* path)
{
	MAPPED(p, path);
	int ret = chdir(p);

	/* The browser starts in "MUSIC" (the 3DS card's folder); here that is
	 * ~/Music, which on a case-sensitive file system needs this. */
	if(ret != 0 && strcmp(path, "MUSIC") == 0)
		ret = chdir("Music");
	return ret;
}

int papaya_rename(const char* from, const char* to)
{
	MAPPED(a, from);
	MAPPED(b, to);
	return rename(a, b);
}

int papaya_remove(const char* path)
{
	MAPPED(p, path);
	return remove(p);
}

int papaya_unlink(const char* path)
{
	MAPPED(p, path);
	return unlink(p);
}

int papaya_access(const char* path, int mode)
{
	MAPPED(p, path);
	return access(p, mode);
}

int papaya_open(const char* path, int flags, ...)
{
	MAPPED(p, path);
	mode_t mode = 0;

	if(flags & O_CREAT)
	{
		va_list ap;

		va_start(ap, flags);
		mode = (mode_t)va_arg(ap, int);
		va_end(ap);
	}
	return open(p, flags, mode);
}
