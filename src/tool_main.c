/***************************************************************************
 *                                  _   _ ____  _
 *  Project                     ___| | | |  _ \| |
 *                             / __| | | | |_) | |
 *                            | (__| |_| |  _ <| |___
 *                             \___|\___/|_| \_\_____|
 *
 * Copyright (C) Daniel Stenberg, <daniel@haxx.se>, et al.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution. The terms
 * are also available at https://curl.se/docs/copyright.html.
 *
 * You may opt to use, copy, modify, merge, publish, distribute and/or sell
 * copies of the Software, and permit persons to whom the Software is
 * furnished to do so, under the terms of the COPYING file.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 * SPDX-License-Identifier: curl
 *
 ***************************************************************************/

/*
 * NOTE:
 *
 * This file has been adapted from the original curl source to expose
 * embeddable entry points.  In addition to the existing ``main`` and
 * ``wmain`` functions (which remain intact for building the curl
 * executable), it now provides ``curl_main`` and ``curl_wmain`` functions.
 * These functions wrap the command line tool behaviour so that it can be
 * invoked directly from within another program.  See ``include/curl_runner.h``
 * for the public API description.
 */

#include "tool_setup.h"

#ifdef _WIN32
#include <tchar.h>
#include <windows.h>
#endif

#ifndef UNDER_CE
#include <signal.h>
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#include "tool_cfgable.h"
#include "tool_doswin.h"
#include "tool_msgs.h"
#include "tool_operate.h"
#include "tool_vms.h"
#include "tool_main.h"
#include "tool_libinfo.h"
#include "tool_stderr.h"

/*
 * This is low-level hard-hacking memory leak tracking and similar. Using
 * the library level code from this client-side is ugly, but we do this
 * anyway for convenience.
 */
#include "memdebug.h" /* keep this as LAST include */

#ifdef __VMS
/*
 * vms_show is a global variable, used in main() as parameter for
 * function vms_special_exit() to allow proper curl tool exiting.
 * Its value may be set in other tool_*.c source files thanks to
 * forward declaration present in tool_vms.h
 */
int vms_show = 0;
#endif

#ifdef __AMIGA__
#ifdef __GNUC__
#define CURL_USED __attribute__((used))
#else
#define CURL_USED
#endif
static const char CURL_USED min_stack[] = "$STACK:16384";
#endif

#ifdef __MINGW32__
/*
 * There seems to be no way to escape "*" in command-line arguments with MinGW
 * when command-line argument globbing is enabled under the MSYS shell, so turn
 * it off.
 */
extern int _CRT_glob;
int _CRT_glob = 0;
#endif /* __MINGW32__ */

/* if we build a static library for unit tests, there is no main() function */
#ifndef UNITTESTS

#if defined(HAVE_PIPE) && defined(HAVE_FCNTL)
/*
 * Ensure that file descriptors 0, 1 and 2 (stdin, stdout, stderr) are
 * open before starting to run. Otherwise, the first three network
 * sockets opened by curl could be used for input sources, downloaded data
 * or error logs as they will effectively be stdin, stdout and/or stderr.
 *
 * fcntl's F_GETFD instruction returns -1 if the file descriptor is closed,
 * otherwise it returns "the file descriptor flags".
 */
static int main_checkfds(void)
{
  int fd[2];
  while((fcntl(STDIN_FILENO, F_GETFD) == -1) ||
        (fcntl(STDOUT_FILENO, F_GETFD) == -1) ||
        (fcntl(STDERR_FILENO, F_GETFD) == -1))
    if(pipe(fd))
      return 1;
  return 0;
}
#else
#define main_checkfds() 0
#endif

#ifdef CURLDEBUG
static void memory_tracking_init(void)
{
  char *env;
  /* if CURL_MEMDEBUG is set, this starts memory tracking message logging */
  env = curl_getenv("CURL_MEMDEBUG");
  if(env) {
    /* use the value as filename */
    char fname[512];
    if(strlen(env) >= sizeof(fname))
      env[sizeof(fname)-1] = '\0';
    strcpy(fname, env);
    curl_free(env);
    curl_dbg_memdebug(fname);
    /* this weird stuff here is to make curl_free() get called before
       curl_dbg_memdebug() as otherwise memory tracking will log a free()
       without an alloc! */
  }
  /* if CURL_MEMLIMIT is set, this enables fail-on-alloc-number-N feature */
  env = curl_getenv("CURL_MEMLIMIT");
  if(env) {
    curl_off_t num;
    const char *p = env;
    if(!curlx_str_number(&p, &num, LONG_MAX))
      curl_dbg_memlimit((long)num);
    curl_free(env);
  }
}
#else
#  define memory_tracking_init() tool_nop_stmt
#endif

