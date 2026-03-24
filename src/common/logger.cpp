#include "logger.hpp"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>
#include <mutex>

namespace ims {

namespace {
    std::shared_ptr<spdlog::logger> g_logger;
    std::once_flag g_init_flag;
}

void init_logger(const std::string& name, const std::string& level) {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] [%t] %v");

    g_logger = std::make_shared<spdlog::logger>(name, console_sink);
    g_logger->set_level(spdlog::level::from_str(level));
    g_logger->flush_on(spdlog::level::warn);

    spdlog::set_default_logger(g_logger);
}

spdlog::logger& get_logger() {
    std::call_once(g_init_flag, []() {
        if (!g_logger) {
            init_logger("ims", "info");
        }
    });
    return *g_logger;
}

} // namespace ims
