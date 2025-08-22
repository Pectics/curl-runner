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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <locale.h>
#include <errno.h>

#ifdef _WIN32
#include <tchar.h>
#include <windows.h>
#else
#include <iconv.h>
#include <langinfo.h>
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
#define memory_tracking_init() tool_nop_stmt
#endif

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
    errorf("(%d) Windows-specific init failed", result);
    return (int)result;
  }
#endif

  if(main_checkfds()) {
    errorf("out of file descriptors");
    return CURLE_FAILED_INIT;
  }

#if defined(HAVE_SIGNAL) && defined(SIGPIPE)
  (void)signal(SIGPIPE, SIG_IGN);
#endif

  /* Initialize memory tracking */
  memory_tracking_init();

  /* Initialize the curl library - do not call any libcurl functions before
     this point */
  result = globalconf_init();
  if(!result) {
    /* Start our curl operation */
    result = operate(argc, argv);

    /* Perform the main cleanup */
    globalconf_free();
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

/**
 * 跨平台的 char*[] 转 wchar_t*[] 转换函数
 * @param argc 参数数量
 * @param argv 输入的 char* 数组
 * @param wargv_out 输出的 wchar_t* 数组指针
 * @return 0 成功, -1 失败
 * 注意：失败时函数内部会自动清理所有已分配的内存，
 *      调用者无需调用 free_wargv。
 *      只有成功时才需要调用 free_wargv 释放内存。
 */
int convert_argv_to_wargv(int argc, char *argv[], wchar_t ***wargv_out) {
    if (!argv || !wargv_out || argc <= 0) {
        return -1;
    }

    // 分配 wchar_t* 指针数组
    wchar_t **wargv = (wchar_t**)calloc(argc + 1, sizeof(wchar_t*));
    if (!wargv) {
        return -1;
    }

#ifdef _WIN32
    // Windows 平台使用 MultiByteToWideChar
    for (int i = 0; i < argc; i++) {
        if (!argv[i]) {
            wargv[i] = NULL;
            continue;
        }

        // 获取所需的宽字符缓冲区大小
        int wlen = MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, NULL, 0);
        if (wlen <= 0) {
            // 尝试使用当前代码页
            wlen = MultiByteToWideChar(CP_ACP, 0, argv[i], -1, NULL, 0);
            if (wlen <= 0) {
                goto cleanup_error;
            }
        }

        // 分配宽字符缓冲区
        wargv[i] = (wchar_t*)malloc(wlen * sizeof(wchar_t));
        if (!wargv[i]) {
            goto cleanup_error;
        }

        // 执行转换
        int result = MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, wargv[i], wlen);
        if (result == 0) {
            // UTF-8 失败，尝试 ACP
            result = MultiByteToWideChar(CP_ACP, 0, argv[i], -1, wargv[i], wlen);
            if (result == 0) {
                free(wargv[i]);
                wargv[i] = NULL;
                goto cleanup_error;
            }
        }
    }

#else
    // Unix/Linux 平台使用 iconv 或 mbstowcs
    
    // 设置本地化环境
    char *old_locale = setlocale(LC_CTYPE, NULL);
    char *saved_locale = NULL;
    if (old_locale) {
        saved_locale = strdup(old_locale);
    }
    
    // 尝试设置 UTF-8 环境
    if (!setlocale(LC_CTYPE, "C.UTF-8") && 
        !setlocale(LC_CTYPE, "en_US.UTF-8") &&
        !setlocale(LC_CTYPE, "")) {
        // 设置失败，使用默认环境
    }

    for (int i = 0; i < argc; i++) {
        if (!argv[i]) {
            wargv[i] = NULL;
            continue;
        }

        size_t input_len = strlen(argv[i]);
        size_t max_wlen = input_len + 1; // 最大可能的宽字符长度

        // 分配宽字符缓冲区
        wargv[i] = (wchar_t*)malloc(max_wlen * sizeof(wchar_t));
        if (!wargv[i]) {
            goto cleanup_error_unix;
        }

        // 使用 mbstowcs 进行转换
        size_t converted = mbstowcs(wargv[i], argv[i], max_wlen - 1);
        
        if (converted == (size_t)-1) {
            // mbstowcs 失败，尝试使用 iconv
            free(wargv[i]);
            wargv[i] = NULL;
            
            // 使用 iconv 进行转换
            iconv_t cd = iconv_open("WCHAR_T", "UTF-8");
            if (cd == (iconv_t)-1) {
                cd = iconv_open("WCHAR_T", "");
                if (cd == (iconv_t)-1) {
                    goto cleanup_error_unix;
                }
            }

            size_t inbytesleft = input_len;
            size_t outbytesleft = (max_wlen - 1) * sizeof(wchar_t);
            char *inbuf = argv[i];
            
            wargv[i] = (wchar_t*)malloc(max_wlen * sizeof(wchar_t));
            if (!wargv[i]) {
                iconv_close(cd);
                goto cleanup_error_unix;
            }
            
            char *outbuf = (char*)wargv[i];
            
            if (iconv(cd, &inbuf, &inbytesleft, &outbuf, &outbytesleft) == (size_t)-1) {
                iconv_close(cd);
                free(wargv[i]);
                wargv[i] = NULL;
                goto cleanup_error_unix;
            }
            
            // 添加结束符
            size_t wchars_written = ((char*)wargv[i] + (max_wlen - 1) * sizeof(wchar_t) - outbuf) / sizeof(wchar_t);
            wargv[i][wchars_written] = L'\0';
            
            iconv_close(cd);
        } else {
            // mbstowcs 成功
            wargv[i][converted] = L'\0';
        }
    }

    // 恢复原始 locale
    if (saved_locale) {
        setlocale(LC_CTYPE, saved_locale);
        free(saved_locale);
    }
#endif

    wargv[argc] = NULL; // NULL 终止数组
    *wargv_out = wargv;
    return 0;

#ifndef _WIN32
cleanup_error_unix:
    if (saved_locale) {
        setlocale(LC_CTYPE, saved_locale);
        free(saved_locale);
    }
#endif

cleanup_error:
    if (wargv) {
        for (int i = 0; i < argc; i++) {
            if (wargv[i]) {
                free(wargv[i]);
            }
        }
        free(wargv);
    }
    return -1;
}

/**
 * 释放由 convert_argv_to_wargv 分配的内存
 * @param argc 参数数量
 * @param wargv 要释放的 wchar_t* 数组
 */
void free_wargv(int argc, wchar_t **wargv) {
  if (!wargv) return;
  for (int i = 0; i < argc; i++)
    if (wargv[i])
      free(wargv[i]);
  free(wargv);
}

/*
 * curl_runner entry
 */
int curl_main(int argc, char *argv[])
{
#if defined(_UNICODE) && !defined(UNDER_CE)
  wchar_t **wargv = NULL;
  if (!convert_argv_to_wargv(argc, argv, &wargv)) {
    int code = wmain(argc, wargv);
    free_wargv(argc, wargv);
    return code;
  }
  return 114514;
#else
  return main(argc, argv);
#endif
}
