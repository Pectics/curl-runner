/*
 * curl_runner.h
 *
 * This header exposes functions that allow embedding the curl command line
 * functionality into another application.  Instead of spawning an external
 * process or parsing arguments yourself, you can call ``curl_main`` (or on
 * Windows, ``curl_wmain``) directly with the same argv/argc parameters you
 * would normally pass to the curl executable.  These functions wrap the
 * original curl main routines in a way that they can be linked from a
 * library build.  See tool_main.c for the implementation details.
 */

#ifndef CURL_RUNNER_H
#define CURL_RUNNER_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Execute curl with the given argument vector.  This function behaves in
 * exactly the same way as invoking the ``curl`` command line tool: it
 * performs global initialization, parses command line arguments, runs the
 * transfer(s) and performs cleanup before returning.  The return value
 * matches the exit code of curl.
 *
 * @param argc  the number of entries in ``argv``
 * @param argv  an array of C strings holding the command line arguments
 * @return      the same integer that the curl executable would return
 */
int curl_main(int argc, char *argv[]);

#if defined(_WIN32)
/*
 * Wide‑character variant of curl_main for Windows.  When building a Unicode
 * Windows application, command line arguments are provided as UTF‑16
 * ``wchar_t`` strings.  This function converts the wide strings to UTF‑8
 * internally and forwards them to ``curl_main``.  The semantics of the
 * return value are identical to those of ``curl_main``.
 *
 * @param argc  the number of entries in ``argv``
 * @param argv  an array of wide strings holding the command line arguments
 * @return      the same integer that the curl executable would return
 */
int curl_wmain(int argc, wchar_t *argv[]);
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CURL_RUNNER_H */