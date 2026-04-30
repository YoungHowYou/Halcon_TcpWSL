# Linux 构建与部署说明

`Halcon_TcpWSL` 在 Linux 上既可以从源码构建，也可以直接用工程内已编好的 `.so` 部署。本文档分两条主线：

- **新机器从零部署**（一条命令，5 分钟内跑通）→ §1
- **手动构建** + **故障排除** + **跨平台差异** → §2 起

---

## 1. 新机器从零部署（推荐）

工程目录里已带 `scripts/install_linux.sh`，把它跑一次就完成所有事情：

```bash
# 1. 先把整个工程目录拷到新机器（git clone / scp / U 盘都行）
cd /path/to/Halcon_TcpWSL

# 2. HALCON 必须先装好（商业产品，无法 bundle）；export HALCONROOT 指向它
export HALCONROOT=/path/to/HALCON-24.11-Progress-Steady

# 3. 一键部署
bash scripts/install_linux.sh
```

脚本会自动完成：

| 步骤 | 做什么 | 是否需要 sudo |
|---|---|---|
| 1 | 校验 HALCON 安装 | – |
| 2 | apt 装 `libmfx-gen1.2`、`intel-media-va-driver-non-free`、`libva-drm2`、`build-essential`、`cmake` | ✓ |
| 3 | 把当前用户加进 `render` 组 + setfacl 给 `/dev/dri/*` 立即生效 | ✓ |
| 4 | 写一份格式合规的 `/etc/environment`（自动备份原文件） | ✓ |
| 5 | `cmake` 配置 + 编译，产物同步到 `lib/x64-linux/` | – |
| 6 | ldd 检查 + H264 编码器 smoke test | – |

**完成后再做一件事**：GUI **完整注销重登一次**（让 `render` 组真正应用，不是关终端）。脚本里 `setfacl` 是临时手段让当前 shell 立即可用，重启后失效，但 `render` 组在重登之后会永久接管。

部署完成之后 HDevelop 启动会自动从 `HALCONEXTENSIONS` 加载本扩展包，可直接调用 `WCreateConnection` / `WSendData` / `WRecvData` / `WCloseConnection`。

> **如果脚本失败** 看 §5 故障排除，所有踩过的坑都列在那里。

---

## 2. 系统要求

| 项 | 最低 | 推荐 | 说明 |
|---|---|---|---|
| OS | Ubuntu 22.04 | Ubuntu 24.04 | 其他 glibc 系发行版同样可用，包名以本文为准 |
| 架构 | x86_64 | – | HALCON 仅提供 `x64-linux` |
| g++/gcc | 9（C++17） | 11+ | – |
| CMake | 3.20 | 3.28+ | 仅 `HALCON_DOTNET=ON` 时才需 4.1.1（Linux 默认关闭） |
| HALCON | 24.11 | 24.11 | 商业产品，必须人工安装 |
| GPU | Intel Gen8+（集显也可） | Intel Gen11+ | H264 走 oneVPL + VAAPI 硬件路径；oneVPL 软件 fallback 在 Ubuntu 仓库不可用 |

---

## 3. 工程内已 bundle 的依赖

[3rd/DjiH264Encoder/lib/x64-linux/](../3rd/DjiH264Encoder/lib/x64-linux/) 目录里 bundle 了如下三方库（含完整 SONAME 链）：

```
libvpl.so       → libvpl.so.2       → libvpl.so.2.9        (229 KB, oneVPL dispatcher)
libyuv.so       → libyuv.so.0       → libyuv.so.0.0.1883   (620 KB, YUV/RGB 转换)
libjpeg.so      → libjpeg.so.8      → libjpeg.so.8.2.2     (534 KB, libyuv 间接依赖)
```

`libHalcon_TcpWSL.so` 内部已烧入 RPATH：

```
$ORIGIN:$ORIGIN/../3rd/DjiH264Encoder/lib/x64-linux
```

也就是说：**这三个库不依赖系统 apt 包，把工程目录拷过去就能解析**。`ldd bin/libHalcon_TcpWSL.so` 应该看到 `libvpl.so.2` / `libyuv.so.0` / `libjpeg.so.8` 都从工程内解析。

---

## 4. 手动构建（不想用脚本时）

