/*
** $Id: ldolphin.c $
** UTF-8 path handling for the standard library
** See Copyright Notice in lua.h
**
** Dolphin-local addition; see ldolphin.h for why this exists.
*/

#define ldolphin_c
#define LUA_LIB

#include "lprefix.h"

#include <stdio.h>

#include "lua.h"

#include "ldolphin.h"

#if defined(_WIN32)

#include <errno.h>
#include <stdlib.h>
#include <wchar.h>
#include <windows.h>


/*
** Converts a UTF-8 string into a freshly allocated wide string. Returns NULL
** if the input is not valid UTF-8 or the allocation fails; the caller frees.
*/
static wchar_t *widen (const char *utf8) {
  wchar_t *wide;
  int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
  if (len <= 0) return NULL;
  wide = (wchar_t *)malloc(sizeof(wchar_t) * (size_t)len);
  if (wide == NULL) return NULL;
  if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, len) != len) {
    free(wide);
    return NULL;
  }
  return wide;
}


FILE *luaDolphin_fopen (const char *filename, const char *mode) {
  wchar_t *wfilename = widen(filename);
  wchar_t *wmode = widen(mode);
  FILE *f = NULL;
  if (wfilename != NULL && wmode != NULL)
    f = _wfopen(wfilename, wmode);
  else
    errno = EINVAL;  /* so the caller's strerror(errno) still says something */
  free(wfilename);
  free(wmode);
  return f;
}


FILE *luaDolphin_freopen (const char *filename, const char *mode, FILE *stream) {
  wchar_t *wfilename = widen(filename);
  wchar_t *wmode = widen(mode);
  FILE *f = NULL;
  if (wfilename != NULL && wmode != NULL)
    f = _wfreopen(wfilename, wmode, stream);
  else
    errno = EINVAL;
  free(wfilename);
  free(wmode);
  return f;
}


int luaDolphin_remove (const char *filename) {
  wchar_t *wfilename = widen(filename);
  int result;
  if (wfilename == NULL) {
    errno = EINVAL;
    return -1;
  }
  result = _wremove(wfilename);
  free(wfilename);
  return result;
}


int luaDolphin_rename (const char *from, const char *to) {
  wchar_t *wfrom = widen(from);
  wchar_t *wto = widen(to);
  int result = -1;
  if (wfrom != NULL && wto != NULL)
    result = _wrename(wfrom, wto);
  else
    errno = EINVAL;
  free(wfrom);
  free(wto);
  return result;
}

#endif
