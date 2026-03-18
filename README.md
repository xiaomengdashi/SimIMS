# VoNR-Only IMS System

最小化 IMS（IP Multimedia Subsystem）实现，专为 5G VoNR（Voice over New Radio）语音通话设计。

## 概述

本项目实现了 IMS 核心网元（P-CSCF、I-CSCF、S-CSCF），配合已有的 5G 核心网（AMF/SMF/UPF/PCF/UDM），使 5G 终端能够通过 VoNR 进行端到端语音通话。

### 支持的功能

- IMS 注册（双次 REGISTER + AKA 鉴权）
- MO/MT 基本语音呼叫（INVITE/183/PRACK/200/ACK/BYE）
- 媒体锚定（通过 rtpengine）
- QoS 承载请求（Rx 接口，5QI=1）
- 会话释放与资源清理

### 不包含（最小化设计）

- PSTN 互通（MGCF/MGW/BGCF）
- 紧急呼叫（E-CSCF/LRF）
- 补充业务（TAS）
- 会议（MRF）
- 运营商互联（IBCF）

## 技术栈

| 技术 | 用途 |
|------|------|
| C++23 | 核心语言（concepts, jthread, std::expected） |
| libosip2 5.x | SIP 消息解析与构造 |
| Boost.Asio | 异步网络 I/O |
| freeDiameter | Diameter Cx/Rx 协议（规划中） |
| c-ares | 异步 DNS 解析 |
| rtpengine | RTP 媒体代理（外部进程） |
| spdlog | 结构化日志 |
| yaml-cpp | YAML 配置 |
| Google Test + Google Mock | 单元测试与 Mock |
| CMake 3.22+ | 构建系统 |

## 快速开始

### 依赖安装

```bash
# Ubuntu/Debian
sudo apt install -y \
    build-essential cmake pkg-config \
    libboost-system-dev \
    libosip2-dev \
    libc-ares-dev \
    libspdlog-dev \
    libyaml-cpp-dev \
    libgtest-dev \
    libgmock-dev

# 安装 rtpengine（媒体代理，可选）
sudo apt install -y rtpengine
```

### 构建

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . -j$(nproc)
```

### 运行

#### 一体化模式（开发推荐）

```bash
# 编辑配置
cp config/ims.yaml config/local.yaml
vim config/local.yaml

# 启动所有 CSCF
./build/src/allinone/ims_allinone config/local.yaml
```

#### 独立进程模式

```bash
# 分别启动三个网元
./build/src/scscf/ims_scscf config/ims.yaml &
./build/src/icscf/ims_icscf config/ims.yaml &
./build/src/pcscf/ims_pcscf config/ims.yaml &
```

### 测试

```bash
cd build
ctest --output-on-failure
```

## 项目结构

```
ims/
├── CMakeLists.txt          # 顶层构建配置
├── config/ims.yaml         # 配置模板
├── docs/                   # 文档
│   ├── requirements.md     # 需求分析
│   └── architecture.md     # 架构设计
├── include/ims/            # 公共头文件
│   ├── common/             # 基础设施（types, config, logger, io_context）
│   ├── sip/                # SIP 协议栈（message, transport, transaction, dialog, stack）
│   ├── diameter/           # Diameter 接口（IHssClient, IPcfClient）
│   ├── media/              # 媒体接口（IRtpengineClient）
│   ├── dns/                # DNS 解析器
│   └── registration/       # 注册存储接口
├── src/
│   ├── common/             # 公共库实现
│   ├── sip/                # SIP 核心实现
│   ├── dns/                # DNS 解析器实现
│   ├── diameter/           # Diameter Stub 实现
│   ├── media/              # Bencode + rtpengine 客户端
│   ├── registration/       # 内存注册存储
│   ├── pcscf/              # P-CSCF 服务 + main
│   ├── icscf/              # I-CSCF 服务 + main
│   ├── scscf/              # S-CSCF 服务 + main（含 registrar, auth_manager, session_router）
│   └── allinone/           # 一体化二进制
└── tests/
    ├── mocks/              # Google Mock（HSS, PCF, Transport, Store, rtpengine）
    └── unit/               # 单元测试
```

## 配置说明

配置文件 `config/ims.yaml` 包含以下部分：

```yaml
global:
  log_level: info           # trace/debug/info/warn/error/critical
  node_name: ims-node-01

pcscf:
  listen_addr: 0.0.0.0
  listen_port: 5060         # UE 接入端口

icscf:
  listen_addr: 0.0.0.0
  listen_port: 5061

scscf:
  listen_addr: 0.0.0.0
  listen_port: 5062
  domain: ims.example.com   # IMS 域名

hss_adapter:
  type: diameter            # diameter 或 http2
  # ...

media:
  rtpengine_host: 127.0.0.1
  rtpengine_port: 22222

dns:
  servers:
    - 127.0.0.1
  timeout: 3000
```

## 网元端口

| 网元 | 默认端口 | 协议 |
|------|----------|------|
| P-CSCF | 5060 | SIP/UDP |
| I-CSCF | 5061 | SIP/UDP |
| S-CSCF | 5062 | SIP/UDP |
| HSS (Diameter) | 3868 | Diameter/TCP |
| rtpengine | 22222 | Bencode/UDP |

## 开发状态

| 阶段 | 内容 | 状态 |
|------|------|------|
| Phase 1 | 基础框架、SIP 栈、DNS、接口定义 | 已完成 |
| Phase 2 | S-CSCF 注册 + AKA 鉴权 | 骨架完成 |
| Phase 3 | I-CSCF 路由 + DNS 集成 | 骨架完成 |
| Phase 4 | 语音呼叫 SIP 信令 | 骨架完成 |
| Phase 5 | 媒体代理集成 | 骨架完成 |
| Phase 6 | QoS Rx 接口 | 骨架完成 |
| Phase 7 | 加固与测试 | 待开始 |

## 协议参考

- [RFC 3261](https://tools.ietf.org/html/rfc3261) - SIP: Session Initiation Protocol
- [RFC 3327](https://tools.ietf.org/html/rfc3327) - SIP Extension for Path Header
- [RFC 3455](https://tools.ietf.org/html/rfc3455) - Private Header Extensions to SIP for 3GPP
- [3GPP TS 23.228](https://www.3gpp.org/DynaReport/23228.htm) - IMS Architecture
- [3GPP TS 24.229](https://www.3gpp.org/DynaReport/24229.htm) - IMS SIP Procedures
- [3GPP TS 29.229](https://www.3gpp.org/DynaReport/29229.htm) - Cx/Dx Interface (Diameter)
- [3GPP TS 29.214](https://www.3gpp.org/DynaReport/29214.htm) - Rx Interface (Diameter)
- [3GPP TS 33.203](https://www.3gpp.org/DynaReport/33203.htm) - IMS Security (AKA)

## License

Proprietary - Internal Use Only
