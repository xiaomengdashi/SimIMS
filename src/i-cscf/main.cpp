#include "icscf_service.hpp"
#include "common/config.hpp"
#include "common/logger.hpp"
#include "common/io_context.hpp"
#include "db/mongo_subscriber_repository.hpp"
#include "diameter/mongo_hss_client.hpp"

#include <csignal>
#include <iostream>

namespace {
    std::function<void()> shutdown_handler;
    void signal_handler(int) {
        if (shutdown_handler) shutdown_handler();
    }
}

int main(int argc, char* argv[]) {
    std::string config_path = "config/ims.yaml";
    if (argc > 1) config_path = argv[1];

    auto config_result = ims::load_config(config_path);
    if (!config_result) {
        std::cerr << "Failed to load config: " << config_result.error().message << std::endl;
        return 1;
    }
    auto& config = *config_result;

    ims::init_logger("icscf", config.global.log_level);
    IMS_LOG_INFO("I-CSCF starting...");

    ims::IoContext io_ctx(2);

    auto repository = ims::db::MongoSubscriberRepository::create(config.hss_adapter);
    if (!repository) {
        IMS_LOG_CRITICAL("Failed to create Mongo subscriber repository: {} ({})",
                         repository.error().message,
                         repository.error().detail);
        return 1;
    }

    auto hss = std::make_shared<ims::diameter::MongoHssClient>(config.hss_adapter, **repository);

    ims::icscf::IcscfService service(config.icscf, io_ctx.get(), hss);

    auto start_result = service.start();
    if (!start_result) {
        IMS_LOG_CRITICAL("Failed to start I-CSCF: {}", start_result.error().message);
        return 1;
    }

    shutdown_handler = [&]() {
        service.stop();
        io_ctx.stop();
    };
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    IMS_LOG_INFO("I-CSCF running on {}:{}", config.icscf.listen_addr, config.icscf.listen_port);
    io_ctx.run();

    return 0;
}
