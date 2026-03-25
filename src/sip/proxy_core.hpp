#pragma once

#include "message.hpp"
#include "transaction.hpp"
#include "transport.hpp"
#include "common/types.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace ims::sip {

class SipStack;

struct ForwardOptions {
    bool add_record_route = false;
    bool process_route_headers = true;
    bool detect_loop = true;
};

struct CancelForwardContext {
    std::string transport = "udp";
    std::string invite_branch;
    std::string local_address;
    Port local_port = 5060;
    bool decrement_max_forwards = true;
    bool process_route_headers = true;
};

class ProxyCore {
public:
    ProxyCore(const std::string& local_address, Port local_port);

    auto prepareRequestForForward(SipMessage& msg,
                                  std::string_view transport = "udp") -> VoidResult;
    auto forwardRequest(SipMessage& msg, const Endpoint& dest,
                        std::shared_ptr<ITransport> transport) -> VoidResult;
    auto forwardResponse(SipMessage& msg,
                         std::shared_ptr<ITransport> transport) -> VoidResult;
    auto forwardResponseUpstream(const SipMessage& response,
                                 const std::shared_ptr<ServerTransaction>& txn) -> VoidResult;
    auto forwardStateful(SipMessage& request,
                         const Endpoint& dest,
                         const std::shared_ptr<ServerTransaction>& upstream_txn,
                         SipStack& sip_stack,
                         const ForwardOptions& options = {}) -> VoidResult;

    auto buildPathHeader() const -> std::string;
    auto buildForwardedCancel(const SipMessage& incoming_cancel,
                              const CancelForwardContext& context) -> Result<SipMessage>;
    void addPathHeader(SipMessage& msg);
    void addRecordRoute(SipMessage& msg);
    bool processRouteHeaders(SipMessage& msg);
    bool isLoopDetected(const SipMessage& msg) const;

private:
    std::string local_address_;
    Port local_port_;
};

} // namespace ims::sip
