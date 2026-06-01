# oSIP (libosip2) API 参考

> 本文档整理自系统安装的头文件 `/usr/local/include/`

---

## 1. oSIP 数据类型

### 1.1 核心结构体

#### osip_t
```c
struct osip {
  void *application_context;          // 用户定义指针

  void *ict_fastmutex;                // ICT 事务互斥锁
  void *ist_fastmutex;                // IST 事务互斥锁
  void *nict_fastmutex;               // NICT 事务互斥锁
  void *nist_fastmutex;               // NIST 事务互斥锁
  void *ixt_fastmutex;                // IXT 互斥锁
  void *id_mutex;                     // ID 生成互斥锁
  int transactionid;                  // 上一个事务 ID

  osip_list_t osip_ict_transactions;  // ICT 事务列表
  osip_list_t osip_ist_transactions;  // IST 事务列表
  osip_list_t osip_nict_transactions; // NICT 事务列表
  osip_list_t osip_nist_transactions; // NIST 事务列表

  osip_list_t ixt_retransmissions;    // IXT 重传列表

  osip_message_cb_t msg_callbacks[OSIP_MESSAGE_CALLBACK_COUNT];
  osip_kill_transaction_cb_t kill_callbacks[OSIP_KILL_CALLBACK_COUNT];
  osip_transport_error_cb_t tp_error_callbacks[OSIP_TRANSPORT_ERROR_CALLBACK_COUNT];

  int (*cb_send_message)(osip_transaction_t *, osip_message_t *, char *, int, int);

  void *osip_ict_hastable;            // ICT 哈希表
  void *osip_ist_hastable;            // IST 哈希表
  void *osip_nict_hastable;           // NICT 哈希表
  void *osip_nist_hastable;           // NIST 哈希表
};
typedef struct osip osip_t;
```

#### osip_transaction_t
```c
struct osip_transaction {
  void *your_instance;        // 用户定义指针
  int transactionid;          // 内部事务标识符
  osip_fifo_t *transactionff; // 事件 FIFO 队列

  osip_via_t *topvia;         // Top Via (CALL-LEG 定义)
  osip_from_t *from;          // From 头部
  osip_to_t *to;              // To 头部
  osip_call_id_t *callid;     // Call-ID 头部
  osip_cseq_t *cseq;          // CSeq 头部

  osip_message_t *orig_request;   // 初始请求
  osip_message_t *last_response;  // 最后响应
  osip_message_t *ack;            // 发送的 ACK 请求

  state_t state;              // 当前事务状态

  time_t birth_time;          // 事务创建时间
  time_t completed_time;      // 事务结束时间
  struct timeval created_time;
  struct timeval destroyed_time;

  int in_socket;              // 入站消息 socket（可选）
  int out_socket;             // 出站消息 socket（可选）

  void *config;               // 内部配置

  osip_fsm_type_t ctx_type;   // 事务类型
  osip_ict_t *ict_context;    // ICT 上下文
  osip_ist_t *ist_context;    // IST 上下文
  osip_nict_t *nict_context;  // NICT 上下文
  osip_nist_t *nist_context;  // NIST 上下文

  osip_srv_record_t record;       // SRV 记录
  osip_naptr_t *naptr_record;     // NAPTR 记录

  void *reserved1;            // 用户保留指针
  void *reserved2;
  void *reserved3;
  void *reserved4;
  void *reserved5;
  void *reserved6;
};
typedef struct osip_transaction osip_transaction_t;
```

