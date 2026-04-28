#include "../p-cscf/pcscf_service.hpp"
#include "../i-cscf/icscf_service.hpp"
#include "../s-cscf/scscf_service.hpp"
#include "common/config.hpp"
#include "common/logger.hpp"
#include "common/io_context.hpp"
#include "../diameter/mongo_hss_client.hpp"
#include "../diameter/rx_client.hpp"
#include "../db/mongo_subscriber_repository.hpp"
#include "../s-cscf/mongo_digest_credential_store.hpp"
#include "../rtp/rtpengine_client_impl.hpp"
#include "../sip/memory_store.hpp"

#include <boost/asio/signal_set.hpp>

#include <csignal>
#include <iostream>
#include <memory>

int main(int argc, char* argv[]) {
    std::string config_path = "config/ims.yaml";
    if (argc > 1) config_path = argv[1];

    auto config_result = ims::load_config(config_path);
    if (!config_result) {
        std::cerr << "Failed to load config: " << config_result.error().message << std::endl;
        return 1;
    }
    auto& config = *config_result;

    ims::init_logger("ims-allinone", config.global.log_level);
    IMS_LOG_INFO("IMS All-in-One starting...");
    IMS_LOG_INFO("  P-CSCF port: {}", config.pcscf.listen_port);
    IMS_LOG_INFO("  I-CSCF port: {}", config.icscf.listen_port);
    IMS_LOG_INFO("  S-CSCF port: {}", config.scscf.listen_port);

    ims::IoContext io_ctx(4);

    // Shared dependencies
    auto repository = ims::db::MongoSubscriberRepository::create(config.hss_adapter);
    if (!repository) {
        IMS_LOG_CRITICAL("Failed to create Mongo subscriber repository: {} ({})",
                         repository.error().message,
                         repository.error().detail);
        return 1;
    }

    auto hss = std::make_shared<ims::diameter::MongoHssClient>(config.hss_adapter, **repository);
    auto pcf = std::make_shared<ims::diameter::StubPcfClient>(config.pcscf.pcf);
    auto store = std::make_shared<ims::registration::MemoryRegistrationStore>();

    auto digest_store = ims::scscf::make_mongo_digest_credential_store(config.hss_adapter);
    if (!digest_store) {
        IMS_LOG_CRITICAL("Failed to create Mongo digest credential store: {} ({})",
                         digest_store.error().message,
                         digest_store.error().detail);
        return 1;
    }

    auto rtpengine = std::make_shared<ims::media::RtpengineClientImpl>(
        io_ctx.get(), config.media.rtpengine_host, config.media.rtpengine_port);
    IMS_LOG_INFO("rtpengine client configured for {}:{}",
                 config.media.rtpengine_host, config.media.rtpengine_port);

    // Core entry address for P-CSCF forwarding
    std::string core_entry_addr = config.pcscf.core_entry.address;
    if (core_entry_addr == "0.0.0.0") {
        core_entry_addr = "127.0.0.1";
    }

    // Create services
    ims::scscf::ScscfService scscf(config.scscf, io_ctx.get(), hss, store, *digest_store, nullptr);
    ims::icscf::IcscfService icscf(config.icscf, io_ctx.get(), hss);
    ims::pcscf::PcscfService pcscf(config.pcscf, io_ctx.get(), pcf, rtpengine,
                                    core_entry_addr, config.pcscf.core_entry.port);

    // Start all services
    auto r1 = scscf.start();
    if (!r1) {
        IMS_LOG_CRITICAL("Failed to start S-CSCF: {}", r1.error().message);
        return 1;
    }

    auto r2 = icscf.start();
    if (!r2) {
        IMS_LOG_CRITICAL("Failed to start I-CSCF: {}", r2.error().message);
        return 1;
    }

    auto r3 = pcscf.start();
    if (!r3) {
        IMS_LOG_CRITICAL("Failed to start P-CSCF: {}", r3.error().message);
        return 1;
    }

    boost::asio::signal_set signals(io_ctx.get(), SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code& ec, int) {
        if (ec) {
            return;
        }
        IMS_LOG_INFO("Shutting down IMS All-in-One...");
        pcscf.stop();
        icscf.stop();
        scscf.stop();
        io_ctx.stop();
    });

    IMS_LOG_INFO("IMS All-in-One is running. Press Ctrl+C to stop.");
    io_ctx.run();

    IMS_LOG_INFO("IMS All-in-One stopped.");
    return 0;
}
