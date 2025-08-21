#ifndef CURL_RUNNER_H
#define CURL_RUNNER_H

#include <string>
#include <vector>

#include "curl_capture.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_UNICODE) && !defined(UNDER_CE)
int curl_wmain(int argc, wchar_t *argv[]);
#else
int curl_main(int argc, char *argv[]);
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

std::wstring to_wide(const std::string& utf8str) {
    if (utf8str.empty()) return std::wstring();
    std::wstring result;
    result.reserve(utf8str.length());
    for (size_t i = 0; i < utf8str.length(); ) {
        uint32_t codepoint = 0;
        uint8_t byte = utf8str[i];
        if (byte <= 0x7F) {
            // ASCII字符
            codepoint = byte;
            i += 1;
        } else if ((byte & 0xE0) == 0xC0) {
            // 2字节UTF-8
            if (i + 1 >= utf8str.length()) break;
            codepoint = ((byte & 0x1F) << 6) | (utf8str[i + 1] & 0x3F);
            i += 2;
        } else if ((byte & 0xF0) == 0xE0) {
            // 3字节UTF-8
            if (i + 2 >= utf8str.length()) break;
            codepoint = ((byte & 0x0F) << 12) | 
                       ((utf8str[i + 1] & 0x3F) << 6) | 
                       (utf8str[i + 2] & 0x3F);
            i += 3;
        } else if ((byte & 0xF8) == 0xF0) {
            // 4字节UTF-8
            if (i + 3 >= utf8str.length()) break;
            codepoint = ((byte & 0x07) << 18) | 
                       ((utf8str[i + 1] & 0x3F) << 12) | 
                       ((utf8str[i + 2] & 0x3F) << 6) | 
                       (utf8str[i + 3] & 0x3F);
            i += 4;
        } else {
            // 无效字节，跳过
            i += 1;
            continue;
        }
        // 转换为wchar_t
        if (sizeof(wchar_t) == 2) {
            // Windows (UTF-16)
            if (codepoint <= 0xFFFF) {
                result += static_cast<wchar_t>(codepoint);
            } else {
                // 代理对
                codepoint -= 0x10000;
                result += static_cast<wchar_t>(0xD800 + (codepoint >> 10));
                result += static_cast<wchar_t>(0xDC00 + (codepoint & 0x3FF));
            }
        } else {
            // Linux/macOS (UTF-32)
            result += static_cast<wchar_t>(codepoint);
        }
    }
    return result;
}

struct CurlResult curl_run(const std::vector<std::string> &args) {

    static struct CaptureBuffer outbuf;
    static char out_static[RUNNER_OUT_BUFFER_SIZE];
    capture_init(&outbuf, out_static, sizeof(out_static));
    set_stdout_capture_buffer(&outbuf);

    static struct CaptureBuffer errbuf;
    static char err_static[RUNNER_ERR_BUFFER_SIZE];
    capture_init(&errbuf, err_static, sizeof(err_static));
    set_stderr_capture_buffer(&errbuf);

#if defined(_UNICODE) && !defined(UNDER_CE)
    std::vector<wchar_t*> argv;
    argv.reserve(args.size() + 2);
    argv.push_back(const_cast<wchar_t*>(L"curl"));
    for (auto &s : args)
        argv.push_back(const_cast<wchar_t*>(to_wide(s).c_str()));
    argv.push_back(nullptr);

    int code = curl_wmain(static_cast<int>(argv.size() - 1), argv.data());
#else
    std::vector<char*> argv;
    argv.reserve(args.size() + 2);
    argv.push_back(const_cast<char*>("curl"));
    for (auto &s : args)
        argv.push_back(const_cast<char*>(s.c_str()));
    argv.push_back(nullptr);

    int code = curl_main(static_cast<int>(argv.size() - 1), argv.data());
#endif

    CurlResult result;
    result.exit_code = code;
    result.stdout_str = std::string(outbuf.data);
    result.stderr_str = std::string(errbuf.data);
    
    return result;
}

#endif /* CURL_RUNNER_H */