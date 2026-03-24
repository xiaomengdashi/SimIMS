# oSIP 和 eXosip2 API 参考

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

## 3. eXosip2 数据类型

### 3.1 eXosip_event_type_t - 事件类型
```c
typedef enum eXosip_event_type {
  // 注册相关事件
  EXOSIP_REGISTRATION_SUCCESS,    // 注册成功
  EXOSIP_REGISTRATION_FAILURE,    // 注册失败

  // 呼叫相关事件
  EXOSIP_CALL_INVITE,             // 新来电
  EXOSIP_CALL_REINVITE,           // 呼叫内 re-INVITE
  EXOSIP_CALL_NOANSWER,           // 超时无应答
  EXOSIP_CALL_PROCEEDING,         // 呼叫处理中
  EXOSIP_CALL_RINGING,            // 响铃
  EXOSIP_CALL_ANSWERED,           // 已应答
  EXOSIP_CALL_REDIRECTED,         // 重定向
  EXOSIP_CALL_REQUESTFAILURE,     // 请求失败
  EXOSIP_CALL_SERVERFAILURE,      // 服务器失败
  EXOSIP_CALL_GLOBALFAILURE,      // 全局失败
  EXOSIP_CALL_ACK,                // 收到 200 OK 的 ACK
  EXOSIP_CALL_CANCELLED,          // 呼叫已取消
  EXOSIP_CALL_CLOSED,             // 收到 BYE
  EXOSIP_CALL_RELEASED,           // 呼叫上下文清除

  // 呼叫内请求事件
  EXOSIP_CALL_MESSAGE_NEW,            // 新呼叫内请求
  EXOSIP_CALL_MESSAGE_PROCEEDING,     // 呼叫内请求处理中
  EXOSIP_CALL_MESSAGE_ANSWERED,       // 呼叫内请求成功
  EXOSIP_CALL_MESSAGE_REDIRECTED,     // 呼叫内请求重定向
  EXOSIP_CALL_MESSAGE_REQUESTFAILURE, // 呼叫内请求失败
  EXOSIP_CALL_MESSAGE_SERVERFAILURE,  // 呼叫内请求服务器失败
  EXOSIP_CALL_MESSAGE_GLOBALFAILURE,  // 呼叫内请求全局失败

  // 独立消息事件
  EXOSIP_MESSAGE_NEW,            // 新消息
  EXOSIP_MESSAGE_PROCEEDING,     // 消息处理中
  EXOSIP_MESSAGE_ANSWERED,       // 消息成功
  EXOSIP_MESSAGE_REDIRECTED,     // 消息重定向
  EXOSIP_MESSAGE_REQUESTFAILURE, // 消息请求失败
  EXOSIP_MESSAGE_SERVERFAILURE,  // 消息服务器失败
  EXOSIP_MESSAGE_GLOBALFAILURE,  // 消息全局失败

  // 订阅相关事件
  EXOSIP_SUBSCRIPTION_NOANSWER,       // 订阅无应答
  EXOSIP_SUBSCRIPTION_PROCEEDING,     // 订阅处理中
  EXOSIP_SUBSCRIPTION_ANSWERED,       // 订阅成功
  EXOSIP_SUBSCRIPTION_REDIRECTED,     // 订阅重定向
  EXOSIP_SUBSCRIPTION_REQUESTFAILURE, // 订阅请求失败
  EXOSIP_SUBSCRIPTION_SERVERFAILURE,  // 订阅服务器失败
  EXOSIP_SUBSCRIPTION_GLOBALFAILURE,  // 订阅全局失败
  EXOSIP_SUBSCRIPTION_NOTIFY,         // 收到 NOTIFY

  // 入站订阅事件
  EXOSIP_IN_SUBSCRIPTION_NEW,     // 新 SUBSCRIBE/REFER

  // 通知相关事件
  EXOSIP_NOTIFICATION_NOANSWER,       // 通知无应答
  EXOSIP_NOTIFICATION_PROCEEDING,     // 通知处理中
  EXOSIP_NOTIFICATION_ANSWERED,       // 通知成功
  EXOSIP_NOTIFICATION_REDIRECTED,     // 通知重定向
  EXOSIP_NOTIFICATION_REQUESTFAILURE, // 通知请求失败
  EXOSIP_NOTIFICATION_SERVERFAILURE,  // 通知服务器失败
  EXOSIP_NOTIFICATION_GLOBALFAILURE,  // 通知全局失败

  EXOSIP_EVENT_COUNT             // 事件数量
} eXosip_event_type_t;
```