#### osip_message_t
```c
struct osip_message {
  char *sip_version;          // SIP 版本（仅请求）
  osip_uri_t *req_uri;        // Request-URI（仅请求）
  char *sip_method;           // 方法名（仅请求）

  int status_code;            // 状态码（仅响应）
  char *reason_phrase;        // 原因短语（仅响应）

  osip_list_t accepts;              // Accept 头部列表
  osip_list_t accept_encodings;     // Accept-Encoding 头部列表
  osip_list_t accept_languages;     // Accept-Language 头部列表
  osip_list_t alert_infos;          // Alert-Info 头部列表
  osip_list_t allows;               // Allow 头部列表
  osip_list_t authentication_infos; // Authentication-Info 头部列表
  osip_list_t authorizations;       // Authorization 头部列表

  osip_call_id_t *call_id;    // Call-ID 头部
  osip_list_t call_infos;     // Call-Info 头部列表
  osip_list_t contacts;       // Contact 头部列表

  osip_list_t content_encodings;    // Content-Encoding 头部列表
  osip_content_length_t *content_length; // Content-Length 头部
  osip_content_type_t *content_type;     // Content-Type 头部
  osip_cseq_t *cseq;           // CSeq 头部

  osip_list_t error_infos;    // Error-Info 头部列表
  osip_from_t *from;           // From 头部
  osip_mime_version_t *mime_version; // Mime-Version 头部

  osip_list_t proxy_authenticates;   // Proxy-Authenticate 头部列表
  osip_list_t proxy_authentication_infos; // Proxy-Authentication-Info 头部列表
  osip_list_t proxy_authorizations;  // Proxy-Authorization 头部列表

  osip_list_t record_routes;  // Record-Route 头部列表
  osip_list_t routes;         // Route 头部列表

  osip_to_t *to;              // To 头部
  osip_list_t vias;           // Via 头部列表
  osip_list_t www_authenticates; // WWW-Authenticate 头部列表

  osip_list_t headers;        // 其他头部列表
  osip_list_t bodies;         // 消息体附件列表

  int message_property;       // 内部值
  char *message;              // 内部缓冲区
  size_t message_length;      // 内部值

  void *application_data;     // 上层应用数据
};
typedef struct osip_message osip_message_t;
```

#### osip_event_t
```c
struct osip_event {
  type_t type;              // 事件类型
  int transactionid;        // 关联的事务 ID
  osip_message_t *sip;      // SIP 消息（可选）
};
typedef struct osip_event osip_event_t;
```

### 1.2 枚举类型

#### state_t - 事务状态
```c
typedef enum _state_t {
  // ICT (Invite Client Transaction) 状态
  ICT_PRE_CALLING,
  ICT_CALLING,
  ICT_PROCEEDING,
  ICT_COMPLETED,
  ICT_TERMINATED,

  // IST (Invite Server Transaction) 状态
  IST_PRE_PROCEEDING,
  IST_PROCEEDING,
  IST_COMPLETED,
  IST_CONFIRMED,
  IST_TERMINATED,

  // NICT (Non-Invite Client Transaction) 状态
  NICT_PRE_TRYING,
  NICT_TRYING,
  NICT_PROCEEDING,
  NICT_COMPLETED,
  NICT_TERMINATED,

  // NIST (Non-Invite Server Transaction) 状态
  NIST_PRE_TRYING,
  NIST_TRYING,
  NIST_PROCEEDING,
  NIST_COMPLETED,
  NIST_TERMINATED,

  // Dialog 状态
  DIALOG_EARLY,
  DIALOG_CONFIRMED,
  DIALOG_CLOSE
} state_t;
```

#### type_t - 事件类型
```c
typedef enum type_t {
  // ICT 超时事件
  TIMEOUT_A,   // Timer A
  TIMEOUT_B,   // Timer B
  TIMEOUT_D,   // Timer D

  // NICT 超时事件
  TIMEOUT_E,   // Timer E
  TIMEOUT_F,   // Timer F
  TIMEOUT_K,   // Timer K

  // IST 超时事件
  TIMEOUT_G,   // Timer G
  TIMEOUT_H,   // Timer H
  TIMEOUT_I,   // Timer I

  // NIST 超时事件
  TIMEOUT_J,   // Timer J

  // 入站消息事件
  RCV_REQINVITE,     // 收到 INVITE 请求
  RCV_REQACK,        // 收到 ACK 请求
  RCV_REQUEST,       // 收到非 INVITE/ACK 请求
  RCV_STATUS_1XX,    // 收到 1xx 响应
  RCV_STATUS_2XX,    // 收到 2xx 响应
  RCV_STATUS_3456XX, // 收到 3xx/4xx/5xx/6xx 响应

  // 出站消息事件
  SND_REQINVITE,     // 发送 INVITE 请求
  SND_REQACK,        // 发送 ACK 请求
  SND_REQUEST,       // 发送非 INVITE/ACK 请求
  SND_STATUS_1XX,    // 发送 1xx 响应
  SND_STATUS_2XX,    // 发送 2xx 响应
  SND_STATUS_3456XX, // 发送 3xx/4xx/5xx/6xx 响应

  KILL_TRANSACTION,  // 终止事务事件
  UNKNOWN_EVT        // 未知事件
} type_t;
```

