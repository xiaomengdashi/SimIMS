# DNS 使用说明（局域网场景）

本文说明如何在局域网测试环境中启用 IMS 域名解析能力，以及当前代码里 DNS 的生效边界。

## 1. 当前实现状态

项目已提供 `ims::dns::DnsResolver`，支持 SIP 常见解析链路：

- `NAPTR -> SRV -> A/AAAA`
- 失败回退到 `A` 记录（`udp/tcp` 默认 5060，`tls` 默认 5061）

配置加载器当前识别的 DNS 配置项是：

- `dns.servers`（DNS 服务器列表）
- `dns.timeout_ms`（超时，毫秒）

注意：

- `dns.timeout` 目前不会被读取。
- 主流程里仍有部分路径使用静态地址（例如 P-CSCF 转发 I-CSCF 默认读取 `icscf.listen_addr`），不是所有路由都已切换到 DNS 动态解析。

## 2. 局域网最小可用配置

### 2.1 规划域名

建议使用测试域名，例如 `ims.lan`（或企业内部统一测试域）。

### 2.2 部署局域网 DNS

可使用 `dnsmasq`、`BIND9`、`CoreDNS` 等，假设 DNS 服务地址为 `192.168.1.53`。

### 2.3 增加基础记录（至少 A 记录）

示例：

- `pcscf.ims.lan A 192.168.1.20`
- `icscf.ims.lan A 192.168.1.21`
- `scscf.ims.lan A 192.168.1.22`
- `hss.ims.lan A 192.168.1.30`

### 2.4（可选）增加 SIP SRV/NAPTR

当你希望按 RFC 3263 做 SIP 服务发现时，添加：

- `SRV _sip._udp.ims.lan -> icscf.ims.lan:5061`
- `SRV _sip._tcp.ims.lan -> icscf.ims.lan:5061`
- `NAPTR ims.lan -> SIP+D2U/SIP+D2T`（指向对应 SRV）

### 2.5 配置 `ims.yaml`

```yaml
scscf:
  domain: ims.lan

dns:
  servers:
    - 192.168.1.53
  timeout_ms: 3000
```

建议：

- `listen_addr` 保持为 `0.0.0.0` 或本机网卡 IP（用于绑定监听）。
- 不要把 `listen_addr` 配成域名。

## 3. 验证步骤

先验证 DNS 记录：

```bash
dig +short icscf.ims.lan A @192.168.1.53
dig +short _sip._udp.ims.lan SRV @192.168.1.53
dig +short ims.lan NAPTR @192.168.1.53
```

再启动 IMS 组件并观察日志是否有 DNS 查询成功/失败信息。

## 4. 常见问题

1. 配了 `dns.timeout` 但超时参数不生效

- 请改为 `dns.timeout_ms`。

2. 域名能解析，但业务仍走静态地址

- 这是当前主流程集成范围导致，需在对应模块显式接入 `DnsResolver`。

3. `0.0.0.0` 被下游当作目标地址

- `0.0.0.0` 仅用于监听绑定，不可作为对端可达地址。
