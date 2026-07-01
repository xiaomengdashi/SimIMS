#pragma once

#include "scscf_selector.hpp"
#include "sip/stack.hpp"
#include "sip/proxy_core.hpp"
#include "core/config.hpp"
#include "diameter/ihss_client.hpp"

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ims::icscf {

class IcscfService {
public:
    IcscfService(const ims::IcscfConfig& config,
                 boost::asio::io_context& io,
                 std::shared_ptr<ims::diameter::IHssClient> hss);

    auto start() -> VoidResult;
    void stop();

private:
    void onRegister(std::shared_ptr<ims::sip::ServerTransaction> txn,
                    ims::sip::SipMessage& request);
    void onInvite(std::shared_ptr<ims::sip::ServerTransaction> txn,
                  ims::sip::SipMessage& request);
    void onMessage(std::shared_ptr<ims::sip::ServerTransaction> txn,
                   ims::sip::SipMessage& request);
    void onSubscribe(std::shared_ptr<ims::sip::ServerTransaction> txn,
                     ims::sip::SipMessage& request);
    auto localScscfEndpoint() const -> ims::sip::Endpoint;
    auto resolveScscfDestination(const ims::sip::SipMessage& request) const -> ims::sip::Endpoint;
    auto extractTopologyToken(const ims::sip::SipMessage& request) const -> std::optional<std::string>;
    auto createTopologyToken() -> std::string;
    void purgeExpiredTopologyRoutesLocked() const;
    auto topologyRoutesForRequest(const ims::sip::SipMessage& request) const -> std::vector<std::string>;
    void rememberTopologyRoute(const std::string& token,
                               const ims::sip::Endpoint& endpoint,
                               std::vector<std::string> routes = {});
    auto topologyRouteForToken(const std::string& token) const -> std::string;
    void hideHeaderRoutesForExternal(ims::sip::SipMessage& message, const std::string& header_name);
    void restoreTopologyRouteForScscf(ims::sip::SipMessage& request);
    void sanitizeForExternalEgress(ims::sip::SipMessage& message);
    void onAck(ims::sip::SipMessage& request);
    void onInDialogStateful(std::shared_ptr<ims::sip::ServerTransaction> txn,
                            ims::sip::SipMessage& request,
                            const char* method_name);
    void forwardStateful(std::shared_ptr<ims::sip::ServerTransaction> txn,
                         ims::sip::SipMessage& request,
                         const ims::sip::Endpoint& dest,
                         bool add_record_route = false);

    ims::IcscfConfig config_;
    std::unique_ptr<ims::sip::SipStack> sip_stack_;
    std::unique_ptr<ScscfSelector> selector_;
    ims::sip::ProxyCore proxy_;
    std::string proxy_public_addr_;

    struct TopologyRouteEntry {
        ims::sip::Endpoint endpoint;
        std::vector<std::string> routes;
        std::chrono::steady_clock::time_point expires_at;
    };

    mutable std::mutex topology_mutex_;
    mutable std::unordered_map<std::string, TopologyRouteEntry> topology_routes_;
};

} // namespace ims::icscf
