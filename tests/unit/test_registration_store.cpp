#include "sip/memory_store.hpp"

#include <gtest/gtest.h>

using namespace ims::registration;

namespace {

auto makeContact(std::string uri,
                 std::string instance_id,
                 std::string reg_id,
                 std::string call_id,
                 uint32_t cseq,
                 std::chrono::seconds ttl = std::chrono::seconds(600)) -> ContactBinding {
    return ContactBinding{
        .contact_uri = std::move(uri),
        .instance_id = std::move(instance_id),
        .reg_id = std::move(reg_id),
        .path = "<sip:pcscf.ims.example.com;lr>",
        .expires = std::chrono::steady_clock::now() + ttl,
        .user_agent = "unit-test",
        .call_id = std::move(call_id),
        .cseq = cseq,
    };
}

auto makeSelector(const ContactBinding& contact) -> ContactBindingSelector {
    ContactBindingSelector selector{
        .normalized_contact_uri = contact.contact_uri,
        .instance_id = contact.instance_id,
        .reg_id = contact.reg_id,
    };
    if (!selector.uses_instance_and_reg_id()) {
        selector.instance_id.clear();
        selector.reg_id.clear();
    }
    return selector;
}

} // namespace

TEST(MemoryRegistrationStoreAtomicTest, UpsertAddsDistinctContactsWithoutOverwrite) {
    MemoryRegistrationStore store;

    auto contact_a = makeContact("sip:user@10.0.0.1:5060", "urn:uuid:ue-a", "1", "call-a", 1);
    auto contact_b = makeContact("sip:user@10.0.0.2:5060", "urn:uuid:ue-b", "2", "call-b", 1);

    auto first = store.upsertContact("sip:user@ims.example.com",
                                     makeSelector(contact_a),
                                     contact_a,
                                     "user@ims.example.com",
                                     "sip:scscf.ims.example.com",
                                     RegistrationBinding::State::kRegistered);
    ASSERT_TRUE(first.has_value()) << first.error().message;
    EXPECT_TRUE(*first);

    auto second = store.upsertContact("sip:user@ims.example.com",
                                      makeSelector(contact_b),
                                      contact_b,
                                      "user@ims.example.com",
                                      "sip:scscf.ims.example.com",
                                      RegistrationBinding::State::kRegistered);
    ASSERT_TRUE(second.has_value()) << second.error().message;
    EXPECT_TRUE(*second);

    auto lookup = store.lookup("sip:user@ims.example.com");
    ASSERT_TRUE(lookup.has_value()) << lookup.error().message;
    ASSERT_EQ(lookup->contacts.size(), 2u);
    EXPECT_EQ(lookup->contacts[0].instance_id, "urn:uuid:ue-a");
    EXPECT_EQ(lookup->contacts[1].instance_id, "urn:uuid:ue-b");
}

TEST(MemoryRegistrationStoreAtomicTest, UpsertRefreshesMatchedContactOnly) {
    MemoryRegistrationStore store;

    auto contact_a = makeContact("sip:user@10.0.0.1:5060", "urn:uuid:ue-a", "1", "call-a", 1);
    auto contact_b = makeContact("sip:user@10.0.0.2:5060", "urn:uuid:ue-b", "2", "call-b", 1);
    ASSERT_TRUE(store.upsertContact("sip:user@ims.example.com", makeSelector(contact_a), contact_a,
                                    "user@ims.example.com", "sip:scscf.ims.example.com",
                                    RegistrationBinding::State::kRegistered)
                    .has_value());
    ASSERT_TRUE(store.upsertContact("sip:user@ims.example.com", makeSelector(contact_b), contact_b,
                                    "user@ims.example.com", "sip:scscf.ims.example.com",
                                    RegistrationBinding::State::kRegistered)
                    .has_value());

    auto refreshed_a = contact_a;
    refreshed_a.path = "<sip:new-pcscf.ims.example.com;lr>";
    refreshed_a.call_id = "call-a";
    refreshed_a.cseq = 2;
    refreshed_a.user_agent = "unit-test-v2";

    auto update = store.upsertContact("sip:user@ims.example.com",
                                      makeSelector(refreshed_a),
                                      refreshed_a,
                                      "user@ims.example.com",
                                      "sip:scscf.ims.example.com",
                                      RegistrationBinding::State::kRegistered,
                                      true,
                                      true);
    ASSERT_TRUE(update.has_value()) << update.error().message;
    EXPECT_TRUE(*update);

    auto lookup = store.lookup("sip:user@ims.example.com");
    ASSERT_TRUE(lookup.has_value()) << lookup.error().message;
    ASSERT_EQ(lookup->contacts.size(), 2u);
    EXPECT_EQ(lookup->contacts[0].path, "<sip:new-pcscf.ims.example.com;lr>");
    EXPECT_EQ(lookup->contacts[0].cseq, 2u);
    EXPECT_EQ(lookup->contacts[1].path, "<sip:pcscf.ims.example.com;lr>");
    EXPECT_EQ(lookup->contacts[1].cseq, 1u);
}

