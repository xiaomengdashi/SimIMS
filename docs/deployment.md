# 部署与编译指南

本文面向首次部署 `SimIMS` 的开发者，重点说明在 Ubuntu 上如何安装依赖、编译、测试、安装并启动项目。

## 1. 适用范围

- 构建系统：CMake 3.22+
- 编译器：支持 C++23 的 GCC / Clang
- 主要依赖：
  - Boost.System
  - libosip2
  - libeXosip2
  - c-ares
  - spdlog
  - yaml-cpp
  - GoogleTest / GoogleMock
- 可选运行依赖：
  - `rtpengine`，用于媒体锚定
  - `baresip`，用于集成测试或手工 SIP 验证

## 2. 代码获取

```bash
git clone <your-repo-url> SimIMS
cd SimIMS
```

如果你已经在本地有仓库，可直接进入项目根目录。

## 3. Ubuntu 部署

### 3.1 推荐系统

- Ubuntu 22.04 LTS
- Ubuntu 24.04 LTS

### 3.2 安装构建依赖

先更新软件源：

```bash
sudo apt update
```

安装编译和测试所需依赖：

```bash
sudo apt install -y \
    build-essential \
    cmake \
    pkg-config \
    ninja-build \
    libboost-system-dev \
    libosip2-dev \
    libexosip2-dev \
    libc-ares-dev \
    libspdlog-dev \
    libyaml-cpp-dev \
    libgtest-dev \
    libgmock-dev
```

如果你计划做媒体联调或端到端验证，还可以安装：

```bash
sudo apt install -y rtpengine baresip
```

说明：

- `libexosip2-dev` 是当前工程必需依赖，顶层 `CMakeLists.txt` 会显式查找它。
- `pkg-config` 用于定位 `libosip2`、`libeXosip2` 和 `c-ares`。
- `ninja-build` 不是强制要求，但配合 CMake 使用时通常更快。

### 3.3 编译

推荐使用 out-of-tree 构建：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

如果你更习惯 Makefile 生成器，也可以使用：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

### 3.4 运行单元测试

```bash
cd build
ctest --output-on-failure
```

如果只想执行单元测试，可加标签或正则进一步筛选；当前仓库还定义了基于 `baresip` 的集成测试，运行前请先确认对应环境已准备完成。

### 3.5 安装到系统目录

默认安装目标包括：

- 可执行文件：`bin/`
- 配置模板：`etc/ims/`

示例：

```bash
cmake --install build --prefix /opt/simims
```

安装后目录通常类似：

```text
/opt/simims/
├── bin/
│   ├── ims_allinone
│   ├── ims_pcscf
│   ├── ims_icscf
│   └── ims_scscf
└── etc/ims/
    └── ims.yaml
```

### 3.6 启动方式

#### 方式 A：一体化模式

适合单机调试和功能验证。

```bash
cp config/ims.yaml config/local.yaml
vim config/local.yaml

./bin/ims_allinone config/local.yaml
```

#### 方式 B：分网元模式

适合分别观察 P-CSCF / I-CSCF / S-CSCF 日志和行为。

```bash
./bin/ims_scscf config/ims.yaml
./bin/ims_icscf config/ims.yaml
./bin/ims_pcscf config/ims.yaml
```

建议在不同终端分别启动，以便排查 SIP 注册、路由和鉴权流程。

## 4. 配置建议

第一次启动前，建议至少检查以下字段：

- `pcscf.listen_addr` / `listen_port`
- `icscf.listen_addr` / `listen_port`
- `scscf.listen_addr` / `listen_port`
- `scscf.domain`
- `scscf.hss`
- `hss_adapter`
- `media.rtpengine_host` / `rtpengine_port`
- `dns.servers`

示例：

```yaml
pcscf:
  listen_addr: 0.0.0.0
  listen_port: 5060

icscf:
  listen_addr: 0.0.0.0
  listen_port: 5061

scscf:
  listen_addr: 0.0.0.0
  listen_port: 5062
  domain: ims.mnc011.mcc460.3gppnetwork.org
```

如果是单机实验环境，建议优先使用默认端口和本地地址，把问题先收敛到配置和协议流程本身。

## 5. 常见问题

### 5.1 CMake 找不到 `libeXosip2`

确认系统已安装 Ubuntu 开发包：`libexosip2-dev`

也可以检查 `pkg-config` 是否能识别：

```bash
pkg-config --modversion libeXosip2
```

### 5.2 CMake 找不到 `libosip2` 或 `libcares`

分别检查：

```bash
pkg-config --modversion libosip2
pkg-config --modversion libcares
```

若命令失败，说明对应开发包尚未正确安装，或者 `PKG_CONFIG_PATH` 未包含实际安装路径。

### 5.3 执行文件不在 `build/src/...`

当前顶层 CMake 设置了：

```cmake
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
```

因此编译完成后，可执行文件默认在项目根目录下的 `bin/`，而不是 `build/src/`。

### 5.4 集成测试失败

仓库中包含 `tools/test_baresip_invite.sh`，它依赖额外运行环境，例如：

- `baresip`
- 可写的本地运行目录
- 可启动的 `ims_allinone`
- 可用的 SIP 注册与媒体流程配置

如果你只是验证编译链路，先跑单元测试即可；集成测试建议在 Ubuntu 环境中进行。

## 6. 建议的最小验证流程

### Ubuntu

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config ninja-build \
    libboost-system-dev libosip2-dev libexosip2-dev libc-ares-dev \
    libspdlog-dev libyaml-cpp-dev libgtest-dev libgmock-dev

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cd build && ctest --output-on-failure
```

完成后，如需启动服务，可执行：

```bash
./bin/ims_allinone config/ims.yaml
```
