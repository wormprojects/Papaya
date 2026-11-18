/**
 * Papaya desktop: real file system access for code written for the 3DS.
 *
 * Force-included into every translation unit (-include), so the 3DS
 * sources don't change. They name files "sdmc:/..." and call plain libc;
 * the macros below send those calls through papaya_map() (fsmap.c):
 *
 *   sdmc:/                 where browsing starts: ~ (browserOpen() then
 *                          enters "MUSIC", which becomes ~/Music)
 *   sdmc:/3ds/papaya/...   the app's own files (settings, themes, tag
 *                          cache): ~/Library/Application Support/Papaya
 *                          (XDG: ~/.config/papaya)
 *   sdmc:/3ds/ctrmus/...   the old ctrmus folder: nowhere (the migration
 *                          from it is a 3DS thing)
 *   sdmc:/<other>          /<other>
 *   anything else          untouched
 *
 * readdir() also resolves symlinks (a 3DS FAT card has none), so linked
 * folders and files show up as what they point to.
 */

#ifndef papaya_pathmap_h
#define papaya_pathmap_h

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The user's home directory (never NULL, no trailing slash unless "/"). */
const char*		papayaHomeDir(void);
/** The app's data directory (created on demand by the 3DS code). */
const char*		papayaDataDir(void);

FILE*			papaya_fopen(const char* path, const char* mode);
int				papaya_stat(const char* path, struct stat* st);
int				papaya_mkdir(const char* path, mode_t mode);
DIR*			papaya_opendir(const char* path);
struct dirent*	papaya_readdir(DIR* dir);
int				papaya_chdir(const char* path);
int				papaya_rename(const char* from, const char* to);
int				papaya_remove(const char* path);
int				papaya_unlink(const char* path);
int				papaya_access(const char* path, int mode);
int				papaya_open(const char* path, int flags, ...);

#ifdef __cplusplus
}
#endif

#ifndef PAPAYA_NO_PATHMAP
#define fopen(p, m)		papaya_fopen(p, m)
#define stat(p, s)		papaya_stat(p, s)
#define mkdir(p, m)		papaya_mkdir(p, m)
#define opendir(p)		papaya_opendir(p)
#define readdir(d)		papaya_readdir(d)
#define chdir(p)		papaya_chdir(p)
#define rename(a, b)	papaya_rename(a, b)
#define remove(p)		papaya_remove(p)
#define unlink(p)		papaya_unlink(p)
#define access(p, m)	papaya_access(p, m)
#define open(p, ...)	papaya_open(p, __VA_ARGS__)
#endif

#endif
