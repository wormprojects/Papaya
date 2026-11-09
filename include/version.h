/**
 * ctrmus - 3DS Music Player
 * Single source of truth for the version number.
 *
 * The Makefile reads the three numeric macros below (for the CIA/banner
 * version), and the app shows CTRMUS_VERSION on the splash screen and in
 * the menu. Bump these together with a new CHANGELOG.md entry.
 */

#ifndef ctrmus_version_h
#define ctrmus_version_h

#define CTRMUS_VERSION_MAJOR 1
#define CTRMUS_VERSION_MINOR 0
#define CTRMUS_VERSION_MICRO 1

#define CTRMUS_STR_(x) #x
#define CTRMUS_STR(x) CTRMUS_STR_(x)

/* Pre-release tag ("-beta.N"), empty for releases. */
#define CTRMUS_VERSION_TAG ""

#define CTRMUS_VERSION CTRMUS_STR(CTRMUS_VERSION_MAJOR) "." \
                       CTRMUS_STR(CTRMUS_VERSION_MINOR) "." \
                       CTRMUS_STR(CTRMUS_VERSION_MICRO) CTRMUS_VERSION_TAG

#endif
