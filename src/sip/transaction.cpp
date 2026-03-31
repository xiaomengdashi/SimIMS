#include "transaction.hpp"
#include "common/logger.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

#include <osipparser2/osip_parser.h>

namespace ims::sip {

namespace {

auto response_transaction_keys(const SipMessage& response) -> std::vector<std::string> {
    std::vector<std::string> keys;
    auto method = response.cseqMethod();
    if (method.empty()) {
        return keys;
    }

    for (int i = 0; i < response.viaCount(); ++i) {
        osip_via_t* via = nullptr;
        if (osip_message_get_via(response.raw(), i, &via) != 0 || !via) {
            continue;
        }

        osip_generic_param_t* branch = nullptr;
        osip_via_param_get_byname(via, const_cast<char*>("branch"), &branch);
        if (branch && branch->gvalue) {
            keys.emplace_back(std::string(branch->gvalue) + ":" + method);
        }
    }

    return keys;
}

} // namespace

using namespace std::chrono_literals;

// RFC 3261 timer constants
static constexpr auto kTimerT1 = 500ms;   // RTT estimate
static constexpr auto kTimerT2 = 4000ms;  // Maximum retransmit interval
static constexpr auto kTimerB = 32s;      // INVITE client transaction timeout
static constexpr auto kTimerJ = 32s;      // Non-INVITE server transaction timeout
static constexpr auto kTimerH = 32s;      // INVITE server ACK wait

// ========== ServerTransaction ==========

ServerTransaction::ServerTransaction(SipMessage request, std::shared_ptr<ITransport> transport,
                                     Endpoint source, boost::asio::io_context& io)
    : request_(std::move(request))
    , transport_(std::move(transport))
    , source_(std::move(source))
    , io_(io)
    , timer_j_(io)
    , timer_h_(io) {
    branch_ = request_.viaBranch();
    IMS_LOG_DEBUG("Server transaction created, branch={}", branch_);
}

auto ServerTransaction::request() const -> const SipMessage& {
    return request_;
}

auto ServerTransaction::source() const -> const Endpoint& {
    return source_;
}

auto ServerTransaction::state() const -> TransactionState {
    return state_;
}

auto ServerTransaction::branch() const -> const std::string& {
    return branch_;
}

auto ServerTransaction::sendResponse(SipMessage response) -> VoidResult {
    int code = response.statusCode();

    auto response_copy = response.clone();
    if (response_copy) {
        last_response_ = std::move(*response_copy);
    }

    auto result = transport_->send(response, source_);
    if (!result) {
        IMS_LOG_ERROR("Failed to send response: {}", result.error().message);
        return result;
    }

    if (code >= 100 && code < 200) {
        state_ = TransactionState::kProceeding;
    } else if (code >= 200) {
        bool is_invite = (request_.method() == "INVITE");
        if (is_invite && code >= 200 && code < 300) {
            // 2xx INVITE final responses are handled outside INVITE server transaction.
            state_ = TransactionState::kTerminated;
            if (on_terminated_) on_terminated_();
        } else {
            state_ = TransactionState::kCompleted;
            startTimers(code);
        }
    }

    IMS_LOG_DEBUG("Server txn sent {} response, state={}", code, static_cast<int>(state_));
    return {};
}

auto ServerTransaction::retransmitLastResponse() -> VoidResult {
    if (!last_response_) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipTransactionFailed, "No response available for retransmission"});
    }

    IMS_LOG_DEBUG("Retransmitting last response for branch={}", branch_);
    return transport_->send(*last_response_, source_);
}

void ServerTransaction::acknowledgeAck() {
    if (request_.method() != "INVITE") return;
    if (state_ != TransactionState::kCompleted) return;

    IMS_LOG_DEBUG("ACK matched INVITE server txn, branch={}", branch_);
    timer_h_.cancel();
    state_ = TransactionState::kConfirmed;
    state_ = TransactionState::kTerminated;
    if (on_terminated_) on_terminated_();
}

void ServerTransaction::onTerminated(std::function<void()> cb) {
    on_terminated_ = std::move(cb);
}

