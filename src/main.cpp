#include "PCH.h"

namespace
{
    constexpr std::string_view kPluginName = "ThorinDragonSoulVisuals";
    constexpr RE::FormID kDragonPowerAbsorbFXS = 0x000280C0;
    constexpr float kStreamDurationSeconds = 8.0F;
    constexpr float kPowerShaderDurationSeconds = 6.5F;
    constexpr std::size_t kPlayerUpdateVFuncIndex = 0x0AD;

    struct Config
    {
        float radius = 6000.0F;
        float windowSeconds = 22.0F;
        std::vector<std::string> names{ "thorin", "throin" };
    };

    Config g_config{};

    std::mutex g_pendingLock;
    std::vector<RE::ObjectRefHandle> g_pendingDragonDeaths;

    struct VisualSession
    {
        RE::ObjectRefHandle dragon;
        RE::ObjectRefHandle thorin;
        std::chrono::steady_clock::time_point expires;
        std::uint8_t redirectedMask = 0;
    };

    std::vector<VisualSession> g_sessions;  // main-thread only
    std::atomic_bool g_updateHookInstalled{ false };

    enum RedirectBits : std::uint8_t
    {
        kDragonStream = 1u << 0,
        kActorStream = 1u << 1,
        kPowerShader = 1u << 2
    };

