#include "scscf_service.hpp"
#include "common/config.hpp"
#include "common/logger.hpp"
#include "common/io_context.hpp"
#include "diameter/mongo_hss_client.hpp"

#include <boost/asio/signal_set.hpp>
#include "db/mongo_subscriber_repository.hpp"
#include "s-cscf/mongo_digest_credential_store.hpp"
#include "../sip/memory_store.hpp"

#include <csignal>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

int main(int argc, char* argv[]) {
    std::string config_path = "config/ims.yaml";
    if (argc > 1) config_path = argv[1];

    auto config_result = ims::load_config(config_path);
    if (!config_result) {
        std::cerr << "Failed to load config: " << config_result.error().message << std::endl;
        return 1;
    }
    auto& config = *config_result;

    ims::init_logger("scscf", config.global.log_level);
    IMS_LOG_INFO("S-CSCF starting...");

    // Here we configure the io_context pool size.
    // For handling high concurrency like 1000 simultaneous REGISTERS, 
    // set this to a higher number based on CPU cores (e.g., 8, 16).
    // Since this is a test/demo environment, we use std::thread::hardware_concurrency()
    unsigned int thread_count = std::thread::hardware_concurrency();
    if (thread_count == 0) thread_count = 8;
    ims::IoContext io_ctx(thread_count);

    auto repository = ims::db::MongoSubscriberRepository::create(config.hss_adapter);
    if (!repository) {
        IMS_LOG_CRITICAL("Failed to create Mongo subscriber repository: {} ({})",
                         repository.error().message,
                         repository.error().detail);
        return 1;
    }

    auto hss = std::make_shared<ims::diameter::MongoHssClient>(config.hss_adapter, **repository);
    auto store = std::make_shared<ims::registration::MemoryRegistrationStore>();

    auto digest_store = ims::scscf::make_mongo_digest_credential_store(config.hss_adapter);
    if (!digest_store) {
        IMS_LOG_CRITICAL("Failed to create Mongo digest credential store: {} ({})",
                         digest_store.error().message,
                         digest_store.error().detail);
        return 1;
    }

    ims::scscf::ScscfService service(config.scscf, io_ctx.get(), hss, store, *digest_store, nullptr);

    auto start_result = service.start();
    if (!start_result) {
        IMS_LOG_CRITICAL("Failed to start S-CSCF: {}", start_result.error().message);
        return 1;
    }

    boost::asio::signal_set signals(io_ctx.get(), SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code& ec, int) {
        if (ec) {
            return;
        }
        IMS_LOG_INFO("Shutting down S-CSCF...");
        service.stop();
        io_ctx.stop();
    });

    IMS_LOG_INFO("S-CSCF running on {}:{}", config.scscf.listen_addr, config.scscf.listen_port);
    io_ctx.run();

    return 0;
}