### 3.2 eXosip_event_t - 事件结构体
```c
struct eXosip_event {
  eXosip_event_type_t type;   // 事件类型
  char textinfo[256];         // 事件文本描述
  void *external_reference;   // 外部引用（用于呼叫）

  osip_message_t *request;    // 当前事务的请求
  osip_message_t *response;   // 当前事务的最后响应
  osip_message_t *ack;        // 当前事务的 ACK

  int tid;                    // 事务 ID（用于应答）
  int did;                    // 对话 ID

  int rid;                    // 注册 ID
  int cid;                    // 呼叫 ID（可能有多个对话）
  int sid;                    // 出站订阅 ID
  int nid;                    // 入站订阅 ID

  int ss_status;              // 当前 Subscription-State
  int ss_reason;              // 当前 Reason 状态
};
typedef struct eXosip_event eXosip_event_t;
```

### 3.3 TLS 配置结构体

#### eXosip_tls_credentials_t
```c
typedef struct eXosip_tls_credentials_s {
  char priv_key[1024];          // 私钥文件绝对路径
  char priv_key_pw[1024];       // 私钥密码
  char cert[1024];              // 证书文件绝对路径
  char public_key_pinned[1024]; // 预期的服务器公钥（DER 格式）
} eXosip_tls_credentials_t;
```

#### eXosip_tls_ctx_t
```c
typedef struct eXosip_tls_ctx_s {
  char random_file[1024];       // 随机数据文件
  char dh_param[1024];          // Diffie-Hellman 参数文件
  char root_ca_cert[1024];      // 根 CA 证书文件
  char cipher_list[2048];       // OpenSSL 密码列表
  unsigned long tls_flags;      // TLS 附加标志
  unsigned long dtls_flags;     // DTLS 附加标志
  eXosip_tls_credentials_t client; // 客户端凭据
  eXosip_tls_credentials_t server; // 服务器凭据
} eXosip_tls_ctx_t;
```

#### eXosip_tls_ctx_error
```c
typedef enum {
  TLS_OK = 0,                    // 成功
  TLS_ERR_NO_RAND = -1,          // 未指定随机文件
  TLS_ERR_NO_DH_PARAM = -2,      // 未指定 DH 参数文件
  TLS_ERR_NO_PW = -3,            // 未指定密码
  TLS_ERR_NO_ROOT_CA = -4,       // 未指定根 CA 文件
  TLS_ERR_MISSING_AUTH_PART = -5 // 缺少私钥或证书
} eXosip_tls_ctx_error;
```

### 3.4 其他结构体

#### eXosip_dns_cache
```c
struct eXosip_dns_cache {
  char host[1024];
  char ip[256];
};
```

#### eXosip_account_info
```c
struct eXosip_account_info {
  char proxy[1024];
  char nat_ip[256];
  int nat_port;
};
```

#### eXosip_stats
```c
struct eXosip_stats {
  int allocated_transactions;    // 当前分配的事务数
  float average_transactions;    // 平均新事务数/小时
  int allocated_registrations;   // 当前分配的注册数
  float average_registrations;   // 平均新注册数/小时
  int allocated_calls;           // 当前分配的呼叫数
  float average_calls;           // 平均新呼叫数/小时
  int allocated_publications;    // 当前分配的发布数
  float average_publications;    // 平均新发布数/小时
  int allocated_subscriptions;   // 当前分配的出站订阅数
  float average_subscriptions;   // 平均新订阅数/小时
  int allocated_insubscriptions; // 当前分配的入站订阅数
  float average_insubscriptions; // 平均新入站订阅数/小时
  int reserved1[20];             // 保留
};
```

---

## 4. eXosip2 API 函数

### 4.1 初始化与配置 (eX_setup.h)

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

// 设置选项
int eXosip_set_option(struct eXosip_t *excontext, int opt, const void *value);

// 监听地址
int eXosip_listen_addr(struct eXosip_t *excontext,
                       int transport,
                       const char *addr,
                       int port,
                       int family,
                       int secure);

// 重置传输 socket
int eXosip_reset_transports(struct eXosip_t *excontext);

// 设置 socket
int eXosip_set_socket(struct eXosip_t *excontext,
                      int transport,
                      int socket,
                      int port);

// 设置 User-Agent
void eXosip_set_user_agent(struct eXosip_t *excontext, const char *user_agent);

// 获取版本
const char *eXosip_get_version(void);

// 设置消息回调
int eXosip_set_cbsip_message(struct eXosip_t *excontext, CbSipCallback cbsipCallback);

// NAT 穿透：伪装 Contact
void eXosip_masquerade_contact(struct eXosip_t *excontext,
                               const char *public_address,
                               int port);