#### osip_fsm_type_t - 事务类型
```c
typedef enum osip_fsm_type_t {
  ICT,   // Invite Client (outgoing) Transaction
  IST,   // Invite Server (incoming) Transaction
  NICT,  // Non-Invite Client (outgoing) Transaction
  NIST   // Non-Invite Server (incoming) Transaction
} osip_fsm_type_t;
```

#### osip_message_callback_type_t - 消息回调类型
```c
typedef enum osip_message_callback_type {
  // ICT 回调
  OSIP_ICT_INVITE_SENT,              // INVITE 已发送
  OSIP_ICT_INVITE_SENT_AGAIN,        // INVITE 重传
  OSIP_ICT_ACK_SENT,                 // ACK 已发送
  OSIP_ICT_ACK_SENT_AGAIN,           // ACK 重传
  OSIP_ICT_STATUS_1XX_RECEIVED,      // 收到 1xx
  OSIP_ICT_STATUS_2XX_RECEIVED,      // 收到 2xx
  OSIP_ICT_STATUS_2XX_RECEIVED_AGAIN,// 2xx 再次收到
  OSIP_ICT_STATUS_3XX_RECEIVED,      // 收到 3xx
  OSIP_ICT_STATUS_4XX_RECEIVED,      // 收到 4xx
  OSIP_ICT_STATUS_5XX_RECEIVED,      // 收到 5xx
  OSIP_ICT_STATUS_6XX_RECEIVED,      // 收到 6xx
  OSIP_ICT_STATUS_3456XX_RECEIVED_AGAIN,
  OSIP_ICT_STATUS_TIMEOUT,           // Timer B 超时

  // IST 回调
  OSIP_IST_INVITE_RECEIVED,          // 收到 INVITE
  OSIP_IST_INVITE_RECEIVED_AGAIN,    // INVITE 再次收到
  OSIP_IST_ACK_RECEIVED,             // 收到 ACK
  OSIP_IST_ACK_RECEIVED_AGAIN,       // ACK 再次收到
  OSIP_IST_STATUS_1XX_SENT,          // 发送 1xx
  OSIP_IST_STATUS_2XX_SENT,          // 发送 2xx
  OSIP_IST_STATUS_2XX_SENT_AGAIN,    // 2xx 重传
  OSIP_IST_STATUS_3XX_SENT,          // 发送 3xx
  OSIP_IST_STATUS_4XX_SENT,          // 发送 4xx
  OSIP_IST_STATUS_5XX_SENT,          // 发送 5xx
  OSIP_IST_STATUS_6XX_SENT,          // 发送 6xx
  OSIP_IST_STATUS_3456XX_SENT_AGAIN,

  // NICT 回调
  OSIP_NICT_REGISTER_SENT,           // REGISTER 已发送
  OSIP_NICT_BYE_SENT,                // BYE 已发送
  OSIP_NICT_OPTIONS_SENT,            // OPTIONS 已发送
  OSIP_NICT_INFO_SENT,               // INFO 已发送
  OSIP_NICT_CANCEL_SENT,             // CANCEL 已发送
  OSIP_NICT_NOTIFY_SENT,             // NOTIFY 已发送
  OSIP_NICT_SUBSCRIBE_SENT,          // SUBSCRIBE 已发送
  OSIP_NICT_UNKNOWN_REQUEST_SENT,    // 未知请求已发送
  OSIP_NICT_REQUEST_SENT_AGAIN,      // 请求重传
  OSIP_NICT_STATUS_1XX_RECEIVED,     // 收到 1xx
  OSIP_NICT_STATUS_2XX_RECEIVED,     // 收到 2xx
  OSIP_NICT_STATUS_2XX_RECEIVED_AGAIN,
  OSIP_NICT_STATUS_3XX_RECEIVED,     // 收到 3xx
  OSIP_NICT_STATUS_4XX_RECEIVED,     // 收到 4xx
  OSIP_NICT_STATUS_5XX_RECEIVED,     // 收到 5xx
  OSIP_NICT_STATUS_6XX_RECEIVED,     // 收到 6xx
  OSIP_NICT_STATUS_3456XX_RECEIVED_AGAIN,
  OSIP_NICT_STATUS_TIMEOUT,          // Timer F 超时

  // NIST 回调
  OSIP_NIST_REGISTER_RECEIVED,       // 收到 REGISTER
  OSIP_NIST_BYE_RECEIVED,            // 收到 BYE
  OSIP_NIST_OPTIONS_RECEIVED,        // 收到 OPTIONS
  OSIP_NIST_INFO_RECEIVED,           // 收到 INFO
  OSIP_NIST_CANCEL_RECEIVED,         // 收到 CANCEL
  OSIP_NIST_NOTIFY_RECEIVED,         // 收到 NOTIFY
  OSIP_NIST_SUBSCRIBE_RECEIVED,      // 收到 SUBSCRIBE
  OSIP_NIST_UNKNOWN_REQUEST_RECEIVED,// 收到未知请求
  OSIP_NIST_REQUEST_RECEIVED_AGAIN,  // 请求再次收到
  OSIP_NIST_STATUS_1XX_SENT,         // 发送 1xx
  OSIP_NIST_STATUS_2XX_SENT,         // 发送 2xx
  OSIP_NIST_STATUS_2XX_SENT_AGAIN,   // 2xx 重传
  OSIP_NIST_STATUS_3XX_SENT,         // 发送 3xx
  OSIP_NIST_STATUS_4XX_SENT,         // 发送 4xx
  OSIP_NIST_STATUS_5XX_SENT,         // 发送 5xx
  OSIP_NIST_STATUS_6XX_SENT,         // 发送 6xx
  OSIP_NIST_STATUS_3456XX_SENT_AGAIN,

  OSIP_MESSAGE_CALLBACK_COUNT        // 枚举结束标记
} osip_message_callback_type_t;
```

