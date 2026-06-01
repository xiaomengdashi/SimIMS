#pragma once

#include "common/config.hpp"
#include "db/subscriber_repository.hpp"
#include "diameter/ihss_client.hpp"

namespace ims::diameter {

class MongoHssClient : public IHssClient {
public:
    MongoHssClient(const HssAdapterConfig& config, db::ISubscriberRepository& repository);

    auto userAuthorization(const UarParams& params) -> Result<UaaResult> override;
    auto multimediaAuth(const MarParams& params) -> Result<MaaResult> override;
    auto serverAssignment(const SarParams& params) -> Result<SaaResult> override;
    auto locationInfo(const LirParams& params) -> Result<LiaResult> override;

private:
    auto resolve_assigned_scscf(const db::SubscriberRecord& subscriber) const -> std::string;

    const HssAdapterConfig& config_;
    db::ISubscriberRepository& repository_;
};

} // namespace ims::diameter
