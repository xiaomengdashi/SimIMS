#pragma once

#include "common/types.hpp"
#include "db/subscriber_repository.hpp"

#include <bson/bson.h>

#include <cstdint>

namespace ims::db {

auto decodeSubscriber(const bson_t& document) -> Result<SubscriberRecord>;
auto nextSqn(uint64_t current, uint64_t step, uint64_t mask) -> uint64_t;

} // namespace ims::db
