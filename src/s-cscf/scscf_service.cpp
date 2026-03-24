#include "scscf_service.hpp"
#include "common/logger.hpp"

#include <format>
#include <optional>
#include <vector>
#include <algorithm>
#include <cctype>

namespace ims::scscf {

namespace {

auto extractUriFromNameAddr(const std::string& value) -> std::string {
    auto start = value.find('<');
    auto end = value.find('>');
    if (start != std::string::npos && end != std::string::npos && end > start) {
        return value.substr(start + 1, end - start - 1);
    }

    auto semi = value.find(';');
    if (semi != std::string::npos) {
        return value.substr(0, semi);
    }
    return value;
}

auto parseEndpointFromSipUri(const std::string& sip_uri) -> std::optional<ims::sip::Endpoint> {
    std::string uri = sip_uri;
    auto normalized = uri;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    std::string transport = "udp";
    auto transport_pos = normalized.find(";transport=");
    if (transport_pos != std::string::npos) {
        auto value_start = transport_pos + std::string(";transport=").size();
        auto value_end = normalized.find(';', value_start);
        transport = normalized.substr(value_start, value_end - value_start);
    }

    if (!uri.empty() && uri.front() == '<') {
        uri.erase(0, 1);
    }
    if (!uri.empty() && uri.back() == '>') {
        uri.pop_back();
    }
    auto sip_pos = uri.find("sip:");
    if (sip_pos != std::string::npos) {
        uri = uri.substr(sip_pos + 4);
    }

    auto at_pos = uri.rfind('@');
    if (at_pos != std::string::npos) {
        uri = uri.substr(at_pos + 1);
    }

    auto param_pos = uri.find(';');
    if (param_pos != std::string::npos) {
        uri = uri.substr(0, param_pos);
    }
    auto angle_pos = uri.find('>');
    if (angle_pos != std::string::npos) {
        uri = uri.substr(0, angle_pos);
    }

    std::string host;
    ims::Port port = 5060;

    if (!uri.empty() && uri.front() == '[') {
        auto close = uri.find(']');
        if (close == std::string::npos) return std::nullopt;
        host = uri.substr(1, close - 1);
        if (close + 1 < uri.size() && uri[close + 1] == ':') {
            try {
                port = static_cast<ims::Port>(std::stoi(uri.substr(close + 2)));
            } catch (...) {
                return std::nullopt;
            }
        }
    } else {
        auto colon = uri.find(':');
        if (colon != std::string::npos) {
            host = uri.substr(0, colon);
            try {
                port = static_cast<ims::Port>(std::stoi(uri.substr(colon + 1)));
            } catch (...) {
                return std::nullopt;
            }
        } else {
            host = uri;
        }
    }

    if (host.empty()) return std::nullopt;
    return ims::sip::Endpoint{
        .address = host,
        .port = port,
        .transport = transport
    };
}

} // namespace

ScscfService::ScscfService(const ims::ScscfConfig& config,
                           boost::asio::io_context& io,
                           std::shared_ptr<ims::diameter::IHssClient> hss,
                           std::shared_ptr<ims::registration::IRegistrationStore> store)
    : config_(config)
    , sip_stack_(std::make_unique<ims::sip::SipStack>(
          io, config.listen_addr, config.listen_port))
    , hss_(std::move(hss))
    , store_(std::move(store))
{
    registrar_ = std::make_unique<Registrar>(store_, hss_, config.domain);
    session_router_ = std::make_unique<SessionRouter>(store_, *sip_stack_);
}

auto ScscfService::start() -> VoidResult {
    IMS_LOG_INFO("Starting S-CSCF on {}:{} domain={}",
                 config_.listen_addr, config_.listen_port, config_.domain);

    sip_stack_->onRequest("REGISTER", [this](auto txn, auto& req) {
        onRegister(txn, req);
    });
    sip_stack_->onRequest("INVITE", [this](auto txn, auto& req) {
        onInvite(txn, req);
    });
    sip_stack_->onRequest("BYE", [this](auto txn, auto& req) {
        onBye(txn, req);
    });
    sip_stack_->onRequest("CANCEL", [this](auto txn, auto& req) {
        onCancel(txn, req);
    });
    sip_stack_->onRequest("SUBSCRIBE", [this](auto txn, auto& req) {
        onSubscribe(txn, req);
    });

    return sip_stack_->start();
}

void ScscfService::stop() {
    IMS_LOG_INFO("Stopping S-CSCF");
    sip_stack_->stop();
}

void ScscfService::onRegister(std::shared_ptr<ims::sip::ServerTransaction> txn,
                               ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("S-CSCF received REGISTER");
    registrar_->handleRegister(request, txn);
}

void ScscfService::onInvite(std::shared_ptr<ims::sip::ServerTransaction> txn,
                             ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("S-CSCF received INVITE");
    session_router_->handleInvite(request, txn);
}

void ScscfService::onBye(std::shared_ptr<ims::sip::ServerTransaction> txn,
                          ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("S-CSCF received BYE");
    session_router_->handleBye(request, txn);
}

void ScscfService::onCancel(std::shared_ptr<ims::sip::ServerTransaction> txn,
                             ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("S-CSCF received CANCEL");
    session_router_->handleCancel(request, txn);
}

void ScscfService::onSubscribe(std::shared_ptr<ims::sip::ServerTransaction> txn,
                                ims::sip::SipMessage& request)
{
    IMS_LOG_DEBUG("S-CSCF received SUBSCRIBE event={}",
                  request.getHeader("Event").value_or("<none>"));

    auto event = request.getHeader("Event");
    if (!event || *event != "reg") {
        auto resp = ims::sip::createResponse(request, 489, "Bad Event");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    auto impu = request.requestUri();
    if (impu.find("sip:") == std::string::npos) {
        impu = "sip:" + impu;
    }

    auto lookup = store_->lookup(impu);
    if (!lookup) {
        auto resp = ims::sip::createResponse(request, 404, "Not Found");
        if (resp) txn->sendResponse(std::move(*resp));
        return;
    }

    auto resp = ims::sip::createResponse(request, 200, "OK");
    if (!resp) return;

    auto expires = request.getHeader("Expires").value_or("3600");
    resp->addHeader("Expires", expires);
    resp->setContact(std::format("<sip:{}:{}>", config_.listen_addr, config_.listen_port));
    for (const auto& rr : request.getHeaders("Record-Route")) {
        resp->addRecordRoute(rr);
    }

    auto to_tag = resp->toTag();
    txn->sendResponse(std::move(*resp));

    sendInitialNotify(request, to_tag);
}

void ScscfService::sendInitialNotify(const ims::sip::SipMessage& subscribe,
                                     const std::string& to_tag)
{
    auto contact = subscribe.contact();
    if (!contact) {
        IMS_LOG_WARN("SUBSCRIBE missing Contact header, cannot send initial NOTIFY");
        return;
    }

    auto target_uri = extractUriFromNameAddr(*contact);
    auto record_routes = subscribe.getHeaders("Record-Route");
    std::vector<std::string> route_set(record_routes.rbegin(), record_routes.rend());

    auto destination_uri = target_uri;
    if (!route_set.empty()) {
        destination_uri = extractUriFromNameAddr(route_set.front());
    }

    auto endpoint = parseEndpointFromSipUri(destination_uri);
    if (!endpoint) {
        IMS_LOG_WARN("Failed to parse NOTIFY destination URI: {}", destination_uri);
        return;
    }

    auto notify = ims::sip::createRequest("NOTIFY", target_uri);
    if (!notify) {
        IMS_LOG_ERROR("Failed to create NOTIFY request");
        return;
    }

    auto from = subscribe.toHeader();
    if (from.find(";tag=") == std::string::npos && !to_tag.empty()) {
        from += ";tag=" + to_tag;
    }

    auto event = subscribe.getHeader("Event").value_or("reg");
    auto expires = subscribe.getHeader("Expires").value_or("3600");

    auto reg_aor = extractUriFromNameAddr(subscribe.toHeader());
    auto body = std::format(
        "<?xml version=\"1.0\"?>\r\n"
        "<reginfo xmlns=\"urn:ietf:params:xml:ns:reginfo\" version=\"0\" state=\"full\">\r\n"
        "  <registration aor=\"{}\" id=\"1\" state=\"active\">\r\n"
        "    <contact id=\"1\" state=\"active\" event=\"registered\" />\r\n"
        "  </registration>\r\n"
        "</reginfo>\r\n",
        reg_aor);

    notify->setFromHeader(from);
    notify->setToHeader(subscribe.fromHeader());
    notify->setCallId(subscribe.callId());
    notify->setCSeq(subscribe.cseq() + 1, "NOTIFY");
    notify->addHeader("Max-Forwards", "70");
    notify->addVia(std::format("SIP/2.0/UDP {}:{};branch={};rport",
                               config_.listen_addr, config_.listen_port,
                               ims::sip::generateBranch()));
    notify->addHeader("Event", event);
    notify->addHeader("Subscription-State", std::format("active;expires={}", expires));
    for (const auto& route : route_set) {
        notify->addHeader("Route", route);
    }
    notify->setContact(std::format("<sip:{}:{}>", config_.listen_addr, config_.listen_port));
    notify->setBody(body, "application/reginfo+xml");

    auto send_result = sip_stack_->sendRequest(std::move(*notify), *endpoint,
        [](const ims::sip::SipMessage& response) {
            IMS_LOG_DEBUG("Initial NOTIFY got {} response", response.statusCode());
        });
    if (!send_result) {
        IMS_LOG_ERROR("Failed to send initial NOTIFY: {}", send_result.error().message);
    }
}

} // namespace ims::scscf