### 1.3 回调函数类型
```c
// 消息回调
typedef void (*osip_message_cb_t)(int type, osip_transaction_t *, osip_message_t *);

// 事务终止回调
typedef void (*osip_kill_transaction_cb_t)(int type, osip_transaction_t *);

// 传输错误回调
typedef void (*osip_transport_error_cb_t)(int type, osip_transaction_t *, int error);
```

### 1.4 事务上下文结构体

#### osip_ict_t (Invite Client Transaction)
```c
struct osip_ict {
  int timer_a_length;           // Timer A: T1, 2*T1...
  struct timeval timer_a_start; // Timer A (重传)
  int timer_b_length;           // Timer B: 64*T1
  struct timeval timer_b_start; // Timer B (事务超时)
  int timer_d_length;           // Timer D: >=32s (不可靠传输)
  struct timeval timer_d_start; // Timer D
  char *destination;            // 发送请求的 IP
  int port;                     // 下一跳端口
};
```

#### osip_ist_t (Invite Server Transaction)
```c
struct osip_ist {
  int timer_g_length;           // Timer G: MIN(T1*2, T2)
  struct timeval timer_g_start; // Timer G (不可靠传输)
  int timer_h_length;           // Timer H: 64*T1
  struct timeval timer_h_start; // Timer H (无 ACK 时触发)
  int timer_i_length;           // Timer I: T4 (不可靠传输)
  struct timeval timer_i_start; // Timer I (吸收所有 ACK)
};
```

#### osip_nict_t (Non-Invite Client Transaction)
```c
struct osip_nict {
  int timer_e_length;           // Timer E: T1, 2*T1...
  struct timeval timer_e_start; // Timer E (重传)
  int timer_f_length;           // Timer F: 64*T1
  struct timeval timer_f_start; // Timer F (事务超时)
  int timer_k_length;           // Timer K: T4 (可靠传输为0)
  struct timeval timer_k_start; // Timer K
  char *destination;            // 发送请求的 IP
  int port;                     // 下一跳端口
};
```

#### osip_nist_t (Non-Invite Server Transaction)
```c
struct osip_nist {
  int timer_j_length;           // Timer J: 64*T1 (可靠传输为0)
  struct timeval timer_j_start; // Timer J
};
```

### 1.5 DNS 相关结构体

#### osip_srv_entry_t
```c
struct osip_srv_entry {
  char srv[512];                // SRV 记录
  int priority;                 // 优先级
  int weight;                   // 权重
  int rweight;                  // 相对权重
  int port;                     // 端口
  char ipaddress[512];          // IP 地址结果
  struct timeval srv_is_broken; // SRV 记录损坏时间
};
```

