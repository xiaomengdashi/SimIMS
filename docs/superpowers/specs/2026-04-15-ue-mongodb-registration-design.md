# SimIMS 设计文档：UE 注册信息改为 MongoDB 实时查询

## 背景
当前 SimIMS 的 UE/HSS 订阅信息来自 YAML (`hss_adapter.subscribers`)。该模式不符合注册时实时查询用户数据的目标流程。

本设计将 UE 注册相关数据源切换为 MongoDB，并在处理注册相关消息（UAR/MAR/SAR/LIR、Digest 认证查询）时实时查库。实现逻辑参考 open5gs 的数据库访问模式（按请求查询 + SQN 更新），但字段模型采用 SimIMS 自定义结构。

## 目标
- 移除运行时对 `hss_adapter.subscribers` YAML 数据的依赖。
- HSS（`IHssClient` 实现）在处理注册流程时实时查询 MongoDB。
- Digest 认证（`digest_only` / `hybrid_fallback`）改为 MongoDB 查询。
- IMS-AKA 的 `sqn` 在 MAR 处理中执行回写与递增。
- 保持现有服务层接口与调用时序稳定，最小化上层改动。

## 非目标
- 不复用 open5gs 的字段命名与完整文档结构。
- 不引入启动时全量订阅者缓存镜像。
- 不修改 Cx/Rx 协议对象定义（`IHssClient` 接口签名保持不变）。

## 约束与决策
- 数据库：MongoDB。
- 字段模型：SimIMS 自定义（不沿用 open5gs 字段）。
- 查询时机：处理注册相关消息时实时查库。
- 范围：全链路，包括 HSS 查询与 DigestCredentialStore。
- YAML 路径策略：`hss_adapter.subscribers` 彻底下线（A）。
- `sqn` 策略：MAR 中需要更新并递增。

## 架构设计

### 1. 组件划分
新增数据库访问层与 Mongo 版业务适配实现：

1) `src/db/`（或同等 infra 路径）
- `mongo_client.hpp/.cpp`
  - 负责 Mongo 连接生命周期（RAII）、基础错误转换、连接健康检查。
- `subscriber_repository.hpp/.cpp`
  - 聚焦 `subscribers` 集合的读写。
  - 提供按 `impi/impu` 查询、按 username/realm 查询、`sqn` 更新接口。

2) `src/diameter/mongo_hss_client.hpp/.cpp`
- 实现 `IHssClient`。
- 在 `userAuthorization/multimediaAuth/serverAssignment/locationInfo` 内实时查库。

3) `src/s-cscf/mongo_digest_credential_store.hpp/.cpp`
- 实现 `IDigestCredentialStore`。
- 替换 `LocalDigestCredentialStore` 的 YAML 内存数据路径。

4) 入口装配替换
- `src/s-cscf/main.cpp`
- `src/i-cscf/main.cpp`
- `src/allinone/main.cpp`

上述入口改为装配 Mongo 依赖，替代当前 `StubHssClient + make_local_digest_credential_store(config.hss_adapter)`。

### 2. 保持稳定的边界
- `IHssClient` 方法签名不变。
- `ScscfService` / `IcscfService` / `Registrar` / `ImsAkaAuthProvider` 保持调用时序不变，仅替换底层数据提供者。
- 错误继续通过 `Result<T>` 传播。

## 数据模型设计（MongoDB）

集合：`subscribers`

建议文档结构（满足当前业务与后续扩展）：

```json
{
  "imsi": "460001234567890",
  "tel": "1234567890",
  "realm": "ims.local",
  "identities": {
    "impi": "460001234567890@ims.local",
    "canonical_impu": "sip:460001234567890@ims.local",
    "associated_impus": [
      "tel:1234567890",
      "sip:1234567890@ims.local",
      "sip:460001234567890@ims.local"
    ]
  },
  "auth": {
    "password": "<digest-password>",
    "ki": "00112233445566778899aabbccddeeff",
    "operator_code_type": "opc",
    "opc": "00112233445566778899aabbccddeeff",
    "op": "",
    "sqn": 0,
    "amf": "8000"
  },
  "profile": {
    "ifcs": []
  },
  "serving": {
    "assigned_scscf": "sip:127.0.0.1:5062;transport=udp",
    "updated_at": "2026-04-15T00:00:00Z"
  }
}
```

### 索引
- 唯一索引：`imsi`
- 普通索引：`identities.impi`
- 普通索引：`identities.canonical_impu`
- 多键索引：`identities.associated_impus`
- 可选索引：`tel`

## 数据流设计

### UAR (`userAuthorization`)
1. 输入 `impi/impu`。
2. 仓储按以下顺序匹配：
   - `identities.impi == impi`
   - `identities.canonical_impu == normalize(impu)`
   - `identities.associated_impus` 包含 `normalize(impu)`