void ServerTransaction::startTimers(int final_response_code) {
    bool is_invite = (request_.method() == "INVITE");

    if (is_invite && final_response_code >= 300) {
        // Timer H: wait for ACK
        auto self = shared_from_this();
        timer_h_.expires_after(kTimerH);
        timer_h_.async_wait([self](boost::system::error_code ec) {
            if (ec) return;
            IMS_LOG_DEBUG("Timer H expired, branch={}", self->branch_);
            self->state_ = TransactionState::kTerminated;
            if (self->on_terminated_) self->on_terminated_();
        });
    } else {
        // Timer J: absorb retransmissions
        auto self = shared_from_this();
        timer_j_.expires_after(kTimerJ);
        timer_j_.async_wait([self](boost::system::error_code ec) {
            if (ec) return;
            IMS_LOG_DEBUG("Timer J expired, branch={}", self->branch_);
            self->state_ = TransactionState::kTerminated;
            if (self->on_terminated_) self->on_terminated_();
        });
    }
}

// ========== ClientTransaction ==========

ClientTransaction::ClientTransaction(SipMessage request, std::shared_ptr<ITransport> transport,
                                     Endpoint dest, boost::asio::io_context& io)
    : request_(std::move(request))
    , transport_(std::move(transport))
    , dest_(std::move(dest))
    , io_(io)
    , timer_a_(io)
    , timer_b_(io) {
    branch_ = request_.viaBranch();
    IMS_LOG_DEBUG("Client transaction created, branch={}", branch_);
}

auto ClientTransaction::request() const -> const SipMessage& {
    return request_;
}

auto ClientTransaction::state() const -> TransactionState {
    return state_;
}

auto ClientTransaction::branch() const -> const std::string& {
    return branch_;
}

