#include "logger.hpp"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <algorithm>
#include <cctype>
#include <memory>
#include <mutex>

namespace ims {

namespace {
    std::shared_ptr<spdlog::logger> g_logger;
    std::once_flag g_init_flag;

    auto parse_log_level(std::string level) -> spdlog::level::level_enum {
        std::transform(level.begin(), level.end(), level.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (level == "warn" || level == "warning") {
            return spdlog::level::warn;
        }
        if (level == "err" || level == "error") {
            return spdlog::level::err;
        }
        return spdlog::level::from_str(level);
    }
}

void init_logger(const std::string& name, const std::string& level) {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] [%t] %v");

    g_logger = std::make_shared<spdlog::logger>(name, console_sink);
    g_logger->set_level(parse_log_level(level));
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
