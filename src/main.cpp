#include "PCH.h"

namespace
{
    constexpr std::array<std::string_view, 3> kNotifications{
        "If Thorin hears about this, I'm dead."sv,
        "I can't shout in front of these people."sv,
        "I don't want to end up in the torture room again."sv
    };

    constexpr std::array<std::string_view, 3> kBlockedLocationKeywordIDs{
        "LocTypeCity"sv,
        "LocTypeTown"sv,
        "LocTypeSettlement"sv
    };

    std::array<RE::BGSKeyword*, kBlockedLocationKeywordIDs.size()> g_blockedKeywords{};
    bool g_locationKeywordsReady = false;
    bool g_hookInstalled = false;

    [[nodiscard]] std::string_view GetRandomNotification()
    {
        static std::mt19937 rng{ std::random_device{}() };
        static std::uniform_int_distribution<std::size_t> distribution{ 0, kNotifications.size() - 1 };
        return kNotifications[distribution(rng)];
    }

    void ResolveLocationKeywords()
    {
        for (std::size_t i = 0; i < kBlockedLocationKeywordIDs.size(); ++i) {
            g_blockedKeywords[i] =
                RE::TESForm::LookupByEditorID<RE::BGSKeyword>(kBlockedLocationKeywordIDs[i]);
        }
        g_locationKeywordsReady = true;
    }

    [[nodiscard]] bool IsBlockedSettlementLocation(RE::BGSLocation* a_location)
    {
        constexpr std::size_t kMaxParentDepth = 32;

        for (std::size_t depth = 0;
             a_location && depth < kMaxParentDepth;
             ++depth, a_location = a_location->parentLoc) {

            for (auto* keyword : g_blockedKeywords) {
                if (keyword && a_location->HasKeyword(keyword)) {
                    return true;
                }
            }
        }
        return false;
    }

    [[nodiscard]] bool ShouldBlockCurrentShout()
    {
        if (!g_locationKeywordsReady) {
            return false;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->GetCurrentShout()) {
            return false;
        }

        return IsBlockedSettlementLocation(player->GetCurrentLocation());
    }

    struct ShoutProcessHook
    {
        static void Thunk(
            RE::ShoutHandler* a_handler,
            RE::ButtonEvent* a_event,
            RE::PlayerControlsData* a_data)
        {
            if (a_event && ShouldBlockCurrentShout()) {
                if (a_event->IsDown()) {
                    const auto notification = GetRandomNotification();
                    RE::DebugNotification(notification.data(), nullptr, true);
                }
                return;
            }

            _original(a_handler, a_event, a_data);
        }

        static bool Install()
        {
            if (g_hookInstalled) {
                return true;
            }

            auto* controls = RE::PlayerControls::GetSingleton();
            if (!controls || !controls->shoutHandler) {
                return false;
            }

            auto* vtable = *reinterpret_cast<std::uintptr_t**>(controls->shoutHandler);
            if (!vtable) {
                return false;
            }

            REL::Relocation<std::uintptr_t> vtableRelocation{
                reinterpret_cast<std::uintptr_t>(vtable)
            };

            _original = vtableRelocation.write_vfunc(0x04, Thunk);
            g_hookInstalled = true;
            return true;
        }

        static inline REL::Relocation<decltype(Thunk)> _original;
    };

    void OnSKSEMessage(SKSE::MessagingInterface::Message* a_message)
    {
        if (!a_message) {
            return;
        }

        switch (a_message->type) {
        case SKSE::MessagingInterface::kInputLoaded:
            ShoutProcessHook::Install();
            break;

        case SKSE::MessagingInterface::kDataLoaded:
            ResolveLocationKeywords();
            ShoutProcessHook::Install();
            break;

        default:
            break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        return false;
    }

    return messaging->RegisterListener(OnSKSEMessage);
}