    void SetupLog()
    {
        auto logDir = SKSE::log::log_directory();
        if (!logDir) {
            return;
        }

        *logDir /= std::string(kPluginName) + ".log";
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logDir->string(), true);
        auto logger = std::make_shared<spdlog::logger>(std::string(kPluginName), std::move(sink));
        spdlog::set_default_logger(std::move(logger));
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        spdlog::set_level(spdlog::level::info);
        spdlog::flush_on(spdlog::level::info);
    }

    [[nodiscard]] std::string Trim(std::string a_value)
    {
        const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
        a_value.erase(a_value.begin(), std::find_if(a_value.begin(), a_value.end(), notSpace));
        a_value.erase(std::find_if(a_value.rbegin(), a_value.rend(), notSpace).base(), a_value.end());
        return a_value;
    }

    [[nodiscard]] std::string Lower(std::string_view a_value)
    {
        std::string out(a_value);
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return out;
    }

    void LoadConfig()
    {
        const auto path = std::filesystem::path("Data") / "SKSE" / "Plugins" / "ThorinDragonSoulVisuals.ini";
        std::ifstream in(path);
        if (!in) {
            spdlog::info("INI not found; using defaults: radius={}, window={}s, names=Thorin/Throin",
                         g_config.radius, g_config.windowSeconds);
            return;
        }

        std::string line;
        while (std::getline(in, line)) {
            line = Trim(line);
            if (line.empty() || line.front() == ';' || line.front() == '#' || line.front() == '[') {
                continue;
            }

            const auto eq = line.find('=');
            if (eq == std::string::npos) {
                continue;
            }

            const auto key = Lower(Trim(line.substr(0, eq)));
            const auto value = Trim(line.substr(eq + 1));

            try {
                if (key == "radius") {
                    g_config.radius = std::max(0.0F, std::stof(value));
                } else if (key == "visualwindowseconds") {
                    g_config.windowSeconds = std::clamp(std::stof(value), 5.0F, 60.0F);
                } else if (key == "names") {
                    std::vector<std::string> names;
                    std::stringstream ss(value);
                    std::string item;
                    while (std::getline(ss, item, ',')) {
                        item = Lower(Trim(item));
                        if (!item.empty()) {
                            names.push_back(std::move(item));
                        }
                    }
                    if (!names.empty()) {
                        g_config.names = std::move(names);
                    }
                }
            } catch (...) {
                spdlog::warn("Ignoring invalid INI value: {}={}", key, value);
            }
        }

        spdlog::info("Config loaded: radius={}, visual window={}s, target name count={}",
                     g_config.radius, g_config.windowSeconds, g_config.names.size());
    }

    [[nodiscard]] bool NameMatches(RE::Actor* a_actor)
    {
        if (!a_actor) {
            return false;
        }

        const char* displayName = a_actor->GetDisplayFullName();
        if (!displayName || displayName[0] == '\0') {
            displayName = a_actor->GetName();
        }
        if (!displayName || displayName[0] == '\0') {
            return false;
        }

        const auto lowerName = Lower(displayName);
        return std::ranges::any_of(g_config.names, [&](const std::string& candidate) {
            return lowerName == candidate;
        });
    }

    [[nodiscard]] RE::Actor* FindThorinNear(RE::TESObjectREFR* a_dragon)
    {
        if (!a_dragon) {
            return nullptr;
        }

        auto* processLists = RE::ProcessLists::GetSingleton();
        if (!processLists) {
            return nullptr;
        }

        RE::Actor* best = nullptr;
        float bestDistance = g_config.radius;

        processLists->ForEachHighActor([&](RE::Actor* a_actor) {
            if (!a_actor || a_actor->IsPlayerRef() || !a_actor->Is3DLoaded() || !NameMatches(a_actor)) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            const float distance = a_dragon->GetDistance(a_actor, false, false);
            if (std::isfinite(distance) && distance <= bestDistance) {
                best = a_actor;
                bestDistance = distance;
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });

        if (best) {
            spdlog::info("Target found near dragon {:08X}: '{}' {:08X}, distance={:.1f}",
                         a_dragon->GetFormID(), best->GetDisplayFullName(), best->GetFormID(), bestDistance);
        }
        return best;
    }

    struct ReplacementAction
    {
        RE::ObjectRefHandle target;
        RE::ObjectRefHandle facingTarget;
        RE::BGSArtObject* art = nullptr;
        RE::TESEffectShader* shader = nullptr;
        float duration = 0.0F;
        bool faceTarget = false;
    };

    [[nodiscard]] bool QueueReferenceReplacement(
        RE::ReferenceEffect* a_effect,
        RE::TESObjectREFR* a_newTarget,
        RE::TESObjectREFR* a_newFacingTarget,
        float a_duration,
        std::vector<ReplacementAction>& a_actions)
    {
        if (!a_effect || !a_effect->controller || !a_newTarget) {
            return false;
        }

        auto* controller = a_effect->controller;
        auto* art = controller->GetHitEffectArt();
        auto* shader = controller->GetHitEffectShader();
        if (!art && !shader) {
            return false;
        }

        // ForEachMagicTempEffect holds ProcessLists::magicEffectsLock while this
        // callback executes. Never create a new temp effect under that lock.
        // Mark only the original visual instance finished and queue its visual
        // data; replacements are created immediately after iteration returns.
        a_effect->finished = true;

        a_actions.push_back(ReplacementAction{
            a_newTarget->CreateRefHandle(),
            a_newFacingTarget ? a_newFacingTarget->CreateRefHandle() : RE::ObjectRefHandle{},
            art,
            shader,
            a_duration,
            controller->EffectShouldFaceTarget()
        });
        return true;
    }

    void ApplyQueuedReplacements(const std::vector<ReplacementAction>& a_actions)
    {
        for (const auto& action : a_actions) {
            auto targetPtr = action.target.get();
            auto* target = targetPtr.get();
            if (!target || !target->Is3DLoaded()) {
                continue;
            }

            auto facingPtr = action.facingTarget.get();
            auto* facing = facingPtr.get();

            if (action.art) {
                target->ApplyArtObject(
                    action.art,
                    action.duration,
                    facing,
                    action.faceTarget,
                    false,  // never attach a replacement to the player's camera
                    nullptr,
                    false);
            }
            if (action.shader) {
                target->ApplyEffectShader(
                    action.shader,
                    action.duration,
                    facing,
                    action.faceTarget,
                    false,
                    nullptr,
                    false);
            }
        }
    }

    void ArmSessionsFromDeaths()
    {
        std::vector<RE::ObjectRefHandle> deaths;
        {
            std::lock_guard lock(g_pendingLock);
            deaths.swap(g_pendingDragonDeaths);
        }

        if (deaths.empty()) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        for (auto& dragonHandle : deaths) {
            auto dragonPtr = dragonHandle.get();
            auto* dragon = dragonPtr.get();
            if (!dragon || !dragon->IsDragon()) {
                continue;
            }

            auto* thorin = FindThorinNear(dragon);
            if (!thorin) {
                spdlog::info("Dragon {:08X} died, but Thorin/Throin was not within {:.0f} units; vanilla visuals unchanged",
                             dragon->GetFormID(), g_config.radius);
                continue;
            }

            const auto already = std::ranges::any_of(g_sessions, [&](const VisualSession& s) {
                return s.dragon == dragonHandle;
            });
            if (already) {
                continue;
            }

            g_sessions.push_back(VisualSession{
                dragonHandle,
                thorin->CreateRefHandle(),
                now + std::chrono::milliseconds(static_cast<std::int64_t>(g_config.windowSeconds * 1000.0F)),
                0
            });

            spdlog::info("Armed visual redirect for dragon {:08X} -> '{}' {:08X}; soul/quest ownership remains player/vanilla",
                         dragon->GetFormID(), thorin->GetDisplayFullName(), thorin->GetFormID());
        }
    }

    void ScanAndRedirectVisuals(RE::PlayerCharacter* a_player)
    {
        if (!a_player || g_sessions.empty()) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        std::erase_if(g_sessions, [&](const VisualSession& s) {
            return now >= s.expires;
        });
        if (g_sessions.empty()) {
            return;
        }

        auto* processLists = RE::ProcessLists::GetSingleton();
        if (!processLists) {
            return;
        }

        const auto playerHandle = a_player->CreateRefHandle();
        std::vector<ReplacementAction> replacements;
        replacements.reserve(6);

        processLists->ForEachMagicTempEffect([&](RE::BSTempEffect* a_tempEffect) {
            if (!a_tempEffect) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            auto* referenceEffect = a_tempEffect->As<RE::ReferenceEffect>();
            if (!referenceEffect || referenceEffect->finished) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            for (auto& session : g_sessions) {
                auto dragonPtr = session.dragon.get();
                auto thorinPtr = session.thorin.get();
                auto* dragon = dragonPtr.get();
                auto* thorinRef = thorinPtr.get();
                auto* thorin = thorinRef ? thorinRef->As<RE::Actor>() : nullptr;
                if (!dragon || !thorin || !thorin->Is3DLoaded()) {
                    continue;
                }

                // Vanilla: DragonAbsorbEffect.Play(dragonRef, 8, AbsorbActor)
                if (!(session.redirectedMask & kDragonStream) &&
                    referenceEffect->target == session.dragon &&
                    referenceEffect->aimAtTarget == playerHandle) {
                    if (QueueReferenceReplacement(
                            referenceEffect,
                            dragon,
                            thorin,
                            kStreamDurationSeconds,
                            replacements)) {
                        session.redirectedMask |= kDragonStream;
                        spdlog::info("Redirected dragon-side absorb stream: dragon {:08X} now faces Thorin {:08X}",
                                     dragon->GetFormID(), thorin->GetFormID());
                    }
                    continue;
                }

                // Vanilla: DragonAbsorbManEffect.Play(AbsorbActor, 8, dragonRef)
                if (!(session.redirectedMask & kActorStream) &&
                    referenceEffect->target == playerHandle &&
                    referenceEffect->aimAtTarget == session.dragon) {
                    if (QueueReferenceReplacement(
                            referenceEffect,
                            thorin,
                            dragon,
                            kStreamDurationSeconds,
                            replacements)) {
                        session.redirectedMask |= kActorStream;
                        spdlog::info("Redirected absorber-side stream from player to Thorin {:08X}", thorin->GetFormID());
                    }
                    continue;
                }

                // Vanilla DragonPowerAbsorbFXS is Skyrim.esm 000280C0.
                // Wait until at least one stream proves this is an absorb sequence,
                // then stop only that player shader and queue it for Thorin.
                if (!(session.redirectedMask & kPowerShader) &&
                    (session.redirectedMask & (kDragonStream | kActorStream)) != 0 &&
                    referenceEffect->target == playerHandle) {
                    auto* shaderEffect = a_tempEffect->As<RE::ShaderReferenceEffect>();
                    if (shaderEffect && shaderEffect->effectData &&
                        shaderEffect->effectData->GetFormID() == kDragonPowerAbsorbFXS) {
                        shaderEffect->finished = true;
                        replacements.push_back(ReplacementAction{
                            thorin->CreateRefHandle(),
                            {},
                            nullptr,
                            shaderEffect->effectData,
                            kPowerShaderDurationSeconds,
                            false
                        });
                        session.redirectedMask |= kPowerShader;
                        spdlog::info("Redirected DragonPowerAbsorbFXS from player to Thorin {:08X}", thorin->GetFormID());
                    }
                }
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });

        // The ProcessLists temp-effect lock is now released, so it is safe to
        // create the replacement art/shader effects without recursive locking.
        ApplyQueuedReplacements(replacements);
    }

    void Tick(RE::PlayerCharacter* a_player)
    {
        ArmSessionsFromDeaths();
        ScanAndRedirectVisuals(a_player);
    }

    class DragonDeathSink final : public RE::BSTEventSink<RE::TESDeathEvent>
    {
    public:
        static DragonDeathSink* GetSingleton()
        {
            static DragonDeathSink singleton;
            return std::addressof(singleton);
        }

        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESDeathEvent* a_event,
            RE::BSTEventSource<RE::TESDeathEvent>*) override
        {
            // TESDeathEvent fires twice. Only the dead=true half arms a session.
            if (!a_event || !a_event->dead || !a_event->actorDying || !a_event->actorDying->IsDragon()) {
                return RE::BSEventNotifyControl::kContinue;
            }

            const auto handle = a_event->actorDying->CreateRefHandle();
            if (handle) {
                std::lock_guard lock(g_pendingLock);
                g_pendingDragonDeaths.push_back(handle);
            }

            return RE::BSEventNotifyControl::kContinue;
        }
    };

    struct PlayerUpdateHook
    {
        static void Thunk(RE::PlayerCharacter* a_player, float a_delta)
        {
            _original(a_player, a_delta);
            Tick(a_player);
        }

        static bool Install()
        {
            if (REL::Module::IsVR()) {
                spdlog::error("VR runtime detected; PlayerCharacter::Update vtable index is not verified for VR. Plugin disabled safely.");
                return false;
            }

            if (g_updateHookInstalled.exchange(true)) {
                return true;
            }

            REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_PlayerCharacter[0] };
            _original = vtable.write_vfunc(kPlayerUpdateVFuncIndex, Thunk);
            spdlog::info("Installed PlayerCharacter::Update hook at vtable index 0x{:X}", kPlayerUpdateVFuncIndex);
            return true;
        }

        static inline REL::Relocation<decltype(Thunk)> _original;
    };

    void OnSKSEMessage(SKSE::MessagingInterface::Message* a_message)
    {
        if (!a_message || a_message->type != SKSE::MessagingInterface::kDataLoaded) {
            return;
        }

        LoadConfig();

        if (auto* source = RE::ScriptEventSourceHolder::GetSingleton()) {
            source->AddEventSink<RE::TESDeathEvent>(DragonDeathSink::GetSingleton());
            spdlog::info("Registered TESDeathEvent sink");
        } else {
            spdlog::error("ScriptEventSourceHolder unavailable; plugin cannot observe dragon deaths");
            return;
        }

        PlayerUpdateHook::Install();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);
    SetupLog();

    spdlog::info("{} loading; runtime {}", kPluginName, a_skse->RuntimeVersion().string());

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        spdlog::critical("SKSE MessagingInterface unavailable");
        return false;
    }

    return messaging->RegisterListener(OnSKSEMessage);
}