```bash
# 4.1 装基础包
sudo apt update
sudo apt install -y build-essential cmake \
    libmfx-gen1.2 intel-media-va-driver-non-free libva-drm2

# 4.2 把自己加进 render 组（让进程能开 GPU）
sudo usermod -aG render "$USER"
# 然后 GUI 完整注销重登一次

# 4.3 设环境变量（务必单引号；/etc/environment 不支持反斜杠续行，见 §5）
export HALCONROOT=/path/to/HALCON-24.11-Progress-Steady
export HALCONEXAMPLES="$HALCONROOT/examples"
export LD_LIBRARY_PATH="$HALCONROOT/lib/x64-linux:$LD_LIBRARY_PATH"

# 4.4 配置 + 编译
cmake -S . -B build_linux
cmake --build build_linux -j

# 4.5 同步到 lib/x64-linux/（HALCONEXTENSIONS 标准位置）
mkdir -p lib/x64-linux
cp bin/libHalcon_TcpWSL.so bin/libHalcon_TcpWSLc.so bin/libHalcon_TcpWSLcpp.so lib/x64-linux/

# 4.6 注册到 HDevelop
export HALCONEXTENSIONS="$PWD"

# 4.7 验证
readelf -d lib/x64-linux/libHalcon_TcpWSL.so | grep -E "RPATH|NEEDED"
LD_LIBRARY_PATH=$HALCONROOT/lib/x64-linux ldd lib/x64-linux/libHalcon_TcpWSL.so
```

---

## 5. 故障排除（踩过的坑全在这里）

### 5.1 `Could not find libvpl (oneVPL)` 在 cmake 配置阶段

CMake 找不到 oneVPL。3rd 里 bundle 已就位时不会触发；如果 3rd 被删了：

```bash
sudo apt install libvpl-dev libyuv-dev
```

### 5.2 `CMake 4.1.1 or higher is required`

只有显式开了 `-DHALCON_DOTNET=ON` 才会触发；Linux 默认关闭 .NET，最低 CMake 3.20。删掉这个开关重新配置即可。

### 5.3 `libhalcon.so.24.11.1 => not found`

HALCON 自家库找不到。这种库**不能 bundle**（HALCON 是商业产品）。两个解决：

```bash
# 方式 A
export LD_LIBRARY_PATH="$HALCONROOT/lib/x64-linux:$LD_LIBRARY_PATH"
# 方式 B
sudo bash -c 'echo "$HALCONROOT/lib/x64-linux" > /etc/ld.so.conf.d/halcon.conf && ldconfig'
```

或者直接让 §1 的脚本写 `/etc/environment`。

### 5.4 H264 编码器初始化失败（HALCON 报 `H264编码器未初始化错误`）

按这个顺序排查：

**(a) `libmfx-gen1.2` / Intel 媒体驱动 没装**

```bash
sudo apt install -y libmfx-gen1.2 intel-media-va-driver-non-free libva-drm2
```

oneVPL 是 dispatcher 模式：`libvpl.so` 自己只是个壳，必须有真实 runtime（GPU 路径靠 `libmfx-gen.so.1.2`，软件 fallback 靠 `libvplswref64.so.1`，后者 Ubuntu 仓库未提供）。

**(b) 用户没在 `render` 组**

```bash
groups | grep render            # 没输出就是没进
sudo usermod -aG render "$USER"
# 然后 GUI 完整注销重登（关终端不算，必须退到登录界面再登）
```

**(c) 临时不想重登**：用 setfacl 给当前会话授权（重启失效）

```bash
sudo setfacl -m "u:$USER:rw" /dev/dri/renderD128
sudo setfacl -m "u:$USER:rw" /dev/dri/card0
```

**(d) 验证**：跑工程自带的 smoke test

```bash
# 期望输出 [1/3] [2/3] [3/3] 全部 0
g++ -std=c++17 scripts/test_h264_init.cpp \
    -I3rd/DjiH264Encoder/include \
    -L3rd/DjiH264Encoder/lib/x64-linux -lvpl \
    -Wl,-rpath,$PWD/3rd/DjiH264Encoder/lib/x64-linux \
    -Wno-deprecated-declarations -o /tmp/h264_test
/tmp/h264_test
```

`MFXVideoENCODE.Query` 返回 `-3` 几乎一定是 GPU 设备权限问题（看 (b)(c)）。

### 5.5 `/etc/environment` 写法错误（最隐蔽的一个坑）

`/etc/environment` 由 PAM 的 `pam_env.so` 解析，**不是 shell**：

| 写法 | shell `source` | `/etc/environment` |
|---|---|---|
| `KEY="VALUE"` | ✓ | ✓ |
| 反斜杠 `\` 续行 | ✓ | ✗ **不支持** |
| `$VAR` 替换 | ✓ | ✗ 不展开 |
| `export` 关键字 | 必需 | 不识别 |

**反斜杠续行是常见的写错**：

```bash
# ✗ 错误（pam_env 只读第一行；bash source 兼容续行但缩进空白被并入变量值）
LD_LIBRARY_PATH="/path/a:\
                /path/b:\
                /path/c"
