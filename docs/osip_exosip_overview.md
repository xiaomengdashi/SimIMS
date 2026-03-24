# oSIP 和 eXosip2 库概述

## 1. oSIP (libosip2)

### 1.1 简介

**oSIP** 是 GNU 项目的一个 SIP（Session Initiation Protocol）协议实现库，遵循 RFC 3261 规范。它是用 C 语言编写的，具有以下特点：

- **许可证**: LGPL（可商用）
- **无依赖**: 仅依赖标准 C 库
- **线程安全**: 支持多线程环境
- **跨平台**: 支持 Linux, Windows, macOS, iOS, Android, 嵌入式系统等

### 1.2 库组成

oSIP 由两个主要组件构成：

| 组件 | 说明 |
|------|------|
| `libosipparser2` | SIP 消息解析器库 |
| `libosip2` | SIP 引擎/状态机库 |

### 1.3 核心功能

#### SIP 解析器
- SIP 请求/响应解析
- SIP URI 解析
- 各种头部解析：Via, CSeq, Call-ID, To, From, Route, Record-Route 等
- 认证相关头部
- Content 相关头部
- Accept 相关头部
- 通用头部
- MIME 附件解析
- SDP 解析器

#### SIP 事务状态机

oSIP 实现了 RFC 3261 定义的 4 种状态机：

| 缩写 | 全称 | RFC 章节 |
|------|------|----------|
| ICT | Invite Client Transaction | 17.1.1 |
| NICT | Non-Invite Client Transaction | 17.1.2 |
| IST | Invite Server Transaction | 17.2.1 |
| NIST | Non-Invite Server Transaction | 17.2.2 |

#### 其他功能
- 线程、信号量、互斥锁抽象
- Dialog 管理 (osip_dialog.h)
- MD5 实现 (用于 SIP Digest 认证)

### 1.4 主要 API

#### 初始化与清理
```c
// 初始化 osip
int osip_init(osip_t **osip);

// 释放资源
void osip_release(osip_t *osip);
```

#### 消息解析
```c
// 解析 SIP 消息
int osip_message_parse(osip_message_t **sip, const char *buf, size_t length);

// 将 SIP 消息转换为字符串
int osip_message_to_str(osip_message_t *sip, char **dest, size_t *length);

// 创建新的 SIP 消息
int osip_message_init(osip_message_t **sip);

// 释放 SIP 消息
void osip_message_free(osip_message_t *sip);
```

#### 事务管理
```c
// 创建事务
int osip_transaction_init(osip_transaction_t **transaction,
                          osip_fsm_type_t type,
                          osip_t *osip,
                          osip_message_t *request);

// 执行事务状态机
int osip_transaction_execute(osip_transaction_t *transaction,
                             osip_event_t *evt);

// 释放事务
void osip_transaction_free(osip_transaction_t *transaction);
```

#### 事件处理
```c
// 解析 SIP 消息字符串创建事件
osip_event_t *osip_parse(const char *buf, size_t length);

// 创建发送事件（用于出站消息）
osip_event_t *osip_new_outgoing_sipmessage(osip_message_t *sip);

// 添加事件到事务
int osip_transaction_add_event(osip_transaction_t *transaction, osip_event_t *evt);

// 释放事件
void osip_event_free(osip_event_t *event);
```

### 1.5 主要头文件

```c
// osip2 库（SIP 状态机）
#include <osip2/osip.h>           // 主头文件，状态机和事务处理
#include <osip2/osip_fifo.h>      // FIFO 队列
#include <osip2/osip_mt.h>        // 多线程支持
#include <osip2/osip_dialog.h>    // Dialog 管理
#include <osip2/osip_time.h>      // 时间相关

// osipparser2 库（SIP 解析器）
#include <osipparser2/osip_parser.h>    // 解析器函数
#include <osipparser2/osip_message.h>   // SIP 消息处理
#include <osipparser2/osip_uri.h>       // URI 处理
#include <osipparser2/osip_headers.h>   // 所有头部
#include <osipparser2/osip_body.h>      // 消息体处理
#include <osipparser2/sdp_message.h>    // SDP 处理
#include <osipparser2/osip_md5.h>       // MD5（用于认证）
```

---

## 2. eXosip2 (libeXosip2)

### 2.1 简介

**eXosip2** 是 oSIP 的扩展库，提供更简单的高级 API，用于实现 SIP 用户代理（User Agent）。

- **许可证**: GPL 或商业许可
- **定位**: 高层 API，隐藏 SIP 复杂性
- **适用场景**: SIP 端点/网关开发

