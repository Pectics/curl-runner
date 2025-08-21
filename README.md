<!--
Forked and modified by Pectics
Original work Copyright (C) Daniel Stenberg, <daniel@haxx.se>, et al.
SPDX-License-Identifier: curl
-->

# curl-runner

# <img src="https://curl.se/logo/curl-logo.svg" alt="curl logo" width="320">

**curl-runner** 是一个基于 [curl](https://curl.se/) 改造的可复用库。
与原版的 `curl` 命令行工具或 `libcurl` 不同，**curl-runner** 让你可以在程序内部直接以函数调用的方式使用 curl 的完整功能，而无需对接 `libcurl` 或依赖外部的 `curl.exe`。  

> 简单来说，就是把 `curl` 从命令行工具改造成了一个 **可调用的库**。

## 功能特色

- 完全保留了 curl 的命令行参数体系
- 支持通过 `argc/argv` 风格的接口在你的应用中直接调用
- 捕获并返回 stdout / stderr 的输出
- 基于 libcurl，具备强大而稳定的 HTTP(S)、FTP、SFTP 等协议支持
- 提供同步执行接口，可嵌入任意 C++ 程序中
- 可编译为静态库或动态库（.lib / .dll）

## 安装与构建

克隆本仓库：

```bash
git clone https://github.com/Pectics/curl-runner.git
```

使用 CMake 构建（示例，具体视你的环境而定）：

```bash
cmake -B build -S .
cmake --build build --config Release
```

输出将包含：

* `curl_runner.lib` / `curl_runner.dll` (Windows)
* 或对应的 `.a` / `.so` (Linux)

## 使用示例

```cpp
#include "curl_runner.h"
#include <iostream>

int main() {
    const char* argv[] = {"-s", "https://example.com"};
    int argc = sizeof(argv) / sizeof(argv[0]);

    CurlResult result = curl_run(argc, const_cast<char**>(argv));
    std::cout << "Exit code: " << result.exit_code << "\n";
    std::cout << "Stdout:\n" << result.stdout_str << "\n";
    std::cerr << "Stderr:\n" << result.stderr_str << "\n";

    return 0;
}
```

输出将与命令行运行如下指令一致：

```bash
curl -s https://example.com
```

## 接口说明

### `CurlResult` 结构体

```cpp
struct CurlResult {
    int exit_code;             // curl 的退出码
    std::string stdout_str;    // 捕获的标准输出
    std::string stderr_str;    // 捕获的错误输出
};
```

### `curl_run` 函数

```cpp
CurlResult curl_run(int argc, char* argv[]);
```

* 参数与原生 `curl` 的 CLI 相似，不需要给定 `"curl"` 参数
* 返回 `CurlResult`，包含执行结果

## License

本项目基于 [curl](https://curl.se/)
原始版权声明见 [LICENSE](https://curl.se/docs/copyright.html)。
修改部分由 Pectics 开发，保持与原版相同的 MIT-like 协议。

## 联系

* 原版 curl: [curl.se](https://curl.se/)
* 本项目 Issues: [GitHub Issues](https://github.com/yourname/curl_runner/issues)

## 致谢

感谢 Daniel Stenberg 及 curl 项目的所有贡献者。
curl-runner 项目的灵感与核心功能均来自于 [curl](https://curl.se/)。