// 查找空闲端口
int eXosip_find_free_port(struct eXosip_t *excontext,
                          int free_port,
                          int transport);

// 猜测本地 IP
int eXosip_guess_localip(struct eXosip_t *excontext,
                         int family,
                         char *address,
                         int size);

// 设置传输协议
int eXosip_transport_set(osip_message_t *msg, const char *transport);
```

### 4.2 配置选项常量

```c
#define EXOSIP_OPT_BASE_OPTION 0

#define EXOSIP_OPT_UDP_KEEP_ALIVE              (EXOSIP_OPT_BASE_OPTION + 1)
#define EXOSIP_OPT_AUTO_MASQUERADE_CONTACT     (EXOSIP_OPT_BASE_OPTION + 2)
#define EXOSIP_OPT_USE_RPORT                   (EXOSIP_OPT_BASE_OPTION + 7)
#define EXOSIP_OPT_SET_IPV4_FOR_GATEWAY        (EXOSIP_OPT_BASE_OPTION + 8)
#define EXOSIP_OPT_ADD_DNS_CACHE               (EXOSIP_OPT_BASE_OPTION + 9)
#define EXOSIP_OPT_DELETE_DNS_CACHE            (EXOSIP_OPT_BASE_OPTION + 10)
#define EXOSIP_OPT_SET_IPV6_FOR_GATEWAY        (EXOSIP_OPT_BASE_OPTION + 12)
#define EXOSIP_OPT_ADD_ACCOUNT_INFO            (EXOSIP_OPT_BASE_OPTION + 13)
#define EXOSIP_OPT_DNS_CAPABILITIES            (EXOSIP_OPT_BASE_OPTION + 14)
#define EXOSIP_OPT_SET_DSCP                    (EXOSIP_OPT_BASE_OPTION + 15)
#define EXOSIP_OPT_REGISTER_WITH_DATE          (EXOSIP_OPT_BASE_OPTION + 16)
#define EXOSIP_OPT_SET_HEADER_USER_AGENT       (EXOSIP_OPT_BASE_OPTION + 17)
#define EXOSIP_OPT_ENABLE_DNS_CACHE            (EXOSIP_OPT_BASE_OPTION + 18)
#define EXOSIP_OPT_ENABLE_AUTOANSWERBYE        (EXOSIP_OPT_BASE_OPTION + 19)
#define EXOSIP_OPT_ENABLE_IPV6                 (EXOSIP_OPT_BASE_OPTION + 20)
#define EXOSIP_OPT_ENABLE_REUSE_TCP_PORT       (EXOSIP_OPT_BASE_OPTION + 21)
#define EXOSIP_OPT_ENABLE_USE_EPHEMERAL_PORT   (EXOSIP_OPT_BASE_OPTION + 22)
#define EXOSIP_OPT_SET_CALLBACK_WAKELOCK       (EXOSIP_OPT_BASE_OPTION + 23)
#define EXOSIP_OPT_ENABLE_OUTBOUND             (EXOSIP_OPT_BASE_OPTION + 24)
#define EXOSIP_OPT_SET_OC_LOCAL_ADDRESS        (EXOSIP_OPT_BASE_OPTION + 25)
#define EXOSIP_OPT_SET_OC_PORT_RANGE           (EXOSIP_OPT_BASE_OPTION + 26)
#define EXOSIP_OPT_REMOVE_PREROUTESET          (EXOSIP_OPT_BASE_OPTION + 27)
#define EXOSIP_OPT_SET_SIP_INSTANCE            (EXOSIP_OPT_BASE_OPTION + 28)
#define EXOSIP_OPT_SET_MAX_MESSAGE_TO_READ     (EXOSIP_OPT_BASE_OPTION + 29)
#define EXOSIP_OPT_SET_MAX_READ_TIMEOUT        (EXOSIP_OPT_BASE_OPTION + 30)
#define EXOSIP_OPT_SET_DEFAULT_CONTACT_DISPLAYNAME (EXOSIP_OPT_BASE_OPTION + 31)
#define EXOSIP_OPT_SET_SESSIONTIMERS_FORCE     (EXOSIP_OPT_BASE_OPTION + 32)
#define EXOSIP_OPT_FORCE_CONNECTIONREUSE       (EXOSIP_OPT_BASE_OPTION + 33)
#define EXOSIP_OPT_SET_CONTACT_DIALOG_EXTRA_PARAMS (EXOSIP_OPT_BASE_OPTION + 34)

