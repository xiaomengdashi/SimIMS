#pragma once
#include <boost/asio/io_context.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <thread>
#include <vector>
#include <cstdint>

namespace ims {

class IoContext {
public:
    explicit IoContext(uint32_t thread_count = std::thread::hardware_concurrency());
    ~IoContext();

    IoContext(const IoContext&) = delete;
    IoContext& operator=(const IoContext&) = delete;

    void run();
    void stop();
    boost::asio::io_context& get();

private:
    uint32_t thread_count_;
    boost::asio::io_context io_context_;
    using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
    std::optional<WorkGuard> work_guard_;
    std::vector<std::jthread> workers_;
};

} // namespace ims