#### osip_srv_record_t
```c
struct osip_srv_record {
  char name[1024];              // 名称
  int srv_state;                // SRV 状态
  char flag[256];               // 标志: "S", "A", "U", "P"
  char protocol[1024];          // 传输协议
  char regexp[1024];            // 正则表达式
  char replacement[1024];       // 替换
  int order;                    // 顺序
  int preference;               // 偏好
  int index;                    // 索引
  osip_srv_entry_t srventry[10];// 结果表
};
```

#### osip_naptr_t
```c
struct osip_naptr {
  char domain[512];             // 域名
  char AUS[64];                 // User Application String (用于 ENUM)
  int naptr_state;              // NAPTR 状态
  void *arg;                    // 参数
  int keep_in_cache;            // 缓存值
  struct osip_srv_record sipudp_record;  // UDP NAPTR 结果
  struct osip_srv_record siptcp_record;  // TCP NAPTR 结果
  struct osip_srv_record siptls_record;  // TLS NAPTR 结果
  struct osip_srv_record sipdtls_record; // DTLS NAPTR 结果
  struct osip_srv_record sipsctp_record; // SCTP NAPTR 结果
  struct osip_srv_record sipenum_record; // ENUM NAPTR 结果
};
```

---

## 2. oSIP API 函数

### 2.1 初始化与清理

```c
// 初始化 osip
int osip_init(osip_t **osip);

// 释放资源
void osip_release(osip_t *osip);

// 设置应用上下文
void osip_set_application_context(osip_t *osip, void *pointer);

// 获取应用上下文
void *osip_get_application_context(osip_t *osip);
```

### 2.2 消息解析与构建

```c
// 初始化 SIP 消息
int osip_message_init(osip_message_t **sip);

// 释放 SIP 消息
void osip_message_free(osip_message_t *sip);

// 解析 SIP 消息
int osip_message_parse(osip_message_t *sip, const char *buf, size_t length);

// 解析 message/sipfrag
int osip_message_parse_sipfrag(osip_message_t *sip, const char *buf, size_t length);

// SIP 消息转字符串
int osip_message_to_str(osip_message_t *sip, char **dest, size_t *message_length);

// message/sipfrag 转字符串
int osip_message_to_str_sipfrag(osip_message_t *sip, char **dest, size_t *message_length);

// 克隆 SIP 消息
int osip_message_clone(const osip_message_t *sip, osip_message_t **dest);
```

### 2.3 消息属性访问

```c
// 设置/获取方法
void osip_message_set_method(osip_message_t *sip, char *method);
char *osip_message_get_method(const osip_message_t *sip);

// 设置/获取状态码
void osip_message_set_status_code(osip_message_t *sip, int statuscode);
int osip_message_get_status_code(const osip_message_t *sip);

// 设置/获取原因短语
void osip_message_set_reason_phrase(osip_message_t *sip, char *reason);
char *osip_message_get_reason_phrase(const osip_message_t *sip);

// 设置/获取 SIP 版本
void osip_message_set_version(osip_message_t *sip, char *version);
char *osip_message_get_version(const osip_message_t *sip);

// 设置/获取 Request-URI
void osip_message_set_uri(osip_message_t *sip, osip_uri_t *uri);
osip_uri_t *osip_message_get_uri(const osip_message_t *sip);
```

### 2.4 消息类型检测宏