### 2.2 核心功能

eXosip2 支持以下 SIP 功能：

| 功能 | SIP 方法 |
|------|----------|
| 注册 | REGISTER |
| 呼叫建立与修改 | INVITE, re-INVITE |
| 呼叫内方法 | INFO, OPTIONS, UPDATE |
| 呼叫转移 | REFER |
| 可靠临时响应 | PRACK |
| 事件订阅 | SUBSCRIBE/NOTIFY |
| 状态发布 | PUBLISH |
| 即时消息 | MESSAGE |

**注意**: eXosip2 不包含：
- RTP 处理
- 音频接口
- SDP 协商

### 2.3 主要 API

#### 初始化与配置
```c
// 分配 eXosip 上下文
struct eXosip_t *eXosip_malloc(void);

// 初始化库
int eXosip_init(struct eXosip_t *excontext);

// 释放资源
void eXosip_quit(struct eXosip_t *excontext);

// 锁定/解锁
int eXosip_lock(struct eXosip_t *excontext);
int eXosip_unlock(struct eXosip_t *excontext);

// 执行事件循环（非线程模式）
int eXosip_execute(struct eXosip_t *excontext);
```

#### 网络配置
```c
// 监听地址
int eXosip_listen_addr(struct eXosip_t *excontext,
                        int transport,
                        const char *addr,
                        int port,
                        int family,
                        int secure);

// 设置选项
int eXosip_set_option(struct eXosip_t *excontext,
                      int opt,
                      const void *value);

// 设置 User-Agent
void eXosip_set_user_agent(struct eXosip_t *excontext,
                           const char *user_agent);

// NAT 穿透：伪装 Contact 地址
void eXosip_masquerade_contact(struct eXosip_t *excontext,
                               const char *public_address,
                               int port);
```

#### 注册管理
```c
// 构建初始 REGISTER 消息
int eXosip_register_build_initial_register(struct eXosip_t *excontext,
                                           const char *from,
                                           const char *proxy,
                                           const char *contact,
                                           int expires,
                                           osip_message_t **reg);

// 发送 REGISTER
int eXosip_register_send_register(struct eXosip_t *excontext,
                                  int rid,
                                  osip_message_t *reg);

// 更新注册（构建新的 REGISTER）
int eXosip_register_build_register(struct eXosip_t *excontext,
                                   int rid,
                                   int expires,
                                   osip_message_t **reg);

// 移除注册（不发送 REGISTER）
int eXosip_register_remove(struct eXosip_t *excontext, int rid);
```

#### 呼叫管理
```c
// 构建初始 INVITE
int eXosip_call_build_initial_invite(struct eXosip_t *excontext,
                                     osip_message_t **invite,
                                     const char *to,
                                     const char *from,
                                     const char *route,
                                     const char *subject);

// 发送 INVITE
int eXosip_call_send_initial_invite(struct eXosip_t *excontext,
                                    osip_message_t *invite);

// 构建应答
int eXosip_call_build_answer(struct eXosip_t *excontext,
                             int tid,
                             int status,
                             osip_message_t **answer);

// 发送应答
int eXosip_call_send_answer(struct eXosip_t *excontext,
                            int tid,
                            int status,
                            osip_message_t *answer);

// 构建请求（BYE, INFO, OPTIONS 等）
int eXosip_call_build_request(struct eXosip_t *excontext,
                              int did,
                              const char *method,
                              osip_message_t **request);

// 发送请求
int eXosip_call_send_request(struct eXosip_t *excontext,
                             int did,
                             osip_message_t *request);

// 构建 ACK
int eXosip_call_build_ack(struct eXosip_t *excontext,
                          int tid,
                          osip_message_t **ack);

// 发送 ACK
int eXosip_call_send_ack(struct eXosip_t *excontext,
                         int tid,
                         osip_message_t *ack);

// 终止呼叫
int eXosip_call_terminate(struct eXosip_t *excontext,
                          int cid,
                          int did);
```

