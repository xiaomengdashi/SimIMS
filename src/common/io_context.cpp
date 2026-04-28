#include "io_context.hpp"
#include <algorithm>
#include <utility>

namespace ims {

IoContext::IoContext(uint32_t thread_count)
    : thread_count_(std::max(1u, thread_count))
    , io_context_()
    , work_guard_(io_context_.get_executor()) {
}

IoContext::~IoContext() {
    stop();
}

void IoContext::run() {
    {
        std::lock_guard lock(mutex_);
        running_ = true;
    }

    workers_.reserve(thread_count_);
    for (uint32_t i = 0; i < thread_count_; ++i) {
        workers_.emplace_back([this](std::stop_token) {
            io_context_.run();
        });
    }

    // Block until stop() is called
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return !running_; });
}

void IoContext::stop() {
    std::vector<std::jthread> workers;
    {
        std::lock_guard lock(mutex_);
        running_ = false;
        workers = std::move(workers_);
    }
    cv_.notify_all();

    work_guard_.reset();
    io_context_.stop();

    const auto current_id = std::this_thread::get_id();
    for (auto& worker : workers) {
        if (worker.get_id() == current_id && worker.joinable()) {
            worker.detach();
        }
    }
}

boost::asio::io_context& IoContext::get() {
    return io_context_;
}

} // namespace ims
