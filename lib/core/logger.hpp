#pragma once
#include <string>
#include <spdlog/spdlog.h>
#include <spdlog/logger.h>

namespace ims {

void init_logger(const std::string& name, const std::string& level);
spdlog::logger& get_logger();

} // namespace ims

#define IMS_LOG_TRACE(...)    ims::get_logger().trace(__VA_ARGS__)
#define IMS_LOG_DEBUG(...)    ims::get_logger().debug(__VA_ARGS__)
#define IMS_LOG_INFO(...)     ims::get_logger().info(__VA_ARGS__)
#define IMS_LOG_WARN(...)     ims::get_logger().warn(__VA_ARGS__)
#define IMS_LOG_ERROR(...)    ims::get_logger().error(__VA_ARGS__)
#define IMS_LOG_CRITICAL(...) ims::get_logger().critical(__VA_ARGS__)