```c
#define MSG_IS_RESPONSE(msg)   ((msg)->status_code != 0)
#define MSG_IS_REQUEST(msg)    ((msg)->status_code == 0)

#define MSG_IS_INVITE(msg)     (MSG_IS_REQUEST(msg) && 0 == strcmp((msg)->sip_method, "INVITE"))
#define MSG_IS_ACK(msg)        (MSG_IS_REQUEST(msg) && 0 == strcmp((msg)->sip_method, "ACK"))
#define MSG_IS_REGISTER(msg)   (MSG_IS_REQUEST(msg) && 0 == strcmp((msg)->sip_method, "REGISTER"))
#define MSG_IS_BYE(msg)        (MSG_IS_REQUEST(msg) && 0 == strcmp((msg)->sip_method, "BYE"))
#define MSG_IS_OPTIONS(msg)    (MSG_IS_REQUEST(msg) && 0 == strcmp((msg)->sip_method, "OPTIONS"))
#define MSG_IS_INFO(msg)       (MSG_IS_REQUEST(msg) && 0 == strcmp((msg)->sip_method, "INFO"))
#define MSG_IS_CANCEL(msg)     (MSG_IS_REQUEST(msg) && 0 == strcmp((msg)->sip_method, "CANCEL"))
#define MSG_IS_REFER(msg)      (MSG_IS_REQUEST(msg) && 0 == strcmp((msg)->sip_method, "REFER"))
#define MSG_IS_NOTIFY(msg)     (MSG_IS_REQUEST(msg) && 0 == strcmp((msg)->sip_method, "NOTIFY"))
#define MSG_IS_SUBSCRIBE(msg)  (MSG_IS_REQUEST(msg) && 0 == strcmp((msg)->sip_method, "SUBSCRIBE"))
#define MSG_IS_MESSAGE(msg)    (MSG_IS_REQUEST(msg) && 0 == strcmp((msg)->sip_method, "MESSAGE"))
#define MSG_IS_PRACK(msg)      (MSG_IS_REQUEST(msg) && 0 == strcmp((msg)->sip_method, "PRACK"))
#define MSG_IS_UPDATE(msg)     (MSG_IS_REQUEST(msg) && 0 == strcmp((msg)->sip_method, "UPDATE"))
#define MSG_IS_PUBLISH(msg)    (MSG_IS_REQUEST(msg) && 0 == strcmp((msg)->sip_method, "PUBLISH"))

#define MSG_IS_STATUS_1XX(msg) ((msg)->status_code >= 100 && (msg)->status_code < 200)
#define MSG_IS_STATUS_2XX(msg) ((msg)->status_code >= 200 && (msg)->status_code < 300)
#define MSG_IS_STATUS_3XX(msg) ((msg)->status_code >= 300 && (msg)->status_code < 400)
#define MSG_IS_STATUS_4XX(msg) ((msg)->status_code >= 400 && (msg)->status_code < 500)
#define MSG_IS_STATUS_5XX(msg) ((msg)->status_code >= 500 && (msg)->status_code < 600)
#define MSG_IS_STATUS_6XX(msg) ((msg)->status_code >= 600 && (msg)->status_code < 700)

#define MSG_TEST_CODE(msg, code) (MSG_IS_RESPONSE(msg) && (code) == (msg)->status_code)
#define MSG_IS_RESPONSE_FOR(msg, reqname) (MSG_IS_RESPONSE(msg) && 0 == strcmp((msg)->cseq->method, (reqname)))
```

### 2.5 事务管理

```c
// 初始化事务
int osip_transaction_init(osip_transaction_t **transaction,
                          osip_fsm_type_t ctx_type,
                          osip_t *osip,
                          osip_message_t *request);

// 释放事务
int osip_transaction_free(osip_transaction_t *transaction);

// 释放事务（已从列表移除）
int osip_transaction_free2(osip_transaction_t *transaction);

// 执行事务状态机
int osip_transaction_execute(osip_transaction_t *transaction, osip_event_t *evt);

// 添加事件到事务
int osip_transaction_add_event(osip_transaction_t *transaction, osip_event_t *evt);

// 设置/获取用户上下文（已废弃）
int osip_transaction_set_your_instance(osip_transaction_t *transaction, void *ptr);
void *osip_transaction_get_your_instance(osip_transaction_t *transaction);

// 设置/获取保留指针
int osip_transaction_set_reserved1(osip_transaction_t *transaction, void *ptr);
int osip_transaction_set_reserved2(osip_transaction_t *transaction, void *ptr);
int osip_transaction_set_reserved3(osip_transaction_t *transaction, void *ptr);
int osip_transaction_set_reserved4(osip_transaction_t *transaction, void *ptr);
int osip_transaction_set_reserved5(osip_transaction_t *transaction, void *ptr);
int osip_transaction_set_reserved6(osip_transaction_t *transaction, void *ptr);

void *osip_transaction_get_reserved1(osip_transaction_t *transaction);
void *osip_transaction_get_reserved2(osip_transaction_t *transaction);
void *osip_transaction_get_reserved3(osip_transaction_t *transaction);
void *osip_transaction_get_reserved4(osip_transaction_t *transaction);
void *osip_transaction_get_reserved5(osip_transaction_t *transaction);
void *osip_transaction_get_reserved6(osip_transaction_t *transaction);

// 设置目的地
int osip_ict_set_destination(osip_ict_t *ict, char *destination, int port);
int osip_nict_set_destination(osip_nict_t *nict, char *destination, int port);

// 获取目的地
int osip_transaction_get_destination(osip_transaction_t *transaction, char **ip, int *port);

// 获取响应目的地
void osip_response_get_destination(osip_message_t *response, char **address, int *portnum);

// 设置 SRV/NAPTR 记录
int osip_transaction_set_srv_record(osip_transaction_t *transaction, osip_srv_record_t *record);
int osip_transaction_set_naptr_record(osip_transaction_t *transaction, osip_naptr_t *record);

// 设置 socket
int osip_transaction_set_in_socket(osip_transaction_t *transaction, int sock);
int osip_transaction_set_out_socket(osip_transaction_t *transaction, int sock);

// 从 osip 栈移除事务
int osip_remove_transaction(osip_t *osip, osip_transaction_t *ict);
```