# 结果：LD_LIBRARY_PATH="/path/a:                /path/b:                /path/c"
# loader 把 `:                /path/b` 当作带前导空格的路径名，找不到。

# ✓ 正确：写成单行
LD_LIBRARY_PATH="/path/a:/path/b:/path/c"
```

要美观折行写在 `/etc/profile.d/halcon.sh`（那是真 shell 脚本，支持续行 + `$VAR` + `export`）。

§1 的脚本会自动检测并修复反斜杠续行。

### 5.6 `vainfo` 段错误 / 输出截断

通常是当前进程没有 `/dev/dri/renderD128` 访问权限。修复见 5.4(b)(c)。

### 5.7 想换更新版的 libvpl/libyuv

```bash
sudo apt install libvpl-dev libyuv-dev   # 装系统包
rm -rf 3rd/DjiH264Encoder/lib/x64-linux  # 删 bundle，CMake 自动回退到系统包
rm -rf build_linux && cmake -S . -B build_linux && cmake --build build_linux -j
```

---

## 6. Linux vs Windows 行为差异

| 维度 | Windows | Linux |
|---|---|---|
| 视频内存 | D3D11 硬件纹理 (`DXGI_FORMAT_NV12`) | 系统内存路径（`MFX_IOPATTERN_IN_SYSTEM_MEMORY`） |
| 硬件后端 | Intel D3D11 | Intel VAAPI（`libva` + `iHD` 驱动），通过 `libmfx-gen` runtime |
| 三方库链接 | 工程内 `vpl.lib` / `yuv.lib`（静态） | bundled `libvpl.so` / `libyuv.so` / `libjpeg.so.8`（动态，带 RPATH） |
| 导出符号 | `__declspec(dllexport)` + `.def` | `__attribute__((visibility("default")))` |
| `.def` 文件 | 同时供 `hcomp` 与 MSVC 链接器使用 | 仅供 `hcomp` 生成算子元数据 |
| 默认 .NET 支持 | `HALCON_DOTNET=ON` | `HALCON_DOTNET=OFF` |

---

## 7. 本次为支持 Linux 修改的文件清单

| 文件 | 改动要点 |
|---|---|
| [CMakeLists.txt](../CMakeLists.txt) | 非 Windows 默认关闭 .NET、放宽最低 CMake 版本；`def` 文件改为通用；Linux 下优先 bundled `.so` 回退系统包；`HALCONEXAMPLES` 字面量未展开兜底；`target_link_options` 注入 RPATH |
| [3rd/DjiH264Encoder/lib/x64-linux/](../3rd/DjiH264Encoder/lib/x64-linux/) | bundle libvpl + libyuv + libjpeg + 完整 SONAME 链 |
| [source/encoder_impl.cpp](../source/encoder_impl.cpp) | D3D11 释放代码用 `#ifdef _WIN32` 包住（Linux 上是 `void*`） |
| [source/Halcon_TcpWSL.cpp](../source/Halcon_TcpWSL.cpp) | `(__int64)` → `(Hlong)`、`Hcpar*` → `const Hcpar*` + `const_cast`（Linux HALCON 签名要求 const） |
| [scripts/install_linux.sh](../scripts/install_linux.sh) | 新机器一键部署脚本 |
| [scripts/test_h264_init.cpp](../scripts/test_h264_init.cpp) | H264 编码器初始化 smoke test |
| [doc/Linux构建说明.md](Linux构建说明.md) | 本文档 |

> [include/network.h](../include/network.h)、[source/network.cpp](../source/network.cpp)、[include/Halcon_TcpWSL.h](../include/Halcon_TcpWSL.h) 的跨平台分支早先已存在，本次未动。

---

## 8. 一键命令汇总

新机器：

```bash
git clone <repo> && cd Halcon_TcpWSL
export HALCONROOT=/path/to/HALCON-24.11-Progress-Steady
bash scripts/install_linux.sh
# 然后 GUI 完整注销重登一次
```

老机器拉取最新代码后只重编：

```bash
cd Halcon_TcpWSL
cmake --build build_linux -j
cp -f bin/libHalcon_TcpWSL*.so lib/x64-linux/
```

校验：

```bash
readelf -d lib/x64-linux/libHalcon_TcpWSL.so | grep -E "RPATH|RUNPATH|NEEDED"
LD_LIBRARY_PATH=$HALCONROOT/lib/x64-linux ldd lib/x64-linux/libHalcon_TcpWSL.so
```
