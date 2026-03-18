#include "ims/sip/transaction.hpp"
#include "ims/common/logger.hpp"

namespace ims::sip {

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

auto ServerTransaction::state() const -> TransactionState {
    return state_;
}

auto ServerTransaction::branch() const -> const std::string& {
    return branch_;
}

auto ServerTransaction::sendResponse(SipMessage response) -> VoidResult {
    int code = response.statusCode();

    auto result = transport_->send(response, source_);
    if (!result) {
        IMS_LOG_ERROR("Failed to send response: {}", result.error().message);
        return result;
    }

    if (code >= 100 && code < 200) {
        state_ = TransactionState::kProceeding;
    } else if (code >= 200) {
        state_ = TransactionState::kCompleted;
        startTimers();
    }

    IMS_LOG_DEBUG("Server txn sent {} response, state={}", code, static_cast<int>(state_));
    return {};
}

void ServerTransaction::onTerminated(std::function<void()> cb) {
    on_terminated_ = std::move(cb);
}

void ServerTransaction::startTimers() {
    bool is_invite = (request_.method() == "INVITE");

    if (is_invite) {
        // Timer H: wait for ACK
        timer_h_.expires_after(kTimerH);
        timer_h_.async_wait([this](boost::system::error_code ec) {
            if (ec) return;
            IMS_LOG_DEBUG("Timer H expired, branch={}", branch_);
            state_ = TransactionState::kTerminated;
            if (on_terminated_) on_terminated_();
        });
    } else {
        // Timer J: absorb retransmissions
        timer_j_.expires_after(kTimerJ);
        timer_j_.async_wait([this](boost::system::error_code ec) {
            if (ec) return;
            IMS_LOG_DEBUG("Timer J expired, branch={}", branch_);
            state_ = TransactionState::kTerminated;
            if (on_terminated_) on_terminated_();
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

    // Start Timer A (retransmission) for UDP
    timer_a_.expires_after(kTimerT1);
    timer_a_.async_wait([this](boost::system::error_code ec) {
        if (ec) return;
        retransmit();
    });

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

    if (code >= 100 && code < 200) {
        state_ = TransactionState::kProceeding;
        // Stop retransmission timer for provisional responses
        timer_a_.cancel();
    } else if (code >= 200) {
        state_ = TransactionState::kCompleted;
        timer_a_.cancel();
        timer_b_.cancel();
    }

    if (on_response_) {
        on_response_(response);
    }

    if (code >= 200) {
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

    auto key = branch + ":" + request.cseqMethod();

    auto txn = std::make_shared<ClientTransaction>(
        std::move(request), transport_, dest, io_);
    txn->onResponse(std::move(on_response));
    txn->onTimeout([this, key]() {
        std::lock_guard lock(mutex_);
        client_txns_.erase(key);
    });

    {
        std::lock_guard lock(mutex_);
        client_txns_[key] = txn;
    }

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
        auto key = getTransactionKey(msg);

        // Check for retransmission of existing server transaction
        {
            std::lock_guard lock(mutex_);
            auto it = server_txns_.find(key);
            if (it != server_txns_.end()) {
                IMS_LOG_DEBUG("Request retransmission for existing server txn, branch={}",
                    it->second->branch());
                return;
            }
        }

        // Create new server transaction
        auto txn = std::make_shared<ServerTransaction>(
            std::move(msg), transport_, std::move(source), io_);

        auto branch = txn->branch();

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
    auto key = response.viaBranch() + ":" + response.cseqMethod();
    std::lock_guard lock(mutex_);
    auto it = client_txns_.find(key);
    if (it != client_txns_.end()) {
        return it->second;
    }
    return nullptr;
}

auto TransactionLayer::getTransactionKey(const SipMessage& msg) const -> std::string {
    return msg.viaBranch() + ":" + (msg.isRequest() ? msg.method() : msg.cseqMethod());
}

} // namespace ims::sip