#define EXOSIP_OPT_SET_TLS_VERIFY_CERTIFICATE      (EXOSIP_OPT_BASE_OPTION + 500)
#define EXOSIP_OPT_SET_TLS_CERTIFICATES_INFO       (EXOSIP_OPT_BASE_OPTION + 501)
#define EXOSIP_OPT_SET_TLS_CLIENT_CERTIFICATE_NAME (EXOSIP_OPT_BASE_OPTION + 502)
#define EXOSIP_OPT_SET_TLS_SERVER_CERTIFICATE_NAME (EXOSIP_OPT_BASE_OPTION + 503)

#define EXOSIP_OPT_SET_TSC_SERVER              (EXOSIP_OPT_BASE_OPTION + 1001)
#define EXOSIP_OPT_GET_STATISTICS              (EXOSIP_OPT_BASE_OPTION + 2000)
```

### 4.3 注册管理 (eX_register.h)

```c
// 构建初始 REGISTER
int eXosip_register_build_initial_register(struct eXosip_t *excontext,
                                           const char *from,
                                           const char *proxy,
                                           const char *contact,
                                           int expires,
                                           osip_message_t **reg);

// 构建初始 REGISTER（带 qvalue）
int eXosip_register_build_initial_register_withqvalue(struct eXosip_t *excontext,
                                                      const char *from,
                                                      const char *proxy,
                                                      const char *contact,
                                                      int expires,
                                                      const char *qvalue,
                                                      osip_message_t **reg);

// 构建更新 REGISTER
int eXosip_register_build_register(struct eXosip_t *excontext,
                                   int rid,
                                   int expires,
                                   osip_message_t **reg);

// 发送 REGISTER
int eXosip_register_send_register(struct eXosip_t *excontext,
                                  int rid,
                                  osip_message_t *reg);

// 移除注册（不发送 REGISTER）
int eXosip_register_remove(struct eXosip_t *excontext, int rid);
```

### 4.4 呼叫管理 (eX_call.h)

```c
// 设置/获取应用上下文
int eXosip_call_set_reference(struct eXosip_t *excontext, int id, void *reference);
void *eXosip_call_get_reference(struct eXosip_t *excontext, int cid);

// 构建初始 INVITE
int eXosip_call_build_initial_invite(struct eXosip_t *excontext,
                                     osip_message_t **invite,
                                     const char *to,
                                     const char *from,
                                     const char *route,
                                     const char *subject);

// 发送初始 INVITE
int eXosip_call_send_initial_invite(struct eXosip_t *excontext,
                                    osip_message_t *invite);

// 构建请求（INVITE, OPTIONS, INFO, REFER, UPDATE, NOTIFY）
int eXosip_call_build_request(struct eXosip_t *excontext,
                              int did,
                              const char *method,
                              osip_message_t **request);

// 构建 ACK
int eXosip_call_build_ack(struct eXosip_t *excontext,
                          int tid,
                          osip_message_t **ack);

// 发送 ACK
int eXosip_call_send_ack(struct eXosip_t *excontext,
                         int tid,
                         osip_message_t *ack);

// 构建 REFER
int eXosip_call_build_refer(struct eXosip_t *excontext,
                            int did,
                            const char *refer_to,
                            osip_message_t **request);

// 构建 INFO
int eXosip_call_build_info(struct eXosip_t *excontext,
                           int did,
                           osip_message_t **request);

// 构建 OPTIONS
int eXosip_call_build_options(struct eXosip_t *excontext,
                              int did,
                              osip_message_t **request);

// 构建 UPDATE
int eXosip_call_build_update(struct eXosip_t *excontext,
                             int did,
                             osip_message_t **request);

// 构建 NOTIFY
int eXosip_call_build_notify(struct eXosip_t *excontext,
                             int did,
                             int subscription_status,
                             osip_message_t **request);

// 发送请求
int eXosip_call_send_request(struct eXosip_t *excontext,
                             int did,
                             osip_message_t *request);

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

// 终止呼叫
int eXosip_call_terminate(struct eXosip_t *excontext, int cid, int did);

// 终止呼叫（带 Reason 头）
int eXosip_call_terminate_with_reason(struct eXosip_t *excontext,
                                      int cid,
                                      int did,
                                      const char *reason);

// 终止呼叫（带自定义头）
int eXosip_call_terminate_with_header(struct eXosip_t *excontext,
                                      int cid,
                                      int did,
                                      const char *header_name,
                                      const char *header_value);

// 构建 PRACK
int eXosip_call_build_prack(struct eXosip_t *excontext,
                            int tid,
                            osip_message_t *response1xx,
                            osip_message_t **prack);

// 发送 PRACK
int eXosip_call_send_prack(struct eXosip_t *excontext,
                           int tid,
                           osip_message_t *prack);

