#include "PCH.h"

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr float kMaxDistance = 6000.0f;
    constexpr auto kPendingLifetime = 45s;
    constexpr auto kPostTransferLifetime = 12s;
    constexpr auto kShaderWindow = 3s;
    constexpr auto kTickInterval = 20ms;
    constexpr RE::FormID kDragonPowerAbsorbFXSFormID = 0x000280C0;

    struct PendingDragon
    {
        RE::ObjectRefHandle dragon;
        Clock::time_point expiresAt{};
        Clock::time_point shaderWindowUntil{};
        bool streamSeen{ false };
        bool shaderTransferred{ false };
    };

    struct EffectReplica
    {
        RE::TESObjectREFR* target{ nullptr };
        RE::TESObjectREFR* facing{ nullptr };
        RE::BGSArtObject* art{ nullptr };
        RE::TESEffectShader* shader{ nullptr };
        float duration{ 8.0f };
        bool faceTarget{ false };
    };

    std::vector<PendingDragon> g_pending;
    std::atomic_bool g_hasPending{ false };
    std::atomic_bool g_tickQueued{ false };
    std::jthread g_worker;
    SKSE::TaskInterface* g_tasks = nullptr;
    RE::TESEffectShader* g_dragonPowerAbsorbFXS = nullptr;
    bool g_eventsRegistered = false;

    [[nodiscard]] std::string Lower(std::string_view a_text)
    {
        std::string result{ a_text };
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char a_ch) {
            return static_cast<char>(std::tolower(a_ch));
        });
        return result;
    }

    [[nodiscard]] bool IsThorinName(const char* a_name)
    {
        if (!a_name || !*a_name) {
            return false;
        }

        const auto lower = Lower(a_name);
        return lower.find("thorin") != std::string::npos ||
               lower.find("throin") != std::string::npos;
    }

    [[nodiscard]] float Distance(RE::TESObjectREFR* a_lhs, RE::TESObjectREFR* a_rhs)
    {
        if (!a_lhs || !a_rhs) {
            return std::numeric_limits<float>::max();
        }

        const auto lhs = a_lhs->GetPosition();
        const auto rhs = a_rhs->GetPosition();
        const auto dx = lhs.x - rhs.x;
        const auto dy = lhs.y - rhs.y;
        const auto dz = lhs.z - rhs.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    [[nodiscard]] RE::Actor* FindNearbyThorin(RE::TESObjectREFR* a_dragon)
    {
        auto* processes = RE::ProcessLists::GetSingleton();
        if (!processes || !a_dragon) {
            return nullptr;
        }

        RE::Actor* best = nullptr;
        float bestDistance = kMaxDistance;

        processes->ForAllActors([&](RE::Actor* a_actor) {
            if (!a_actor || a_actor->IsDead() || a_actor->IsDisabled() || !a_actor->Is3DLoaded()) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            if (!IsThorinName(a_actor->GetDisplayFullName())) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            const auto distance = Distance(a_dragon, a_actor);
            if (distance <= bestDistance) {
                best = a_actor;
                bestDistance = distance;
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });

        return best;
    }

    [[nodiscard]] float RemainingLifetime(const RE::ReferenceEffect* a_effect)
    {
        if (!a_effect) {
            return 8.0f;
        }

        if (a_effect->lifetime > 0.0f) {
            return std::max(0.25f, a_effect->lifetime - a_effect->age);
        }

        return 8.0f;
    }

    void ApplyReplica(const EffectReplica& a_replica)
    {
        if (!a_replica.target) {
            return;
        }

        if (a_replica.art) {
            a_replica.target->ApplyArtObject(
                a_replica.art,
                a_replica.duration,
                a_replica.facing,
                a_replica.faceTarget,
                false,
                nullptr,
                false);
        }

        if (a_replica.shader) {
            a_replica.target->ApplyEffectShader(
                a_replica.shader,
                a_replica.duration,
                a_replica.facing,
                a_replica.faceTarget,
                false,
                nullptr,
                false);
        }
    }

    void Tick()
    {
        g_tickQueued.store(false, std::memory_order_release);

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* processes = RE::ProcessLists::GetSingleton();
        if (!player || !processes) {
            g_pending.clear();
            g_hasPending.store(false, std::memory_order_release);
            return;
        }

        const auto now = Clock::now();
        const auto playerHandle = player->CreateRefHandle();

        for (auto& pending : g_pending) {
            if (now >= pending.expiresAt) {
                continue;
            }

            auto dragonPtr = pending.dragon.get();
            auto* dragon = dragonPtr ? dragonPtr->As<RE::Actor>() : nullptr;
            if (!dragon) {
                pending.expiresAt = now;
                continue;
            }

            auto* thorin = FindNearbyThorin(dragon);
            if (!thorin) {
                continue;
            }

            const auto dragonHandle = dragon->CreateRefHandle();
            std::vector<EffectReplica> replicas;

            processes->ForEachMagicTempEffect([&](RE::BSTempEffect* a_tempEffect) {
                auto* effect = a_tempEffect ? a_tempEffect->As<RE::ReferenceEffect>() : nullptr;
                if (!effect || effect->finished || !effect->controller) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }

                RE::TESObjectREFR* newTarget = nullptr;
                RE::TESObjectREFR* newFacing = nullptr;

                if (effect->target == playerHandle && effect->aimAtTarget == dragonHandle) {
                    newTarget = thorin;
                    newFacing = dragon;
                } else if (effect->target == dragonHandle && effect->aimAtTarget == playerHandle) {
                    newTarget = dragon;
                    newFacing = thorin;
                } else {
                    return RE::BSContainer::ForEachResult::kContinue;
                }

                auto* art = effect->controller->GetHitEffectArt();
                auto* shader = effect->controller->GetHitEffectShader();
                if (!art && !shader) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }

                EffectReplica replica;
                replica.target = newTarget;
                replica.facing = newFacing;
                replica.art = art;
                replica.shader = shader;
                replica.duration = RemainingLifetime(effect);
                replica.faceTarget = effect->controller->EffectShouldFaceTarget();
                replicas.push_back(replica);

                // Ending only the exact dragon<->player stream effect does not touch
                // MQKillDragonScript, DragonSouls, quest stages, or the death sequence.
                effect->finished = true;

                return RE::BSContainer::ForEachResult::kContinue;
            });

            // Important: create replacement effects only after ForEachMagicTempEffect
            // releases ProcessLists::magicEffectsLock.
            for (const auto& replica : replicas) {
                ApplyReplica(replica);
            }

            if (!replicas.empty()) {
                pending.streamSeen = true;
                pending.shaderWindowUntil = now + kShaderWindow;
                pending.expiresAt = std::min(pending.expiresAt, now + kPostTransferLifetime);
            }

            if (pending.streamSeen && !pending.shaderTransferred &&
                now <= pending.shaderWindowUntil && g_dragonPowerAbsorbFXS) {

                bool transferShader = false;
                processes->ForEachShaderEffect([&](RE::ShaderReferenceEffect* a_effect) {
                    if (!a_effect || a_effect->finished ||
                        a_effect->target != playerHandle ||
                        a_effect->effectData != g_dragonPowerAbsorbFXS) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    }

                    a_effect->finished = true;
                    transferShader = true;
                    return RE::BSContainer::ForEachResult::kStop;
                });

                // Apply after the shader list lock is released.
                if (transferShader) {
                    thorin->ApplyEffectShader(
                        g_dragonPowerAbsorbFXS,
                        4.0f,
                        nullptr,
                        false,
                        false,
                        nullptr,
                        false);
                    pending.shaderTransferred = true;
                }
            }
        }

        std::erase_if(g_pending, [&](const PendingDragon& a_pending) {
            return now >= a_pending.expiresAt || !a_pending.dragon;
        });

        g_hasPending.store(!g_pending.empty(), std::memory_order_release);
    }

    void AddPendingDragon(RE::ObjectRefHandle a_dragon)
    {
        if (!a_dragon) {
            return;
        }

        const auto now = Clock::now();
        const auto existing = std::find_if(g_pending.begin(), g_pending.end(), [&](const PendingDragon& a_pending) {
            return a_pending.dragon == a_dragon;
        });

        if (existing != g_pending.end()) {
            existing->expiresAt = now + kPendingLifetime;
            existing->streamSeen = false;
            existing->shaderTransferred = false;
            existing->shaderWindowUntil = {};
        } else {
            PendingDragon pending;
            pending.dragon = a_dragon;
            pending.expiresAt = now + kPendingLifetime;
            g_pending.push_back(pending);
        }

        g_hasPending.store(true, std::memory_order_release);
    }

    class DeathSink final : public RE::BSTEventSink<RE::TESDeathEvent>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESDeathEvent* a_event,
            RE::BSTEventSource<RE::TESDeathEvent>*) override
        {
            if (!a_event || !a_event->dead || !a_event->actorDying || !g_tasks) {
                return RE::BSEventNotifyControl::kContinue;
            }

            auto* dyingActor = a_event->actorDying->As<RE::Actor>();
            if (!dyingActor || !dyingActor->IsDragon()) {
                return RE::BSEventNotifyControl::kContinue;
            }

            const auto handle = dyingActor->CreateRefHandle();
            g_tasks->AddTask([handle]() {
                AddPendingDragon(handle);
            });

            return RE::BSEventNotifyControl::kContinue;
        }
    };

    DeathSink g_deathSink;

    void StartWorker()
    {
        if (g_worker.joinable()) {
            return;
        }

        g_worker = std::jthread([](std::stop_token a_stopToken) {
            while (!a_stopToken.stop_requested()) {
                std::this_thread::sleep_for(kTickInterval);

                if (!g_tasks || !g_hasPending.load(std::memory_order_acquire)) {
                    continue;
                }

                bool expected = false;
                if (!g_tickQueued.compare_exchange_strong(
                        expected,
                        true,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire)) {
                    continue;
                }

                g_tasks->AddTask([]() {
                    Tick();
                });
            }
        });
    }

    void ClearPending()
    {
        g_pending.clear();
        g_hasPending.store(false, std::memory_order_release);
    }

    void OnSKSEMessage(SKSE::MessagingInterface::Message* a_message)
    {
        if (!a_message) {
            return;
        }

        switch (a_message->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            g_dragonPowerAbsorbFXS =
                RE::TESForm::LookupByID<RE::TESEffectShader>(kDragonPowerAbsorbFXSFormID);

            if (!g_eventsRegistered) {
                if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
                    events->AddEventSink<RE::TESDeathEvent>(&g_deathSink);
                    g_eventsRegistered = true;
                }
            }

            StartWorker();
            break;

        case SKSE::MessagingInterface::kPostLoadGame:
        case SKSE::MessagingInterface::kNewGame:
            ClearPending();
            break;

        default:
            break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);

    g_tasks = SKSE::GetTaskInterface();
    if (!g_tasks) {
        return false;
    }

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        return false;
    }

    return messaging->RegisterListener(OnSKSEMessage);
}