/*
 * This is the main global constructor for the app. Call this before
 * _any_ libcurl usage. If this fails, *NO* libcurl functions may be
 * used, or havoc may be the result.
 */
static CURLcode main_init(struct GlobalConfig *global)
{
  CURLcode result = CURLE_OK;

#ifdef __DJGPP__
  /* stop stat() wasting time */
  _djstat_flags |= _STAT_INODE | _STAT_EXEC_MAGIC | _STAT_DIRSIZE;
#endif

  /* Initialise the global config */
  global->showerror = FALSE;          /* show errors when silent */
  global->styled_output = TRUE;       /* enable detection */
  global->parallel_max = PARALLEL_DEFAULT;

  /* Allocate the initial operate config */
  global->first = global->last = config_alloc(global);
  if(global->first) {
    /* Perform the libcurl initialization */
    result = curl_global_init(CURL_GLOBAL_DEFAULT);
    if(!result) {
      /* Get information about libcurl */
      result = get_libcurl_info();

      if(result) {
        errorf(global, "error retrieving curl library information");
        free(global->first);
      }
    }
    else {
      errorf(global, "error initializing curl library");
      free(global->first);
    }
  }
  else {
    errorf(global, "error initializing curl");
    result = CURLE_FAILED_INIT;
  }

  return result;
}

static void free_globalconfig(struct GlobalConfig *global)
{
  tool_safefree(global->trace_dump);

  if(global->trace_fopened && global->trace_stream)
    fclose(global->trace_stream);
  global->trace_stream = NULL;

  tool_safefree(global->libcurl);
}

/*
 * This is the main global destructor for the app. Call this after _all_
 * libcurl usage is done.
 */
static void main_free(struct GlobalConfig *global)
{
  /* Cleanup the easy handle */
  /* Main cleanup */
  curl_global_cleanup();
  free_globalconfig(global);

  /* Free the OperationConfig structures */
  config_free(global->last);
  global->first = NULL;
  global->last = NULL;
}

/*
** curl tool main function.
*/
#if defined(_UNICODE) && !defined(UNDER_CE)
#if defined(__GNUC__) || defined(__clang__)
/* GCC does not know about wmain() */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#endif
int wmain(int argc, wchar_t *argv[])
#else
int main(int argc, char *argv[])
#endif
{
  CURLcode result = CURLE_OK;
  struct GlobalConfig global;
  memset(&global, 0, sizeof(global));

  tool_init_stderr();

#if defined(_WIN32) && !defined(UNDER_CE)
  /* Undocumented diagnostic option to list the full paths of all loaded
     modules. This is purposefully pre‑init. */
  if(argc == 2 && !_tcscmp(argv[1], _T("--dump-module-paths"))) {
    struct curl_slist *item, *head = GetLoadedModulePaths();
    for(item = head; item; item = item->next)
      printf("%s\n", item->data);
    curl_slist_free_all(head);
    return head ? 0 : 1;
  }
#endif
#ifdef _WIN32
  /* win32_init must be called before other init routines. */
  result = win32_init();
  if(result) {
    errorf(&global, "(%d) Windows-specific init failed", result);
    return (int)result;
  }
#endif

  if(main_checkfds()) {
    errorf(&global, "out of file descriptors");
    return CURLE_FAILED_INIT;
  }

#if defined(HAVE_SIGNAL) && defined(SIGPIPE)
  (void)signal(SIGPIPE, SIG_IGN);
#endif

  /* Initialize memory tracking */
  memory_tracking_init();

  /* Initialize the curl library - do not call any libcurl functions before
     this point */
  result = main_init(&global);
  if(!result) {
    /* Start our curl operation */
    result = operate(&global, argc, argv);

    /* Perform the main cleanup */
    main_free(&global);
  }

#ifdef _WIN32
  /* Flush buffers of all streams opened in write or update mode */
  fflush(NULL);
#endif

#ifdef __VMS
  vms_special_exit(result, vms_show);
#else
  return (int)result;
#endif
}

