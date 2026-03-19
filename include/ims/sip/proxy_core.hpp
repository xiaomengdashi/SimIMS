#pragma once

#include "ims/sip/message.hpp"
#include "ims/sip/transport.hpp"
#include "ims/common/types.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace ims::sip {

class ProxyCore {
public:
    ProxyCore(const std::string& local_address, Port local_port);

    auto prepareRequestForForward(SipMessage& msg,
                                  std::string_view transport = "udp") -> VoidResult;
    auto forwardRequest(SipMessage& msg, const Endpoint& dest,
                        std::shared_ptr<ITransport> transport) -> VoidResult;
    auto forwardResponse(SipMessage& msg,
                         std::shared_ptr<ITransport> transport) -> VoidResult;

    void addRecordRoute(SipMessage& msg);
    bool processRouteHeaders(SipMessage& msg);
    bool isLoopDetected(const SipMessage& msg) const;

private:
    std::string local_address_;
    Port local_port_;
};

} // namespace ims::sip