3. 命中则返回 `UaaResult{ result_code=2001, assigned_scscf=... }`。
4. 未命中返回 `kDiameterUserNotFound`。

### MAR (`multimediaAuth`)
1. 通过身份匹配查询订阅者认证材料（`ki/opc|op/sqn/amf`）。
2. 使用现有 `aka_vector_builder` 逻辑生成 `AuthVector`。
3. 执行 `sqn` 回写与递增（见 SQN 语义）。
4. 返回 `MaaResult`。

### SAR (`serverAssignment`)
1. 查询订阅者 profile + identities。
2. 生成 `SaaResult.user_profile`（`impu/associated_impus/ifcs`）。
3. 可选回写 `serving.assigned_scscf` 与 `updated_at`。

### LIR (`locationInfo`)
1. 以 IMPU 查询订阅者。
2. 读取 `serving.assigned_scscf` 并返回 `LiaResult`。
3. 无 serving 信息按未找到处理。

### Digest 认证查询
`MongoDigestCredentialStore` 提供：
- `findByUsername(username, realm)`
- `findByIdentity(identity)`

数据来自同一 `subscribers` 集合，不再构建启动期内存快照。

## SQN 更新语义
对齐 open5gs 的“处理 MAR 时更新 + 递增”语义：

1. 读取当前 `auth.sqn`（int64）。
2. 若存在重同步场景，按算法得到新 `sqn` 基线。
3. `$set` 回写基线 `sqn`。
4. `$inc`（+32）后再执行按位与掩码（`0x0000FFFFFFFFFFFF`）以保持 48-bit 语义。

要求：
- 在单文档级别使用原子更新，避免并发 REGISTER 导致序列回退或丢增量。
- 更新失败时 MAR 失败返回，不下发 challenge。
- `sqn` 在数据库中使用数值类型存储，不使用十六进制字符串。

## 配置与启动改造

### 配置结构
- 移除 `HssAdapterConfig::subscribers`。
- 在 `hss_adapter` 下新增 Mongo 配置：
  - `mongo_uri`
  - `mongo_db`
  - `mongo_collection`（默认 `subscribers`）

示例：

```yaml
hss_adapter:
  type: diameter
  mongo_uri: mongodb://127.0.0.1:27017/simims
  mongo_db: simims
  mongo_collection: subscribers
```

### 启动装配
- `allinone` / `s-cscf` / `i-cscf` 入口统一注入 `MongoHssClient`。
- S-CSCF 注入 `MongoDigestCredentialStore`。
- Mongo 初始化失败时启动失败（fail-fast）。

## 错误处理与日志
- 连接失败：启动阶段直接失败。
- 查询未命中：映射为 `kDiameterUserNotFound`。
- 字段缺失/格式非法：映射为配置/数据错误（`kConfigInvalidValue` 或新增 DB 数据错误码）。
- `sqn` 更新失败：返回错误并拒绝本次认证。
- 日志脱敏：不输出 `ki/password/op/opc` 明文。

## 测试设计

### 单元测试
1. Repository
- 身份匹配优先级与归一化（IMPI / canonical IMPU / associated IMPUs）。
- 字段映射正确性。
- `sqn` 更新与递增路径。

2. `MongoHssClient`
- UAR/MAR/SAR/LIR 成功路径。
- 用户不存在、字段非法、数据库异常路径。

3. `MongoDigestCredentialStore`
- `findByUsername` 与 `findByIdentity` 成功/未命中。

### 集成测试
- REGISTER 首次注册：确认运行时查库。
- 认证成功后检查 `sqn` 已更新。
- `digest_only` / `hybrid_fallback` 在 Mongo 数据下正常认证。
- I-CSCF LIR 路径与 S-CSCF SAR 路径不再依赖 YAML subscribers。

## 迁移步骤（高层）
1. 新增 Mongo 访问层与仓储。
2. 实现 `MongoHssClient` 并替换入口注入。
3. 实现 `MongoDigestCredentialStore` 并替换 S-CSCF 注入。
4. 移除 YAML subscribers 配置与解析。
5. 增加单元/集成测试，更新示例配置。

## 风险与缓解
- 风险：Mongo 不可用导致服务不可用。
  - 缓解：启动前连接检查 + 明确错误日志。
- 风险：并发下 `sqn` 一致性。
  - 缓解：单文档原子更新 + 失败即返回。
- 风险：identity 归一化不一致导致查不到用户。
  - 缓解：统一复用 `sip::normalize_impu_uri`，并补齐测试覆盖。

## 结论
本方案在不改变上层业务接口的前提下，将 UE 注册相关信息来源切换为 MongoDB 实时查询，覆盖 HSS 与 Digest 全路径，并实现 MAR 阶段 `sqn` 的回写与递增，满足“处理注册消息时查库”的目标。