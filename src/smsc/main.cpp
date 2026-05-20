#include "common/config.hpp"
#include "common/io_context.hpp"
#include "common/logger.hpp"
#include "sms/smsc_service.hpp"

#include <boost/asio/signal_set.hpp>

#include <iostream>

int main(int argc, char* argv[]) {
    std::string config_path = "config/ims.yaml";
    if (argc > 1) {
        config_path = argv[1];
    }

    auto config_result = ims::load_config(config_path);
    if (!config_result) {
        std::cerr << "Failed to load config: " << config_result.error().message << std::endl;
        return 1;
    }
    auto& config = *config_result;

    ims::init_logger("smsc", config.global.log_level);
    IMS_LOG_INFO("SMSC starting...");

    ims::IoContext io_ctx(2);
    ims::sms::SmscService service(config.smsc, io_ctx.get());

    auto start_result = service.start();
    if (!start_result) {
        IMS_LOG_CRITICAL("Failed to start SMSC: {}", start_result.error().message);
        return 1;
    }

    boost::asio::signal_set signals(io_ctx.get(), SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code& ec, int) {
        if (ec) {
            return;
        }
        IMS_LOG_INFO("Shutting down SMSC...");
        service.stop();
        io_ctx.stop();
    });

    IMS_LOG_INFO("SMSC running on {}:{} psi={} scscf={}:{}",
                 config.smsc.listen_addr,
                 config.smsc.listen_port,
                 config.smsc.psi,
                 config.smsc.scscf.address,
                 config.smsc.scscf.port);
    io_ctx.run();

    return 0;
}
