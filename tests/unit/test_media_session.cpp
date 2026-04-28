#include "rtp/media_session.hpp"

#include <gtest/gtest.h>

using namespace ims::media;

TEST(MediaSessionManagerTest, SameCallIdDifferentFromTagsDoNotOverwrite) {
    MediaSessionManager manager;

    auto caller_a = manager.createSession(MediaSessionKey{
        .call_id = "same-call",
        .from_tag = "from-a",
    });
    auto caller_b = manager.createSession(MediaSessionKey{
        .call_id = "same-call",
        .from_tag = "from-b",
    });

    EXPECT_EQ(caller_a.call_id, "same-call");
    EXPECT_EQ(caller_a.from_tag, "from-a");
    EXPECT_EQ(caller_b.call_id, "same-call");
    EXPECT_EQ(caller_b.from_tag, "from-b");
    EXPECT_EQ(manager.sessionCount(), 2u);

    auto state_a = manager.getSession(MediaSessionKey{
        .call_id = "same-call",
        .from_tag = "from-a",
    });
    auto state_b = manager.getSession(MediaSessionKey{
        .call_id = "same-call",
        .from_tag = "from-b",
    });

    ASSERT_TRUE(state_a.has_value());
    ASSERT_TRUE(state_b.has_value());
    EXPECT_EQ(state_a->session.from_tag, "from-a");
    EXPECT_EQ(state_b->session.from_tag, "from-b");
    EXPECT_FALSE(manager.getSession("same-call").has_value());
}

TEST(MediaSessionManagerTest, UpdatingToTagMovesOnlyMatchingSession) {
    MediaSessionManager manager;

    manager.createSession(MediaSessionKey{.call_id = "same-call", .from_tag = "from-a"});
    manager.createSession(MediaSessionKey{.call_id = "same-call", .from_tag = "from-b"});
    manager.updateCallerSdp(MediaSessionKey{.call_id = "same-call", .from_tag = "from-a"}, "caller-a-sdp");
    manager.updateCallerSdp(MediaSessionKey{.call_id = "same-call", .from_tag = "from-b"}, "caller-b-sdp");

    manager.updateToTag(MediaSessionKey{.call_id = "same-call", .from_tag = "from-a"}, "to-a");

    EXPECT_FALSE(manager.getSession(MediaSessionKey{
        .call_id = "same-call",
        .from_tag = "from-a",
    }).has_value());

    auto state_a = manager.getSession(MediaSessionKey{
        .call_id = "same-call",
        .from_tag = "from-a",
        .to_tag = "to-a",
    });
    auto state_b = manager.getSession(MediaSessionKey{
        .call_id = "same-call",
        .from_tag = "from-b",
    });

    ASSERT_TRUE(state_a.has_value());
    ASSERT_TRUE(state_b.has_value());
    EXPECT_EQ(state_a->session.to_tag, "to-a");
    EXPECT_EQ(state_a->caller_sdp, "caller-a-sdp");
    EXPECT_EQ(state_b->session.to_tag, "");
    EXPECT_EQ(state_b->caller_sdp, "caller-b-sdp");
    EXPECT_EQ(manager.sessionCount(), 2u);
}

TEST(MediaSessionManagerTest, RemovingOneDialogDoesNotRemoveAnother) {
    MediaSessionManager manager;

    manager.createSession(MediaSessionKey{.call_id = "same-call", .from_tag = "from-a", .to_tag = "to-a"});
    manager.createSession(MediaSessionKey{.call_id = "same-call", .from_tag = "from-b", .to_tag = "to-b"});

    manager.removeSession(MediaSessionKey{.call_id = "same-call", .from_tag = "from-a", .to_tag = "to-a"});

    EXPECT_EQ(manager.sessionCount(), 1u);
    EXPECT_FALSE(manager.getSession(MediaSessionKey{
        .call_id = "same-call",
        .from_tag = "from-a",
        .to_tag = "to-a",
    }).has_value());

    auto remaining = manager.getSession(MediaSessionKey{
        .call_id = "same-call",
        .from_tag = "from-b",
        .to_tag = "to-b",
    });
    ASSERT_TRUE(remaining.has_value());
    EXPECT_EQ(remaining->session.from_tag, "from-b");
    EXPECT_EQ(remaining->session.to_tag, "to-b");
}

TEST(MediaSessionManagerTest, CallIdOnlyCompatibilityWorksForSingleSession) {
    MediaSessionManager manager;

    manager.createSession("single-call", "from-a");
    manager.updateCallerSdp("single-call", "caller-sdp");
    manager.updateToTag("single-call", "to-a");
    manager.updateCalleeSdp("single-call", "callee-sdp");
    manager.setRxSession("single-call", "rx-session");
    manager.setQosActive("single-call", true);

    auto state = manager.getSession(MediaSessionKey{
        .call_id = "single-call",
        .from_tag = "from-a",
        .to_tag = "to-a",
    });
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->caller_sdp, "caller-sdp");
    EXPECT_EQ(state->callee_sdp, "callee-sdp");
    EXPECT_EQ(state->rx_session_id, "rx-session");
    EXPECT_TRUE(state->qos_active);

    manager.removeSession("single-call");
    EXPECT_EQ(manager.sessionCount(), 0u);
}