#if defined(_UNICODE) && !defined(UNDER_CE)
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#endif

#endif /* ndef UNITTESTS */

/*
 * ---------------------------------------------------------------------------
 * Embeddable entry points
 *
 * The following functions allow the curl command line tool logic to be
 * executed from within another program.  They mirror the behaviour of the
 * standard ``main``/``wmain`` functions but are always available, even when
 * building a static library without the ``main`` symbol (UNITTESTS).  The
 * functions reuse the same helper routines defined above to perform setup,
 * execution and cleanup.
 */

#include <string.h>
#include <stdlib.h>

/*
 * Run the curl tool with a traditional C argument vector.
 */
int curl_main(int argc, char *argv[])
{
  CURLcode result = CURLE_OK;
  struct GlobalConfig global;
  memset(&global, 0, sizeof(global));

  tool_init_stderr();

#if defined(_WIN32) && !defined(UNDER_CE)
  /* Provide the same diagnostic behaviour as the standalone executable.  We
     cannot rely on the Windows generic string macros here because this
     function always receives narrow strings.  */
  if(argc == 2 && argv && argv[1] && strcmp(argv[1], "--dump-module-paths") == 0) {
    struct curl_slist *item, *head = GetLoadedModulePaths();
    for(item = head; item; item = item->next)
      printf("%s\n", item->data);
    curl_slist_free_all(head);
    return head ? 0 : 1;
  }
#endif

#ifdef _WIN32
  /* Call the Windows-specific initialisation when building on Windows. */
  result = win32_init();
  if(result) {
    errorf(&global, "(%d) Windows-specific init failed", result);
    return (int)result;
  }
#endif

  /* Ensure the standard file descriptors are open */
  if(main_checkfds()) {
    errorf(&global, "out of file descriptors");
    return CURLE_FAILED_INIT;
  }

  /* Ignore SIGPIPE on platforms that support it. */
#if defined(HAVE_SIGNAL) && defined(SIGPIPE)
  (void)signal(SIGPIPE, SIG_IGN);
#endif

  /* Initialize any optional memory tracking. */
  memory_tracking_init();

  /* Perform global curl initialisation. */
  result = main_init(&global);
  if(!result) {
    /* Execute the requested transfer(s). */
    result = operate(&global, argc, argv);
    /* Clean up after ourselves. */
    main_free(&global);
  }

#ifdef _WIN32
  /* Flush buffers of all streams opened in write or update mode */
  fflush(NULL);
#endif

#ifdef __VMS
  vms_special_exit(result, vms_show);
  return 0;
#else
  return (int)result;
#endif
}

#if defined(_WIN32)
/*
 * Wide character entry point.  Convert the UTF‑16 Windows arguments into
 * UTF‑8 byte strings and forward them to curl_main().
 */
int curl_wmain(int argc, wchar_t *argv[])
{
  int i;
  int ret = 0;
  char **mbargv = NULL;

  /* Allocate an array for the narrow argument strings.  One extra slot is
     reserved for a NULL terminator in case some consumers expect it. */
  mbargv = (char**)malloc(sizeof(char*) * (argc + 1));
  if(!mbargv) {
    /* In extreme cases allocation can fail; mimic a generic curl error
       return. */
    return (int)CURLE_OUT_OF_MEMORY;
  }

  for(i = 0; i < argc; i++) {
    /* Determine the size in bytes of the UTF‑8 representation.  Use
       CP_UTF8 so that the result is portable and independent of the local
       code page. */
    int bytes = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, NULL, 0, NULL, NULL);
    if(bytes <= 0) {
      ret = (int)CURLE_FAILED_INIT;
      goto cleanup;
    }
    mbargv[i] = (char*)malloc((size_t)bytes);
    if(!mbargv[i]) {
      ret = (int)CURLE_OUT_OF_MEMORY;
      goto cleanup;
    }
    if(!WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, mbargv[i], bytes, NULL, NULL)) {
      ret = (int)CURLE_FAILED_INIT;
      goto cleanup;
    }
  }
  /* NULL‑terminate the array for safety */
  mbargv[argc] = NULL;

  ret = curl_main(argc, mbargv);

cleanup:
  /* Free any memory we allocated */
  if(mbargv) {
    for(i = 0; i < argc; i++) {
      free(mbargv[i]);
    }
    free(mbargv);
  }
  return ret;
}
#endif /* _WIN32 */