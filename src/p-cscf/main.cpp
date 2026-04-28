#include "pcscf_service.hpp"
#include "common/config.hpp"
#include "common/logger.hpp"
#include "common/io_context.hpp"
#include "../diameter/rx_client.hpp"

#include <boost/asio/signal_set.hpp>
#include "../rtp/rtpengine_client_impl.hpp"

#include <csignal>
#include <iostream>

int main(int argc, char* argv[]) {
    std::string config_path = "config/ims.yaml";
    if (argc > 1) config_path = argv[1];

    auto config_result = ims::load_config(config_path);
    if (!config_result) {
        std::cerr << "Failed to load config: " << config_result.error().message << std::endl;
        return 1;
    }
    auto& config = *config_result;

    ims::init_logger("pcscf", config.global.log_level);
    IMS_LOG_INFO("P-CSCF starting...");

    ims::IoContext io_ctx(4);

    auto pcf = std::make_shared<ims::diameter::StubPcfClient>(config.pcscf.pcf);
    auto rtpengine = std::make_shared<ims::media::RtpengineClientImpl>(
        io_ctx.get(), config.media.rtpengine_host, config.media.rtpengine_port);

    // Core entry address for forwarding (default target is S-CSCF(A))
    std::string core_entry_addr = config.pcscf.core_entry.address;
    if (core_entry_addr == "0.0.0.0") core_entry_addr = "127.0.0.1";

    ims::pcscf::PcscfService service(config.pcscf, io_ctx.get(), pcf, rtpengine,
                                     core_entry_addr, config.pcscf.core_entry.port);

    auto start_result = service.start();
    if (!start_result) {
        IMS_LOG_CRITICAL("Failed to start P-CSCF: {}", start_result.error().message);
        return 1;
    }

    boost::asio::signal_set signals(io_ctx.get(), SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code& ec, int) {
        if (ec) {
            return;
        }
        service.stop();
        io_ctx.stop();
    });

    IMS_LOG_INFO("P-CSCF running on {}:{}", config.pcscf.listen_addr, config.pcscf.listen_port);
    io_ctx.run();

    return 0;
}
