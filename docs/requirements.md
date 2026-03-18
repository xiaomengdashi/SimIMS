# VoNR-Only IMS 系统需求分析文档

> 版本: 1.0
> 日期: 2026-03-18
> 状态: Phase 1 已实现

---

## 1. 项目背景

### 1.1 业务场景

在 5G SA（Standalone）网络中，语音通话通过 VoNR（Voice over New Radio）实现。VoNR 依赖 IMS（IP Multimedia Subsystem）作为控制面，建立和管理 SIP 会话，并通过 5G 核心网的 QoS 机制保障语音质量。

本项目目标是开发一个**最小化 IMS 系统**，仅支持 5G 终端的 VoNR 接入，实现基本语音呼叫功能。

### 1.2 现有网络环境

| 组件 | 状态 | 说明 |
|------|------|------|
| 5G 终端 (UE) | 已有 | 支持 VoNR 的 5G 手机或模组 |
| gNB | 已有 | 5G 基站 |
| AMF | 已有 | 接入和移动性管理 |
| SMF | 已有 | 会话管理 |
| UPF | 已有 | 用户面功能 |
| PCF | 已有 | 策略控制（QoS 策略下发） |
| UDM | 已有 | 统一数据管理 |
| **IMS** | **待开发** | **本项目交付物** |

### 1.3 项目目标

- 补齐 IMS 层，使 5G 终端能通过 VoNR 进行端到端语音通话
- 最小化实现，仅覆盖基本语音场景，不含 PSTN 互通、紧急呼叫、会议等
- 采用现代 C++20 开发，利用成熟开源库

---

## 2. 功能需求

### 2.1 IMS 注册（FR-001）

**描述**: UE 开机后通过 IMS 注册获取服务。

**详细流程**:
1. UE 发送首次 REGISTER 请求（无鉴权信息）
2. P-CSCF 添加 Path 头，转发至 I-CSCF
3. I-CSCF 查询 HSS（UAR）获取 S-CSCF 分配，转发至 S-CSCF
4. S-CSCF 查询 HSS（MAR）获取 AKA 鉴权向量
5. S-CSCF 返回 401 Unauthorized，携带 WWW-Authenticate（nonce = RAND||AUTN）
6. UE 使用 USIM 计算 RES，发送第二次 REGISTER（携带 Authorization）
7. S-CSCF 验证 RES == XRES
8. S-CSCF 通知 HSS（SAR）注册成功，下载用户签约数据
9. S-CSCF 返回 200 OK（携带 Service-Route, P-Associated-URI）

**验收标准**:
- [ ] 完整双次 REGISTER 交互成功
- [ ] AKA 鉴权向量正确传递
- [ ] 注册绑定持久化存储
- [ ] Service-Route 正确下发
- [ ] 支持重注册（re-REGISTER）
- [ ] 支持去注册（Expires: 0）

### 2.2 MO 语音呼叫（FR-002）

**描述**: 主叫 UE 发起语音呼叫。

**详细流程**:
1. 主叫 UE → P-CSCF: INVITE（含 SDP offer）
2. P-CSCF: rtpengine offer（SDP 改写），添加 Record-Route，转发至 S-CSCF
3. S-CSCF: 查找被叫注册信息，添加 Record-Route，转发至被叫 P-CSCF
4. 被叫 P-CSCF → 被叫 UE: INVITE
5. 被叫 UE → 183 Session Progress（含 SDP answer）
6. P-CSCF: rtpengine answer（SDP 改写），Rx AAR 申请 QoS（5QI=1）
7. PRACK 交互确认可靠临时响应
8. 被叫 UE 接听 → 200 OK
9. ACK 完成三次握手
10. 双向 RTP 语音流经 rtpengine 转发

**验收标准**:
- [ ] 完整 INVITE 信令流程
- [ ] SDP 通过 rtpengine 正确改写
- [ ] QoS 承载（5QI=1）成功建立
- [ ] 双向语音可听
- [ ] Record-Route 确保后续请求经过 P-CSCF/S-CSCF

### 2.3 MT 语音呼叫（FR-003）

**描述**: 被叫 UE 收到来自网络侧的呼叫。

**详细流程**:
1. INVITE 到达 I-CSCF
2. I-CSCF 查询 HSS（LIR）获取被叫的 serving S-CSCF
3. S-CSCF 查找被叫注册绑定，转发至被叫 P-CSCF
4. P-CSCF 转发至被叫 UE
5. 后续流程与 MO 呼叫对称

**验收标准**:
- [ ] I-CSCF LIR 查询正确路由
- [ ] 被叫正确接收 INVITE
- [ ] 非注册用户返回 404

### 2.4 会话释放（FR-004）

**描述**: 任一方挂断电话。

**详细流程**:
1. 挂断方发送 BYE
2. BYE 沿 dialog route set 转发
3. P-CSCF 清理 rtpengine 媒体会话
4. P-CSCF 通过 Rx STR 释放 QoS 承载
5. 对端返回 200 OK

**验收标准**:
- [ ] BYE 正确转发
- [ ] rtpengine 会话清理
- [ ] QoS 承载释放
- [ ] 无资源泄漏

### 2.5 呼叫取消（FR-005）

**描述**: 主叫在被叫接听前取消呼叫。

**验收标准**:
- [ ] CANCEL 正确转发
- [ ] 被叫收到 487 Request Terminated
- [ ] 媒体资源正确清理

---

## 3. 非功能需求

### 3.1 性能要求（NFR-001）

| 指标 | 目标值 |
|------|--------|
| 注册处理速率 | ≥ 100 REGISTER/s |
| 呼叫建立时延 | ≤ 500ms（IMS 侧信令处理） |
| 并发活跃呼叫 | ≥ 1000 |
| 内存占用 | ≤ 512MB（1000 活跃呼叫时） |
| CPU 占用 | ≤ 50%（单核，稳态） |

