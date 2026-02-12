Package: fmt:x64-linux-dynamic@12.1.0

**Host Environment**

- Host: x64-linux
- Compiler: GNU 13.1.0
- CMake Version: 3.31.10
-    vcpkg-tool version: 2025-12-16-44bb3ce006467fc13ba37ca099f64077b8bbf84d
    vcpkg-scripts version: aa2d37682e 2026-02-06 (5 天前)

**To Reproduce**

`vcpkg install `

**Failure logs**

```
-- Using cached fmtlib-fmt-12.1.0.tar.gz
-- Extracting source /home/prototype152/vcpkg/downloads/fmtlib-fmt-12.1.0.tar.gz
-- Using source at /home/prototype152/vcpkg/buildtrees/fmt/src/12.1.0-54f1f91231.clean
-- Configuring x64-linux-dynamic
-- Building x64-linux-dynamic-dbg
-- Building x64-linux-dynamic-rel
-- Fixing pkgconfig file: /home/prototype152/vcpkg/packages/fmt_x64-linux-dynamic/lib/pkgconfig/fmt.pc
-- Fixing pkgconfig file: /home/prototype152/vcpkg/packages/fmt_x64-linux-dynamic/debug/lib/pkgconfig/fmt.pc
-- Installing: /home/prototype152/vcpkg/packages/fmt_x64-linux-dynamic/share/fmt/usage
-- Installing: /home/prototype152/vcpkg/packages/fmt_x64-linux-dynamic/share/fmt/copyright
Downloading https://github.com/NixOS/patchelf/releases/download/0.15.5/patchelf-0.15.5-x86_64.tar.gz -> patchelf-0.15.5-x86_64.tar.gz
error: curl: (35) error:0A000126:SSL routines::unexpected eof while reading
note: If you are using a proxy, please ensure your proxy settings are correct.
Possible causes are:
1. You are actually using an HTTP proxy, but setting HTTPS_PROXY variable to `https://address:port`.
This is not correct, because `https://` prefix claims the proxy is an HTTPS proxy, while your proxy (v2ray, shadowsocksr, etc...) is an HTTP proxy.
Try setting `http://address:port` to both HTTP_PROXY and HTTPS_PROXY instead.
2. If you are using Windows, vcpkg will automatically use your Windows IE Proxy Settings set by your proxy software. See: https://github.com/microsoft/vcpkg-tool/pull/77
The value set by your proxy might be wrong, or have same `https://` prefix issue.
3. Your proxy's remote server is out of service.
If you believe this is not a temporary download server failure and vcpkg needs to be changed to download this file from a different location, please submit an issue to https://github.com/Microsoft/vcpkg/issues
CMake Error at scripts/cmake/vcpkg_download_distfile.cmake:136 (message):
  Download failed, halting portfile.
Call Stack (most recent call first):
  scripts/cmake/vcpkg_find_acquire_program.cmake:205 (vcpkg_download_distfile)
  scripts/cmake/z_vcpkg_fixup_rpath.cmake:96 (vcpkg_find_acquire_program)
  scripts/ports.cmake:216 (z_vcpkg_fixup_rpath_in_dir)



```

**Additional context**

<details><summary>vcpkg.json</summary>

```
{
  "name": "ex-miraclevision",
  "version": "0.1.0",
  "dependencies": [
    "fmt",
    "spdlog",
    {
      "name": "opencv4",
      "features": [
        "ffmpeg",
        "dnn",
        "jpeg",
        "png",
        "contrib",
        "tbb",
        "eigen"
      ]
    },
    "eigen3",
    "yaml-cpp",
    "nlohmann-json",
    "tbb"
  ],
  "builtin-baseline": "aa2d37682e3318d93aef87efa7b0e88e81cd3d59"
}

```
</details>