### 2.6 事件处理

```c
// 解析 SIP 消息字符串创建事件
osip_event_t *osip_parse(const char *buf, size_t length);

// 创建出站 SIP 消息事件
osip_event_t *osip_new_outgoing_sipmessage(osip_message_t *sip);

// 释放事件
void osip_event_free(osip_event_t *event);
```

### 2.7 回调注册

```c
// 注册消息回调
int osip_set_message_callback(osip_t *osip, int type, osip_message_cb_t cb);

// 注册事务终止回调
int osip_set_kill_transaction_callback(osip_t *osip, int type, osip_kill_transaction_cb_t cb);

// 注册传输错误回调
int osip_set_transport_error_callback(osip_t *osip, int type, osip_transport_error_cb_t cb);

// 注册发送消息回调
void osip_set_cb_send_message(osip_t *cf,
                              int (*cb)(osip_transaction_t *,
                                        osip_message_t *,
                                        char *, int, int));
```

### 2.8 事务执行

```c
// 执行所有 ICT 事务
int osip_ict_execute(osip_t *osip);

// 执行所有 IST 事务
int osip_ist_execute(osip_t *osip);

// 执行所有 NICT 事务
int osip_nict_execute(osip_t *osip);

// 执行所有 NIST 事务
int osip_nist_execute(osip_t *osip);
```

### 2.9 定时器

```c
// 获取最小超时时间
void osip_timers_gettimeout(osip_t *osip, struct timeval *lower_tv);

// 执行 ICT 定时器
void osip_timers_ict_execute(osip_t *osip);

// 执行 IST 定时器
void osip_timers_ist_execute(osip_t *osip);

// 执行 NICT 定时器
void osip_timers_nict_execute(osip_t *osip);

// 执行 NIST 定时器
void osip_timers_nist_execute(osip_t *osip);
```

### 2.10 事务查找

```c
// 查找匹配事务
osip_transaction_t *osip_transaction_find(osip_list_t *transactions, osip_event_t *evt);

// 查找事务并添加事件
int osip_find_transaction_and_add_event(osip_t *osip, osip_event_t *evt);

// 创建事务
osip_transaction_t *osip_create_transaction(osip_t *osip, osip_event_t *evt);
```

### 2.11 重传管理

```c
// 执行重传
void osip_retransmissions_execute(osip_t *osip);

// 启动 200 OK 重传
void osip_start_200ok_retransmissions(osip_t *osip,
                                       struct osip_dialog *dialog,
                                       osip_message_t *msg200ok,
                                       int sock);

// 启动 ACK 重传
void osip_start_ack_retransmissions(osip_t *osip,
                                    struct osip_dialog *dialog,
                                    osip_message_t *ack,
                                    char *dest,
                                    int port,
                                    int sock);

// 停止 200 OK 重传
struct osip_dialog *osip_stop_200ok_retransmissions(osip_t *osip, osip_message_t *ack);

// 停止 dialog 相关重传
void osip_stop_retransmissions_from_dialog(osip_t *osip, struct osip_dialog *dialog);
```

### 2.12 事件类型检测宏

