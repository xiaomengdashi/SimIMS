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

完整部署说明见：[docs/deployment.md](docs/deployment.md)。

### 依赖安装

```bash
# Ubuntu/Debian
sudo apt install -y \
    build-essential cmake pkg-config ninja-build \
    libboost-system-dev \
    libosip2-dev \
    libexosip2-dev \
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
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### 运行

#### 一体化模式（开发推荐）

```bash
# 编辑配置
cp config/ims.yaml config/local.yaml
vim config/local.yaml

# 启动所有 CSCF
./bin/ims_allinone config/local.yaml
```

#### 独立进程模式

```bash
# 分别启动三个网元
./bin/ims_scscf config/ims.yaml &
./bin/ims_icscf config/ims.yaml &
./bin/ims_pcscf config/ims.yaml &
```

### 测试

```bash
cd build
ctest --output-on-failure
```

## SIP 客户端对接

### S-CSCF 鉴权模式

`scscf.auth_mode` 目前支持三种模式：

- `ims_only`: 仅允许 IMS AKA，适合真实 IMS/VoNR UE
- `digest_only`: 仅允许普通 SIP Digest，适合 Linphone、Zoiper、MicroSIP 这类软电话
- `hybrid_fallback`: 先尝试 IMS AKA，失败后回退到本地 Digest 用户库

`digest_only` 和 `hybrid_fallback` 会复用 `hss_adapter.subscribers` 中的订户信息作为本地 Digest 用户库。

### 普通 SIP 客户端最小配置

如果你要对接普通 SIP 客户端，建议先把 `scscf.auth_mode` 设成 `digest_only`，例如：

```yaml
scscf:
  listen_addr: 0.0.0.0
  listen_port: 5062
  domain: ims.mnc011.mcc460.3gppnetwork.org
  auth_mode: digest_only
```

然后客户端填写：

- `Domain/Realm`: `ims.mnc011.mcc460.3gppnetwork.org`
- `SIP Server`: `sip:<你的S-CSCF地址>:5062`
- `Transport`: `UDP`
- `Password`: 与 `hss_adapter.subscribers[].password` 一致

用户名可使用以下任一种：

- `460112024122023@ims.mnc011.mcc460.3gppnetwork.org`
- `460112024122023`
- `+8613824122023`
- `+8613824122023@ims.mnc011.mcc460.3gppnetwork.org`

建议优先使用完整 IMPI 形式：

```text
460112024122023@ims.mnc011.mcc460.3gppnetwork.org
```

### Linphone 本地联调

仓库里提供了一份本机联调用配置：

- `config/linphone-local.yaml`

另外，baresip 集成测试 `tools/test_baresip_invite.sh` 默认会使用：

- `config/ims-baresip.yaml`

这份测试配置会把 `scscf.auth_mode` 设成 `digest_only`，因为脚本里的 baresip 账号是普通 SIP Digest 客户端模型，不是真实 IMS AKA UE。当前脚本会把 `404` 也视为可验证的 INVITE 响应，用于覆盖被叫 baresip 以 Contact 私有 UA 标识拒绝直拨的场景。

这份配置的特点是：

- `S-CSCF` 监听在 `127.0.0.1:15060`
- `scscf.auth_mode=digest_only`
- 预置两个本地测试账号

```text
1001 / testpass
1002 / testpass
```

启动方式：

```bash
./bin/ims_scscf config/linphone-local.yaml
```

Linphone 中可直接填写：

- `SIP identity`: `sip:1001@ims.local` 或 `sip:1002@ims.local`
- `Authentication username`: `1001@ims.local` 或 `1002@ims.local`
- `Password`: `testpass`
- `Domain`: `ims.local`
- `Proxy / Server`: `sip:127.0.0.1:15060;transport=udp`
- `Transport`: `UDP`

### 关于同机双账号

如果 `1001` 和 `1002` 都在同一个 Linphone 进程里登录，服务端日志里可能会看到它们共用同一个本地 Contact 端口，例如：

```text
127.0.0.1:49441
```

这通常不影响收发，因为：

- 这个端口代表 Linphone 进程的本地 SIP socket
- 真正区分账号的是 `Request-URI`、`From`、`To`、`Authorization`、注册绑定的 `IMPU`

因此，即使两个账号共用同一个本地 UDP 端口，只要客户端本身支持多账号分发，`INVITE` 仍然可以按账号正确投递。

### 什么时候用 hybrid_fallback

如果你希望同一套 S-CSCF 同时服务两类终端，可以用 `hybrid_fallback`：

- 真正的 IMS/VoNR UE 继续走 HSS/MAR/MAA 的 IMS AKA
- HSS 不匹配或实验用软电话，可回退到本地 Digest

这个模式更适合实验环境，不建议把它当作严格 3GPP 兼容行为。

## 项目结构

```
ims/
├── CMakeLists.txt          # 顶层构建配置
├── config/ims.yaml         # 配置模板
├── docs/                   # 文档
│   ├── requirements.md     # 需求分析
│   ├── architecture.md     # 架构设计
│   └── dns.md              # DNS 使用说明（局域网）
├── src/
│   ├── common/             # 公共库实现
│   ├── sip/                # SIP 核心实现
│   ├── dns/                # DNS 解析器实现
│   ├── diameter/           # Diameter Stub 实现
│   ├── rtp/                # Bencode + rtpengine 客户端与媒体会话管理
│   ├── p-cscf/             # P-CSCF 服务 + main
│   ├── i-cscf/             # I-CSCF 服务 + main
│   ├── s-cscf/             # S-CSCF 服务 + main（含 registrar, auth_manager, session_router）
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
  rtpengine:
    host: 127.0.0.1
    port: 22222
  pcf:
    host: 127.0.0.1
    port: 8080

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
  timeout_ms: 3000
```

注意：当前 [config/ims.yaml](/Users/kolane/SimIMS/config/ims.yaml) 里 `pcscf` 和 `dns` 仍保留了一些历史键名示例，实际解析字段以 [config.hpp](/Users/kolane/SimIMS/src/common/config.hpp) 和 [config.cpp](/Users/kolane/SimIMS/src/common/config.cpp) 为准。

DNS 局域网部署与记录示例见：`docs/dns.md`。

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