void ClientTransaction::start() {
    // Send initial request
    auto result = transport_->send(request_, dest_);
    if (!result) {
        IMS_LOG_ERROR("Failed to send request: {}", result.error().message);
        state_ = TransactionState::kTerminated;
        if (on_timeout_) on_timeout_();
        return;
    }

    state_ = TransactionState::kTrying;
    retransmit_count_ = 0;

    auto transport = dest_.transport;
    std::transform(transport.begin(), transport.end(), transport.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (transport == "udp" || transport.empty()) {
        // Start Timer A (retransmission) for UDP.
        timer_a_.expires_after(kTimerT1);
        timer_a_.async_wait([this](boost::system::error_code ec) {
            if (ec) return;
            retransmit();
        });
    }

    // Start Timer B (transaction timeout)
    timer_b_.expires_after(kTimerB);
    timer_b_.async_wait([this](boost::system::error_code ec) {
        if (ec) return;
        if (state_ != TransactionState::kCompleted && state_ != TransactionState::kTerminated) {
            IMS_LOG_WARN("Client transaction timeout, branch={}", branch_);
            state_ = TransactionState::kTerminated;
            timer_a_.cancel();
            if (on_timeout_) on_timeout_();
        }
    });
}

void ClientTransaction::onResponse(ResponseCallback cb) {
    on_response_ = std::move(cb);
}

void ClientTransaction::onTimeout(std::function<void()> cb) {
    on_timeout_ = std::move(cb);
}

bool ClientTransaction::matches(const SipMessage& response) const {
    // RFC 3261 17.1.3: match by Via branch and CSeq method
    return response.viaBranch() == branch_ && response.cseqMethod() == request_.cseqMethod();
}

void ClientTransaction::processResponse(SipMessage response) {
    int code = response.statusCode();
    IMS_LOG_DEBUG("Client txn received {} response, branch={}", code, branch_);

    const bool is_invite = request_.cseqMethod() == "INVITE";
    const bool is_invite_2xx = is_invite && code >= 200 && code < 300;

    if (code >= 100 && code < 200) {
        state_ = TransactionState::kProceeding;
        // Stop retransmission timer for provisional responses
        timer_a_.cancel();
    } else if (code >= 200) {
        state_ = TransactionState::kCompleted;
        timer_a_.cancel();
        if (!is_invite_2xx) {
            timer_b_.cancel();
        }
    }

    if (on_response_) {
        on_response_(response);
    }

    if (code >= 200 && !is_invite_2xx) {
        state_ = TransactionState::kTerminated;
    }
}

void ClientTransaction::retransmit() {
    if (state_ == TransactionState::kCompleted || state_ == TransactionState::kTerminated) {
        return;
    }

    ++retransmit_count_;
    IMS_LOG_DEBUG("Retransmit #{}, branch={}", retransmit_count_, branch_);

    transport_->send(request_, dest_);

    // Double the retransmit interval, capped at T2
    auto interval = kTimerT1 * (1 << retransmit_count_);
    if (interval > kTimerT2) interval = kTimerT2;

    timer_a_.expires_after(interval);
    timer_a_.async_wait([this](boost::system::error_code ec) {
        if (ec) return;
        retransmit();
    });
}

// ========== TransactionLayer ==========

TransactionLayer::TransactionLayer(boost::asio::io_context& io, std::shared_ptr<ITransport> transport)
    : io_(io)
    , transport_(std::move(transport)) {}

void TransactionLayer::setRequestHandler(RequestHandler handler) {
    request_handler_ = std::move(handler);
}

auto TransactionLayer::sendRequest(SipMessage request, const Endpoint& dest,
                                   ClientTransaction::ResponseCallback on_response) -> Result<std::string> {
    auto branch = request.viaBranch();
    if (branch.empty()) {
        return std::unexpected(ErrorInfo{
            ErrorCode::kSipTransactionFailed, "Request has no Via branch"});
    }

    auto method = request.cseqMethod();
    auto key = branch + ":" + method;

    auto txn = std::make_shared<ClientTransaction>(
        std::move(request), transport_, dest, io_);
    txn->onResponse([this, key, cb = std::move(on_response)](const SipMessage& response) {
        if (cb) {
            cb(response);
        }
        const bool is_invite_2xx = response.cseqMethod() == "INVITE" &&
                                   response.statusCode() >= 200 && response.statusCode() < 300;
        if (response.statusCode() >= 200 && !is_invite_2xx) {
            std::lock_guard lock(mutex_);
            client_txns_.erase(key);
        }
    });
    txn->onTimeout([this, key]() {
        std::lock_guard lock(mutex_);
        client_txns_.erase(key);
    });

    {
        std::lock_guard lock(mutex_);
        client_txns_[key] = txn;
    }

    IMS_LOG_INFO("Created client transaction key={} dest={}:{} method={}",
                 key, dest.address, dest.port, method);
    txn->start();
    return key;
}

void TransactionLayer::processMessage(SipMessage msg, Endpoint source) {
    if (msg.isResponse()) {
        // Find matching client transaction
        auto txn = findClientTransaction(msg);
        if (txn) {
            txn->processResponse(std::move(msg));
        } else {
            IMS_LOG_WARN("No matching client transaction for response {}", msg.statusCode());
        }
    } else if (msg.isRequest()) {
        if (msg.method() == "ACK") {
            auto ack_invite_key = msg.viaBranch() + ":INVITE";
            std::shared_ptr<ServerTransaction> invite_txn;
            {
                std::lock_guard lock(mutex_);
                auto it = server_txns_.find(ack_invite_key);
                if (it != server_txns_.end()) {
                    invite_txn = it->second;
                }
            }
            if (invite_txn) {
                invite_txn->acknowledgeAck();
                return;
            }
        }

        auto key = getTransactionKey(msg);

        // Check for retransmission of existing server transaction
        std::shared_ptr<ServerTransaction> existing_txn;
        {
            std::lock_guard lock(mutex_);
            auto it = server_txns_.find(key);
            if (it != server_txns_.end()) {
                existing_txn = it->second;
            }
        }
        if (existing_txn) {
            IMS_LOG_DEBUG("Request retransmission for existing server txn, branch={}",
                existing_txn->branch());
            auto retransmit_result = existing_txn->retransmitLastResponse();
            if (!retransmit_result) {
                IMS_LOG_DEBUG("No cached response to retransmit for key={}", key);
            }
            return;
        }

        // Create new server transaction
        auto txn = std::make_shared<ServerTransaction>(
            std::move(msg), transport_, std::move(source), io_);

        txn->onTerminated([this, key]() {
            std::lock_guard lock(mutex_);
            server_txns_.erase(key);
            IMS_LOG_DEBUG("Server transaction terminated, key={}", key);
        });

        {
            std::lock_guard lock(mutex_);
            server_txns_[key] = txn;
        }

        if (request_handler_) {
            request_handler_(txn);
        }
    }
}

auto TransactionLayer::findClientTransaction(const SipMessage& response)
    -> std::shared_ptr<ClientTransaction> {
    auto keys = response_transaction_keys(response);
    IMS_LOG_INFO("Looking up client transaction for response {} with {} candidate keys",
                 response.statusCode(), keys.size());
    for (const auto& key : keys) {
        IMS_LOG_INFO("Response candidate key={}", key);
    }

    std::lock_guard lock(mutex_);
    for (const auto& key : keys) {
        auto it = client_txns_.find(key);
        if (it != client_txns_.end()) {
            IMS_LOG_INFO("Matched client transaction key={}", key);
            return it->second;
        }
    }
    return nullptr;
}

auto TransactionLayer::getTransactionKey(const SipMessage& msg) const -> std::string {
    return msg.viaBranch() + ":" + (msg.isRequest() ? msg.method() : msg.cseqMethod());
}

} // namespace ims::sip
