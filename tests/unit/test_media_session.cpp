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

TEST(MediaSessionManagerTest, TerminatingSessionRejectsLateInviteResponse) {
    MediaSessionManager manager;

    manager.createSession(MediaSessionKey{.call_id = "call-1", .from_tag = "from-a"});
    auto termination = manager.markTerminating(MediaSessionKey{.call_id = "call-1", .from_tag = "from-a"});
    ASSERT_TRUE(termination.has_value());

    auto update = manager.beginInviteResponse(MediaSessionKey{
        .call_id = "call-1",
        .from_tag = "from-a",
        .to_tag = "to-a",
    });
    EXPECT_FALSE(update.has_value());

    manager.commitInviteResponse(MediaSessionKey{.call_id = "call-1", .from_tag = "from-a", .to_tag = "to-a"},
                                  "callee-sdp", true);
    EXPECT_FALSE(manager.getSession(MediaSessionKey{
        .call_id = "call-1",
        .from_tag = "from-a",
        .to_tag = "to-a",
    }).has_value());
}

TEST(MediaSessionManagerTest, DuplicateTerminationIsIdempotent) {
    MediaSessionManager manager;

    manager.createSession(MediaSessionKey{.call_id = "call-1", .from_tag = "from-a"});
    auto first = manager.markTerminating(MediaSessionKey{.call_id = "call-1", .from_tag = "from-a"});
    auto second = manager.markTerminating(MediaSessionKey{.call_id = "call-1", .from_tag = "from-a"});

    ASSERT_TRUE(first.has_value());
    EXPECT_FALSE(second.has_value());
    manager.completeTermination(first->key);
    EXPECT_EQ(manager.sessionCount(), 0u);
}

TEST(MediaSessionManagerTest, ReverseDialogTerminationFindsEstablishedSession) {
    MediaSessionManager manager;

    manager.createSession(MediaSessionKey{.call_id = "call-1", .from_tag = "caller"});
    auto update = manager.beginInviteResponse(MediaSessionKey{
        .call_id = "call-1",
        .from_tag = "caller",
        .to_tag = "callee",
    });
    ASSERT_TRUE(update.has_value());
    manager.commitInviteResponse(update->key, "callee-sdp", true);
    manager.setRxSession(update->key, "rx-session-1");

    auto termination = manager.markTerminating(MediaSessionKey{
        .call_id = "call-1",
        .from_tag = "callee",
        .to_tag = "caller",
    });

    ASSERT_TRUE(termination.has_value());
    EXPECT_EQ(termination->key.from_tag, "caller");
    EXPECT_EQ(termination->key.to_tag, "callee");
    EXPECT_EQ(termination->rx_session_id, "rx-session-1");
}

TEST(MediaSessionManagerTest, ReverseEarlyTerminationFindsInitialSession) {
    MediaSessionManager manager;

    manager.createSession(MediaSessionKey{.call_id = "call-1", .from_tag = "caller"});
    auto termination = manager.markTerminating(MediaSessionKey{
        .call_id = "call-1",
        .from_tag = "callee",
        .to_tag = "caller",
    });

    ASSERT_TRUE(termination.has_value());
    EXPECT_EQ(termination->key.from_tag, "caller");
    EXPECT_TRUE(termination->key.to_tag.empty());
}

TEST(MediaSessionManagerTest, BeginInviteResponseCreatesBranchAndKeepsInitialSession) {
    MediaSessionManager manager;

    manager.createSession(MediaSessionKey{.call_id = "call-1", .from_tag = "from-a"});
    auto update = manager.beginInviteResponse(MediaSessionKey{
        .call_id = "call-1",
        .from_tag = "from-a",
        .to_tag = "to-a",
    });
    ASSERT_TRUE(update.has_value());
    EXPECT_EQ(update->session.to_tag, "to-a");
    manager.commitInviteResponse(update->key, "callee-sdp", false);

    EXPECT_TRUE(manager.getSession(MediaSessionKey{.call_id = "call-1", .from_tag = "from-a"}).has_value());
    auto state = manager.getSession(MediaSessionKey{.call_id = "call-1", .from_tag = "from-a", .to_tag = "to-a"});
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->callee_sdp, "callee-sdp");
    EXPECT_EQ(state->lifecycle, MediaSessionLifecycle::kEarly);
    EXPECT_EQ(manager.sessionCount(), 2u);
}

TEST(MediaSessionManagerTest, SecondForkedEarlyDialogCreatesIndependentSession) {
    MediaSessionManager manager;

    manager.createSession(MediaSessionKey{.call_id = "fork-call", .from_tag = "caller"});
    auto first = manager.beginInviteResponse(MediaSessionKey{
        .call_id = "fork-call",
        .from_tag = "caller",
        .to_tag = "early-a",
    });
    auto second = manager.beginInviteResponse(MediaSessionKey{
        .call_id = "fork-call",
        .from_tag = "caller",
        .to_tag = "early-b",
    });

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_TRUE(manager.getSession(MediaSessionKey{.call_id = "fork-call", .from_tag = "caller"}).has_value());
    EXPECT_TRUE(manager.getSession(first->key).has_value());
    EXPECT_TRUE(manager.getSession(second->key).has_value());
    EXPECT_EQ(manager.sessionCount(), 3u);
}

TEST(MediaSessionManagerTest, FinalSuccessKeepsWinningDialogAndRemovesOtherEarlyDialogs) {
    MediaSessionManager manager;

    manager.createSession(MediaSessionKey{.call_id = "fork-success", .from_tag = "caller"});
    auto winning = manager.beginInviteResponse(MediaSessionKey{
        .call_id = "fork-success",
        .from_tag = "caller",
        .to_tag = "winner",
    });
    auto losing = manager.beginInviteResponse(MediaSessionKey{
        .call_id = "fork-success",
        .from_tag = "caller",
        .to_tag = "loser",
    });
    ASSERT_TRUE(winning.has_value());
    ASSERT_TRUE(losing.has_value());
    manager.commitInviteResponse(winning->key, "winner-sdp", true);

    auto plans = manager.markInviteFinalSuccess(winning->key);
    EXPECT_EQ(plans.size(), 2u);
    for (const auto& plan : plans) {
        manager.completeTermination(plan.key);
    }

    auto winner = manager.getSession(winning->key);
    ASSERT_TRUE(winner.has_value());
    EXPECT_EQ(winner->lifecycle, MediaSessionLifecycle::kEstablished);
    EXPECT_FALSE(manager.getSession(MediaSessionKey{.call_id = "fork-success", .from_tag = "caller"}).has_value());
    EXPECT_FALSE(manager.getSession(losing->key).has_value());
}

TEST(MediaSessionManagerTest, FinalFailureRemovesInitialAndEarlyDialogs) {
    MediaSessionManager manager;

    manager.createSession(MediaSessionKey{.call_id = "fork-fail", .from_tag = "caller"});
    auto early = manager.beginInviteResponse(MediaSessionKey{
        .call_id = "fork-fail",
        .from_tag = "caller",
        .to_tag = "early",
    });
    ASSERT_TRUE(early.has_value());

    auto plans = manager.markInviteFinalFailure(MediaSessionKey{
        .call_id = "fork-fail",
        .from_tag = "caller",
        .to_tag = "early",
    });
    EXPECT_EQ(plans.size(), 2u);
    for (const auto& plan : plans) {
        manager.completeTermination(plan.key);
    }

    EXPECT_EQ(manager.sessionCount(), 0u);
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