```c
#define EVT_IS_RCV_INVITE(event)       (event->type == RCV_REQINVITE)
#define EVT_IS_RCV_ACK(event)          (event->type == RCV_REQACK)
#define EVT_IS_RCV_REQUEST(event)      (event->type == RCV_REQUEST)
#define EVT_IS_RCV_STATUS_1XX(event)   (event->type == RCV_STATUS_1XX)
#define EVT_IS_RCV_STATUS_2XX(event)   (event->type == RCV_STATUS_2XX)
#define EVT_IS_RCV_STATUS_3456XX(event)(event->type == RCV_STATUS_3456XX)

#define EVT_IS_SND_INVITE(event)       (event->type == SND_REQINVITE)
#define EVT_IS_SND_ACK(event)          (event->type == SND_REQACK)
#define EVT_IS_SND_REQUEST(event)      (event->type == SND_REQUEST)
#define EVT_IS_SND_STATUS_1XX(event)   (event->type == SND_STATUS_1XX)
#define EVT_IS_SND_STATUS_2XX(event)   (event->type == SND_STATUS_2XX)
#define EVT_IS_SND_STATUS_3456XX(event)(event->type == SND_STATUS_3456XX)

#define EVT_IS_INCOMINGMSG(event)  (event->type >= RCV_REQINVITE && event->type <= RCV_STATUS_3456XX)
#define EVT_IS_INCOMINGREQ(event)  (EVT_IS_RCV_INVITE(event) || EVT_IS_RCV_ACK(event) || EVT_IS_RCV_REQUEST(event))
#define EVT_IS_INCOMINGRESP(event) (EVT_IS_RCV_STATUS_1XX(event) || EVT_IS_RCV_STATUS_2XX(event) || EVT_IS_RCV_STATUS_3456XX(event))

#define EVT_IS_OUTGOINGMSG(event)  (event->type >= SND_REQINVITE && event->type <= SND_STATUS_3456XX)
#define EVT_IS_OUTGOINGREQ(event)  (EVT_IS_SND_INVITE(event) || EVT_IS_SND_ACK(event) || EVT_IS_SND_REQUEST(event))
#define EVT_IS_OUTGOINGRESP(event) (EVT_IS_SND_STATUS_1XX(event) || EVT_IS_SND_STATUS_2XX(event) || EVT_IS_SND_STATUS_3456XX(event))

#define EVT_IS_MSG(event)          (event->type >= RCV_REQINVITE && event->type <= SND_STATUS_3456XX)
#define EVT_IS_KILL_TRANSACTION(event) (event->type == KILL_TRANSACTION)
```

---

## 3. 头文件路径

### oSIP 头文件
```
/usr/local/include/osip2/osip.h
/usr/local/include/osip2/osip_fifo.h
/usr/local/include/osip2/osip_mt.h
/usr/local/include/osip2/osip_dialog.h
/usr/local/include/osip2/osip_time.h
/usr/local/include/osip2/osip_condv.h

/usr/local/include/osipparser2/osip_parser.h
/usr/local/include/osipparser2/osip_message.h
/usr/local/include/osipparser2/osip_uri.h
/usr/local/include/osipparser2/osip_headers.h
/usr/local/include/osipparser2/osip_body.h
/usr/local/include/osipparser2/osip_const.h
/usr/local/include/osipparser2/osip_port.h
/usr/local/include/osipparser2/osip_list.h
/usr/local/include/osipparser2/osip_md5.h
/usr/local/include/osipparser2/sdp_message.h

/usr/local/include/osipparser2/headers/osip_via.h
/usr/local/include/osipparser2/headers/osip_from.h
/usr/local/include/osipparser2/headers/osip_to.h
/usr/local/include/osipparser2/headers/osip_call_id.h
/usr/local/include/osipparser2/headers/osip_cseq.h
/usr/local/include/osipparser2/headers/osip_contact.h
/usr/local/include/osipparser2/headers/osip_route.h
/usr/local/include/osipparser2/headers/osip_record_route.h
/usr/local/include/osipparser2/headers/osip_authorization.h
/usr/local/include/osipparser2/headers/osip_www_authenticate.h
/usr/local/include/osipparser2/headers/osip_proxy_authenticate.h
/usr/local/include/osipparser2/headers/osip_proxy_authorization.h
/usr/local/include/osipparser2/headers/osip_content_type.h
/usr/local/include/osipparser2/headers/osip_content_length.h
/usr/local/include/osipparser2/headers/osip_accept.h
/usr/local/include/osipparser2/headers/osip_allow.h
# ... 更多头部文件
```


---

*文档生成日期: 2026-03-25*
*基于系统安装的头文件: /usr/local/include/*
