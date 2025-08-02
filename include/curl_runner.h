#ifndef CURL_RUNNER_H
#define CURL_RUNNER_H

#include <string>
#include <vector>

#include "curl_capture.h"

#ifdef __cplusplus
extern "C" {
#endif

int curl_main(int argc, char *argv[]);
#if defined(_WIN32)
int curl_wmain(int argc, wchar_t *argv[]);
#endif

struct CurlResult {
    int exit_code;
    std::string stdout_str;
    std::string stderr_str;
};

void set_stdout_capture_buffer(struct CaptureBuffer *buffer);
void set_stderr_capture_buffer(struct CaptureBuffer *buffer);

#ifdef __cplusplus
} /* extern "C" */
#endif

#define RUNNER_OUT_BUFFER_SIZE 1024 * 64
#define RUNNER_ERR_BUFFER_SIZE 1024 * 16

struct CurlResult curl_run(const std::vector<std::string> &args) {

    static struct CaptureBuffer outbuf;
    static char out_static[RUNNER_OUT_BUFFER_SIZE];
    capture_init(&outbuf, out_static, sizeof(out_static));
    set_stdout_capture_buffer(&outbuf);

    static struct CaptureBuffer errbuf;
    static char err_static[RUNNER_ERR_BUFFER_SIZE];
    capture_init(&errbuf, err_static, sizeof(err_static));
    set_stderr_capture_buffer(&errbuf);

    std::vector<char*> argv;
    argv.reserve(args.size() + 2);
    argv.push_back(const_cast<char*>("curl"));
    for (auto &s : args)
        argv.push_back(const_cast<char*>(s.c_str()));
    argv.push_back(nullptr);

    int code = curl_main(static_cast<int>(argv.size() - 1), argv.data());

    CurlResult result;
    result.exit_code = code;
    result.stdout_str = std::string(outbuf.data);
    result.stderr_str = std::string(errbuf.data);
    
    return result;
}

#endif /* CURL_RUNNER_H */