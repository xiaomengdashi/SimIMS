# oSIP (libosip2) 库概述

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

## 2. 参考资源

### 官方文档
- [GNU oSIP 项目主页](https://www.gnu.org/software/osip/)
- [oSIP API 文档 (Doxygen)](https://www.antisip.com/doc/osip2/)

### RFC 规范
- [RFC 3261 - SIP: Session Initiation Protocol](https://www.ietf.org/rfc/rfc3261.txt)
- [RFC 3262 - Reliability of Provisional Responses](https://www.ietf.org/rfc/rfc3262.txt)
- [RFC 3263 - SIP: Locating SIP Servers](https://www.ietf.org/rfc/rfc3263.txt)
- [RFC 3264 - Offer/Answer Model with SDP](https://www.ietf.org/rfc/rfc3264.txt)
- [RFC 3265 - SIP-Specific Event Notification](https://www.ietf.org/rfc/rfc3265.txt)

### 项目仓库
- [oSIP Savannah 项目](http://savannah.gnu.org/projects/osip/)

---

## 3. 本项目说明

> SimIMS 是一个 **SIP 代理系统**（P-CSCF / I-CSCF / S-CSCF）。
> **libosip2** 仅用于 SIP 消息解析与序列化（`osip_message_t`）；
> 代理转发、事务管理、传输层均由项目自研的 `ProxyCore` / `TransactionLayer` / `SipStack` 实现。

---

*文档生成日期: 2026-03-25*