#### 事件处理
```c
// 等待事件
eXosip_event_t *eXosip_event_wait(struct eXosip_t *excontext,
                                  int tv_sec,
                                  int tv_ms);

// 事件类型枚举
typedef enum eXosip_event_type {
    // 注册相关
    EXOSIP_REGISTRATION_SUCCESS,   // 注册成功
    EXOSIP_REGISTRATION_FAILURE,   // 注册失败

    // 呼叫相关
    EXOSIP_CALL_INVITE,            // 来电
    EXOSIP_CALL_REINVITE,          // re-INVITE
    EXOSIP_CALL_NOANSWER,          // 无应答
    EXOSIP_CALL_PROCEEDING,        // 呼叫处理中
    EXOSIP_CALL_RINGING,           // 响铃
    EXOSIP_CALL_ANSWERED,          // 已应答
    EXOSIP_CALL_REDIRECTED,        // 重定向
    EXOSIP_CALL_REQUESTFAILURE,    // 请求失败
    EXOSIP_CALL_SERVERFAILURE,     // 服务器失败
    EXOSIP_CALL_GLOBALFAILURE,     // 全局失败
    EXOSIP_CALL_ACK,               // ACK 收到
    EXOSIP_CALL_CANCELLED,         // 呼叫取消
    EXOSIP_CALL_CLOSED,            // BYE 收到
    EXOSIP_CALL_RELEASED,          // 呼叫释放

    // 呼叫内消息
    EXOSIP_CALL_MESSAGE_NEW,       // 新呼叫内请求
    EXOSIP_CALL_MESSAGE_ANSWERED,  // 呼叫内消息成功
    EXOSIP_CALL_MESSAGE_REQUESTFAILURE, // 呼叫内消息失败

    // 独立消息
    EXOSIP_MESSAGE_NEW,            // 新消息
    EXOSIP_MESSAGE_ANSWERED,       // 消息成功
    EXOSIP_MESSAGE_REQUESTFAILURE, // 消息失败

    // 订阅相关
    EXOSIP_SUBSCRIPTION_ANSWERED,  // 订阅成功
    EXOSIP_SUBSCRIPTION_NOTIFY,    // NOTIFY 收到
    EXOSIP_IN_SUBSCRIPTION_NEW,    // 新 SUBSCRIBE
} eXosip_event_type_t;

// 事件结构体
struct eXosip_event {
    eXosip_event_type_t type;  // 事件类型
    char textinfo[256];        // 文本描述
    void *external_reference;  // 外部引用
    osip_message_t *request;   // 请求消息
    osip_message_t *response;  // 响应消息
    osip_message_t *ack;       // ACK 消息
    int tid;                   // 事务 ID
    int did;                   // 对话 ID
    int rid;                   // 注册 ID
    int cid;                   // 呼叫 ID
    int sid;                   // 订阅 ID
    int nid;                   // 入站订阅 ID
};

// 释放事件
void eXosip_event_free(eXosip_event_t *event);

// 自动处理（401/407 重试、注册刷新等）
void eXosip_automatic_action(struct eXosip_t *excontext);
```

#### DNS 工具
```c
// NAPTR 查询
struct osip_naptr *eXosip_dnsutils_naptr(struct eXosip_t *excontext,
                                         const char *domain,
                                         const char *protocol,
                                         const char *transport,
                                         int keep_in_cache);

// DNS 处理
int eXosip_dnsutils_dns_process(struct osip_naptr *output_record,
                                int force);

// 释放 DNS 记录
void eXosip_dnsutils_release(struct osip_naptr *naptr_record);
```

### 2.4 配置选项

常用配置宏（定义在 `eX_setup.h`）：

| 选项 | 值类型 | 说明 |
|------|--------|------|
| `EXOSIP_OPT_UDP_KEEP_ALIVE` | `int*` | UDP/TCP/TLS 保活间隔（秒） |
| `EXOSIP_OPT_USE_RPORT` | `int*` | 启用/禁用 Via 中的 rport |
| `EXOSIP_OPT_AUTO_MASQUERADE_CONTACT` | `int*` | 自动伪装 Contact（rport 复用） |
| `EXOSIP_OPT_ENABLE_IPV6` | `int*` | 0=禁用, 1=仅IPv6, 2=自动选择 |
| `EXOSIP_OPT_DNS_CAPABILITIES` | `int*` | 0=禁用, 2=使用 NAPTR/SRV |
| `EXOSIP_OPT_ENABLE_DNS_CACHE` | `int*` | DNS 缓存开关 |
| `EXOSIP_OPT_SET_HEADER_USER_AGENT` | `char*` | User-Agent 头 |
| `EXOSIP_OPT_SET_TLS_VERIFY_CERTIFICATE` | `int*` | TLS 证书验证开关 |
| `EXOSIP_OPT_SET_TLS_CERTIFICATES_INFO` | `eXosip_tls_ctx_t*` | TLS 证书配置 |
| `EXOSIP_OPT_ENABLE_OUTBOUND` | `int*` | RFC 5626 outbound 支持 |
| `EXOSIP_OPT_SET_SIP_INSTANCE` | `char*` | +sip.instance 参数 |
| `EXOSIP_OPT_SET_OC_LOCAL_ADDRESS` | `char*` | 出站连接绑定地址 |
| `EXOSIP_OPT_SET_OC_PORT_RANGE` | `int[2]*` | 出站端口范围 [min, max] |