### 3.2 可靠性要求（NFR-002）

- SIP 事务超时重传符合 RFC 3261
- 事务超时后正确清理资源
- 信令处理异常不导致进程崩溃
- 无内存泄漏（ASan 验证通过）

### 3.3 可维护性要求（NFR-003）

- 模块化设计，组件间通过抽象接口解耦
- 依赖注入便于单元测试
- 结构化日志（spdlog），支持日志级别动态调整
- YAML 配置文件，支持多环境部署

### 3.4 可扩展性要求（NFR-004）

- HSS 客户端支持 Diameter 和 HTTP/2 两种实现（通过接口抽象）
- 注册存储支持内存和 Redis 两种后端
- 支持独立进程部署和一体化部署两种模式
- 预留 TAS（补充业务）、MGCF（PSTN 互通）扩展点

### 3.5 安全性要求（NFR-005）

- 支持 IMS AKA 鉴权（Digest-AKAv1-MD5）
- 预留 IPSec（Gm 接口）扩展能力
- 鉴权向量（CK, IK）不在日志中明文输出

---

## 4. 接口需求

### 4.1 Gm 接口（UE ↔ P-CSCF）

| 属性 | 值 |
|------|-----|
| 协议 | SIP over UDP（Phase 1），SIP over IPSec（后续） |
| 端口 | 5060（UDP） |
| 消息 | REGISTER, INVITE, ACK, BYE, CANCEL, PRACK, UPDATE |
| 鉴权 | Digest-AKAv1-MD5 |

### 4.2 Mw 接口（CSCF 间）

| 属性 | 值 |
|------|-----|
| 协议 | SIP over UDP/TCP |
| 端口 | P-CSCF:5060, I-CSCF:5061, S-CSCF:5062 |
| 路由 | DNS NAPTR/SRV + Route/Record-Route |

### 4.3 Cx/Dx 接口（CSCF ↔ HSS）

| 属性 | 值 |
|------|-----|
| 协议 | Diameter（RFC 6733） |
| 应用 | Cx (3GPP TS 29.229) |
| 命令 | UAR/UAA, MAR/MAA, SAR/SAA, LIR/LIA |
| 端口 | 3868 |

### 4.4 Rx 接口（P-CSCF ↔ PCF）

| 属性 | 值 |
|------|-----|
| 协议 | Diameter（RFC 6733） |
| 应用 | Rx (3GPP TS 29.214) |
| 命令 | AAR/AAA, STR/STA, ASR/ASA |
| QoS | 5QI=1（VoNR 语音） |

### 4.5 rtpengine NG 接口

| 属性 | 值 |
|------|-----|
| 协议 | Bencode over UDP |
| 端口 | 22222（默认） |
| 命令 | offer, answer, delete, query, ping |

---

## 5. 约束与假设

### 5.1 约束

1. **仅支持 VoNR**: 不支持 VoLTE、CS fallback、SRVCC
2. **无 PSTN 互通**: 不含 MGCF/MGW/BGCF
3. **无紧急呼叫**: 不含 E-CSCF/LRF
4. **无补充业务**: 不含 TAS（呼叫等待、呼叫转移等）
5. **无会议**: 不含 MRF
6. **单运营商**: 不含 IBCF

### 5.2 假设

1. 5G 核心网（AMF/SMF/UPF/PCF/UDM）已部署且功能正常
2. UE 已完成 5G NAS 注册和 PDU 会话建立
3. PCF 支持 Rx 接口或 N5 接口的 QoS 策略下发
4. 实验室环境可暂时不使用 IPSec
5. rtpengine 作为外部进程独立部署

---

## 6. 用例图

```
┌─────────┐                    ┌──────────────────────────────┐
│   UE    │──── Gm(SIP) ──────│         IMS System           │
│ (5G终端) │                    │                              │
└─────────┘                    │  ┌───────┐  Mw  ┌───────┐   │
                               │  │P-CSCF │─────│I-CSCF │   │
                               │  └───┬───┘      └───┬───┘   │
                               │      │              │        │
                               │      │ Mw      Cx   │        │
                               │      │              │        │
                               │  ┌───┴───┐    ┌─────┴──┐    │
                               │  │S-CSCF │────│HSS/UDM │    │
                               │  └───────┘ Cx └────────┘    │
                               │      │                       │
                               │      │ Rx                    │
                               │      │                       │
                               │  ┌───┴───┐    ┌─────────┐   │
                               │  │  PCF  │    │rtpengine│   │
                               │  └───────┘    └─────────┘   │
                               └──────────────────────────────┘
```

---

## 7. 需求追踪矩阵

| 需求ID | 描述 | 优先级 | 实现阶段 | 状态 |
|--------|------|--------|----------|------|
| FR-001 | IMS 注册 | P0 | Phase 2 | 骨架完成 |
| FR-002 | MO 语音呼叫 | P0 | Phase 4 | 骨架完成 |
| FR-003 | MT 语音呼叫 | P0 | Phase 4 | 骨架完成 |
| FR-004 | 会话释放 | P0 | Phase 4 | 骨架完成 |
| FR-005 | 呼叫取消 | P1 | Phase 4 | 骨架完成 |
| NFR-001 | 性能 | P1 | Phase 7 | 待测试 |
| NFR-002 | 可靠性 | P0 | Phase 7 | 待测试 |
| NFR-003 | 可维护性 | P0 | Phase 1 | 已满足 |
| NFR-004 | 可扩展性 | P1 | Phase 1 | 接口已定义 |
| NFR-005 | 安全性 | P0 | Phase 2 | AKA 骨架完成 |
