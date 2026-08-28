/*
** $Id: ldolphin.h $
** UTF-8 path handling for the standard library
** See Copyright Notice in lua.h
**
** This file is a Dolphin-local addition; it is not part of upstream Lua 5.3.1.
**
** On Windows the narrow CRT functions resolve their path argument using the
** process code page, but every path Dolphin hands to Lua is UTF-8. A script
** stored under a folder whose name contains non-ASCII characters could not be
** reached at all: not by luaL_loadfile (so dofile, loadfile and require all
** failed), and not by io.open, io.lines, os.remove or os.rename. Route those
** through the wide-character CRT so UTF-8 paths work regardless of locale.
**
** Everywhere else these are the plain C functions: POSIX takes UTF-8 byte
** paths natively, so there is nothing to translate.
*/

#ifndef ldolphin_h
#define ldolphin_h

#include <stdio.h>

#include "luaconf.h"

#if defined(_WIN32)

LUAI_FUNC FILE *luaDolphin_fopen (const char *filename, const char *mode);
LUAI_FUNC FILE *luaDolphin_freopen (const char *filename, const char *mode,
                                    FILE *stream);
LUAI_FUNC int luaDolphin_remove (const char *filename);
LUAI_FUNC int luaDolphin_rename (const char *from, const char *to);

#define l_fopen(fn,m)		luaDolphin_fopen((fn), (m))
#define l_freopen(fn,m,s)	luaDolphin_freopen((fn), (m), (s))
#define l_remove(fn)		luaDolphin_remove((fn))
#define l_rename(f,t)		luaDolphin_rename((f), (t))

#else

#define l_fopen(fn,m)		fopen((fn), (m))
#define l_freopen(fn,m,s)	freopen((fn), (m), (s))
#define l_remove(fn)		remove((fn))
#define l_rename(f,t)		rename((f), (t))

#endif

#endif