### 2.5 主要头文件

```c
// eXosip2 库
#include <eXosip2/eXosip.h>       // 主头文件（包含所有子头文件）
#include <eXosip2/eX_setup.h>     // 初始化和配置
#include <eXosip2/eX_register.h>  // 注册相关
#include <eXosip2/eX_call.h>      // 呼叫相关
#include <eXosip2/eX_message.h>   // MESSAGE 相关
#include <eXosip2/eX_publish.h>   // PUBLISH 相关
#include <eXosip2/eX_subscribe.h> // SUBSCRIBE/NOTIFY 相关
#include <eXosip2/eX_options.h>   // OPTIONS 相关

// eXosip2 也依赖 osipparser2
#include <osipparser2/osip_parser.h>
#include <osipparser2/sdp_message.h>
```

---

## 3. oSIP vs eXosip2 对比

| 特性 | oSIP | eXosip2 |
|------|------|---------|
| **许可证** | LGPL | GPL 或商业 |
| **定位** | 底层 SIP 解析器和状态机 | 高层 SIP 用户代理 API |
| **适用场景** | SIP 代理、B2BUA、任何 SIP 实体 | SIP 端点/客户端 |
| **传输层** | 需自行实现 | 内置 UDP/TCP/TLS 支持 |
| **复杂度** | 较高，需理解 SIP 协议细节 | 较低，封装良好 |
| **灵活性** | 极高 | 中等 |
| **消息处理** | 手动解析/构建 | 自动处理 |
| **事件循环** | 需自行实现 | 内置事件循环 |

---

## 4. 选择建议

### 使用 oSIP 的场景
- 实现 SIP 代理服务器（Proxy）
- 实现 B2BUA（Back-to-Back User Agent）
- 需要完全控制 SIP 消息处理
- 项目需要 LGPL 许可证
- 实现非标准 SIP 扩展

### 使用 eXosip2 的场景
- 实现 SIP 客户端/软电话
- 快速开发 SIP 应用
- 不需要深入理解 SIP 协议细节
- 需要内置传输层支持
- 在本项目中承担局部 UA-like 主动出站流程，例如 initial NOTIFY

---

## 5. 参考资源

### 官方文档
- [GNU oSIP 项目主页](https://www.gnu.org/software/osip/)
- [oSIP API 文档 (Doxygen)](https://www.antisip.com/doc/osip2/)
- [eXosip2 API 文档](https://www.antisip.com/doc/exosip2/)

### RFC 规范
- [RFC 3261 - SIP: Session Initiation Protocol](https://www.ietf.org/rfc/rfc3261.txt)
- [RFC 3262 - Reliability of Provisional Responses](https://www.ietf.org/rfc/rfc3262.txt)
- [RFC 3263 - SIP: Locating SIP Servers](https://www.ietf.org/rfc/rfc3263.txt)
- [RFC 3264 - Offer/Answer Model with SDP](https://www.ietf.org/rfc/rfc3264.txt)
- [RFC 3265 - SIP-Specific Event Notification](https://www.ietf.org/rfc/rfc3265.txt)

### 项目仓库
- [oSIP Savannah 项目](http://savannah.gnu.org/projects/osip/)
- [eXosip2 Savannah 项目](http://savannah.gnu.org/projects/exosip)

---

## 6. 本项目说明

> **重要**: SimIMS 项目是一个 **SIP 代理系统**，但现在采用 **osip2 + eXosip2 双栈职责隔离**：
>
> - **libosip2** 负责 proxy plane：SIP 消息解析、`SipMessage` 封装、主监听、事务层、代理转发、Registrar 服务端逻辑
> - **libeXosip2** 负责 UA-like outbound plane：局部主动出站流程，首批用于 S-CSCF `SUBSCRIBE` 后的 initial `NOTIFY`
> - 主业务端口仍由项目自研传输层和事务层持有，不交给 eXosip2 接管
> - osip2 与 eXosip2 之间不直接共享同一个 `osip_message_t*` 所有权，而是在适配层重新构造消息

---

*文档生成日期: 2026-03-25*