TEST(MemoryRegistrationStoreAtomicTest, RemoveContactDeletesOnlyRequestedBinding) {
    MemoryRegistrationStore store;

    auto contact_a = makeContact("sip:user@10.0.0.1:5060", "urn:uuid:ue-a", "1", "call-a", 1);
    auto contact_b = makeContact("sip:user@10.0.0.2:5060", "urn:uuid:ue-b", "2", "call-b", 1);
    ASSERT_TRUE(store.upsertContact("sip:user@ims.example.com", makeSelector(contact_a), contact_a,
                                    "user@ims.example.com", "sip:scscf.ims.example.com",
                                    RegistrationBinding::State::kRegistered)
                    .has_value());
    ASSERT_TRUE(store.upsertContact("sip:user@ims.example.com", makeSelector(contact_b), contact_b,
                                    "user@ims.example.com", "sip:scscf.ims.example.com",
                                    RegistrationBinding::State::kRegistered)
                    .has_value());

    auto remove = store.removeContact("sip:user@ims.example.com", makeSelector(contact_a));
    ASSERT_TRUE(remove.has_value()) << remove.error().message;
    EXPECT_TRUE(*remove);

    auto lookup = store.lookup("sip:user@ims.example.com");
    ASSERT_TRUE(lookup.has_value()) << lookup.error().message;
    ASSERT_EQ(lookup->contacts.size(), 1u);
    EXPECT_EQ(lookup->contacts[0].instance_id, "urn:uuid:ue-b");
}

TEST(MemoryRegistrationStoreAtomicTest, RemoveContactDeletesEmptyBinding) {
    MemoryRegistrationStore store;

    auto contact = makeContact("sip:user@10.0.0.1:5060", "urn:uuid:ue-a", "1", "call-a", 1);
    ASSERT_TRUE(store.upsertContact("sip:user@ims.example.com", makeSelector(contact), contact,
                                    "user@ims.example.com", "sip:scscf.ims.example.com",
                                    RegistrationBinding::State::kRegistered)
                    .has_value());

    auto remove = store.removeContact("sip:user@ims.example.com", makeSelector(contact));
    ASSERT_TRUE(remove.has_value()) << remove.error().message;
    EXPECT_TRUE(*remove);

    auto lookup = store.lookup("sip:user@ims.example.com");
    ASSERT_FALSE(lookup.has_value());
    EXPECT_EQ(lookup.error().code, ims::ErrorCode::kRegistrationNotFound);
}

TEST(MemoryRegistrationStoreAtomicTest, OlderCseqDoesNotOverwriteMatchedContact) {
    MemoryRegistrationStore store;

    auto current = makeContact("sip:user@10.0.0.1:5060", "urn:uuid:ue-a", "1", "call-a", 5);
    ASSERT_TRUE(store.upsertContact("sip:user@ims.example.com", makeSelector(current), current,
                                    "user@ims.example.com", "sip:scscf.ims.example.com",
                                    RegistrationBinding::State::kRegistered)
                    .has_value());

    auto stale = current;
    stale.path = "<sip:stale-pcscf.ims.example.com;lr>";
    stale.cseq = 4;

    auto update = store.upsertContact("sip:user@ims.example.com",
                                      makeSelector(stale),
                                      stale,
                                      "user@ims.example.com",
                                      "sip:scscf.ims.example.com",
                                      RegistrationBinding::State::kRegistered,
                                      true,
                                      true);
    ASSERT_TRUE(update.has_value()) << update.error().message;
    EXPECT_TRUE(*update);

    auto lookup = store.lookup("sip:user@ims.example.com");
    ASSERT_TRUE(lookup.has_value()) << lookup.error().message;
    ASSERT_EQ(lookup->contacts.size(), 1u);
    EXPECT_EQ(lookup->contacts[0].path, "<sip:pcscf.ims.example.com;lr>");
    EXPECT_EQ(lookup->contacts[0].cseq, 5u);
}