// 获取 Refer-To（带 Replace 参数）
int eXosip_call_get_referto(struct eXosip_t *excontext,
                            int did,
                            char *refer_to,
                            size_t refer_to_len);

// 根据 Replaces 查找呼叫
int eXosip_call_find_by_replaces(struct eXosip_t *excontext, char *replaces);
```

### 4.5 事件处理 (eXosip.h)

```c
// 等待事件
eXosip_event_t *eXosip_event_wait(struct eXosip_t *excontext, int tv_s, int tv_ms);

// 获取事件（已废弃，阻塞式）
eXosip_event_t *eXosip_event_get(struct eXosip_t *excontext);

// 获取事件 socket
int eXosip_event_geteventsocket(struct eXosip_t *excontext);

// 释放事件
void eXosip_event_free(eXosip_event_t *je);

// 自动处理（401/407 重试、注册刷新等）
void eXosip_automatic_action(struct eXosip_t *excontext);

// 默认动作
int eXosip_default_action(struct eXosip_t *excontext, eXosip_event_t *je);
```

### 4.6 认证 (eXosip.h)

```c
// 添加认证信息
int eXosip_add_authentication_info(struct eXosip_t *excontext,
                                   const char *username,
                                   const char *userid,
                                   const char *passwd,
                                   const char *ha1,
                                   const char *realm);

// 移除认证信息
int eXosip_remove_authentication_info(struct eXosip_t *excontext,
                                      const char *username,
                                      const char *realm);

// 清除所有认证信息
int eXosip_clear_authentication_info(struct eXosip_t *excontext);
```

### 4.7 SDP 辅助 (eXosip.h)

```c
// 获取远端 SDP
sdp_message_t *eXosip_get_remote_sdp(struct eXosip_t *excontext, int did);
sdp_message_t *eXosip_get_remote_sdp_from_tid(struct eXosip_t *excontext, int tid);

// 获取本地 SDP
sdp_message_t *eXosip_get_local_sdp(struct eXosip_t *excontext, int did);
sdp_message_t *eXosip_get_previous_local_sdp(struct eXosip_t *excontext, int did);
sdp_message_t *eXosip_get_local_sdp_from_tid(struct eXosip_t *excontext, int tid);

// 从消息获取 SDP
sdp_message_t *eXosip_get_sdp_info(osip_message_t *message);

// 获取音频连接信息
sdp_connection_t *eXosip_get_audio_connection(sdp_message_t *sdp);
sdp_media_t *eXosip_get_audio_media(sdp_message_t *sdp);

// 获取视频连接信息
sdp_connection_t *eXosip_get_video_connection(sdp_message_t *sdp);
sdp_media_t *eXosip_get_video_media(sdp_message_t *sdp);

// 获取指定媒体信息
sdp_connection_t *eXosip_get_connection(sdp_message_t *sdp, const char *media);
sdp_media_t *eXosip_get_media(sdp_message_t *sdp, const char *media);
```

### 4.8 DNS 工具 (eX_setup.h)

```c
// NAPTR 查询
struct osip_naptr *eXosip_dnsutils_naptr(struct eXosip_t *excontext,
                                         const char *domain,
                                         const char *protocol,
                                         const char *transport,
                                         int keep_in_cache);

// DNS 处理
int eXosip_dnsutils_dns_process(struct osip_naptr *output_record, int force);

// 释放 DNS 记录
void eXosip_dnsutils_release(struct osip_naptr *naptr_record);

// SRV 轮转
int eXosip_dnsutils_rotate_srv(struct osip_srv_record *output_record);
```

### 4.9 随机数生成 (eXosip.h)

```c
// 生成随机字符串（仅数字）
int eXosip_generate_random(char *buf, int buf_size);

// 生成随机字符串（低熵，十六进制）
int eXosip_hexa_generate_random(char *buf, int buf_size,
                                char *str1, char *str2, char *str3);

// 生成随机字节（高熵，需要 OpenSSL）
int eXosip_byte_generate_random(char *buf, int buf_size);
```

---

## 5. 头文件路径

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

### eXosip2 头文件
```
/usr/local/include/eXosip2/eXosip.h
/usr/local/include/eXosip2/eX_setup.h
/usr/local/include/eXosip2/eX_register.h
/usr/local/include/eXosip2/eX_call.h
/usr/local/include/eXosip2/eX_message.h
/usr/local/include/eXosip2/eX_publish.h
/usr/local/include/eXosip2/eX_subscribe.h
/usr/local/include/eXosip2/eX_options.h
```

---

*文档生成日期: 2026-03-25*
*基于系统安装的头文件: /usr/local/include/*
