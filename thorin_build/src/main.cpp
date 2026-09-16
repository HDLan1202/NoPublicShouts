#include "PCH.h"

namespace
{
    constexpr std::string_view kTargetEditorID = "realdragonborn"sv;
    constexpr RE::FormID kTargetFallbackRefID = 0x250050CE;
    constexpr float kRadius = 6000.0F;
    constexpr auto kWatchWindow = std::chrono::seconds(22);
    constexpr auto kPollInterval = std::chrono::milliseconds(30);

    RE::TESObjectREFR* g_target = nullptr;
    RE::BGSReferenceEffect* g_dragonAbsorbEffect = nullptr;
    RE::BGSReferenceEffect* g_dragonAbsorbManEffect = nullptr;
    RE::TESEffectShader* g_dragonPowerAbsorbFXS = nullptr;

    std::atomic<std::uint64_t> g_generation{ 0 };
    std::atomic_bool g_visualTransferred{ false };
    RE::ObjectRefHandle g_dragonHandle{};

    void SetupLog()
    {
        auto logDir = SKSE::log::log_directory();
        if (!logDir) {
            return;
        }

        *logDir /= "ThorinDragonSoulVisuals.log";
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logDir->string(), true);
        auto logger = std::make_shared<spdlog::logger>("global log", std::move(sink));
        spdlog::set_default_logger(std::move(logger));
        spdlog::set_level(spdlog::level::info);
        spdlog::flush_on(spdlog::level::info);
    }

    [[nodiscard]] bool SameRef(const RE::ObjectRefHandle& a_handle, const RE::TESObjectREFR* a_ref)
    {
        if (!a_ref) {
            return false;
        }
        auto ref = a_handle.get();
        return ref && ref.get() == a_ref;
    }

    [[nodiscard]] float DistanceSquared(const RE::TESObjectREFR* a_lhs, const RE::TESObjectREFR* a_rhs)
    {
        const auto lhs = a_lhs->GetPosition();
        const auto rhs = a_rhs->GetPosition();
        const float dx = lhs.x - rhs.x;
        const float dy = lhs.y - rhs.y;
        const float dz = lhs.z - rhs.z;
        return dx * dx + dy * dy + dz * dz;
    }

    RE::TESObjectREFR* ResolveTarget()
    {
        if (auto* byEditorID = RE::TESForm::LookupByEditorID<RE::TESObjectREFR>(kTargetEditorID)) {
            SKSE::log::info(
                "Resolved target by EditorID '{}': runtime FormID {:08X}",
                kTargetEditorID,
                byEditorID->GetFormID());
            return byEditorID;
        }

        if (auto* byRuntimeID = RE::TESForm::LookupByID<RE::TESObjectREFR>(kTargetFallbackRefID)) {
            SKSE::log::warn(
                "EditorID '{}' was not found; using fallback runtime RefFormID {:08X}",
                kTargetEditorID,
                kTargetFallbackRefID);
            return byRuntimeID;
        }

        SKSE::log::error(
            "Could not resolve target. EditorID='{}', fallback RefFormID={:08X}",
            kTargetEditorID,
            kTargetFallbackRefID);
        return nullptr;
    }

    void ResolveVanillaEffects()
    {
        g_dragonAbsorbEffect =
            RE::TESForm::LookupByEditorID<RE::BGSReferenceEffect>("DragonAbsorbEffect"sv);
        g_dragonAbsorbManEffect =
            RE::TESForm::LookupByEditorID<RE::BGSReferenceEffect>("DragonAbsorbManEffect"sv);
        g_dragonPowerAbsorbFXS =
            RE::TESForm::LookupByEditorID<RE::TESEffectShader>("DragonPowerAbsorbFXS"sv);

        if (!g_dragonPowerAbsorbFXS) {
            g_dragonPowerAbsorbFXS =
                RE::TESForm::LookupByID<RE::TESEffectShader>(0x000280C0);
        }

        SKSE::log::info(
            "Vanilla FX resolved: DragonAbsorbEffect={}, DragonAbsorbManEffect={}, DragonPowerAbsorbFXS={}",
            static_cast<const void*>(g_dragonAbsorbEffect),
            static_cast<const void*>(g_dragonAbsorbManEffect),
            static_cast<const void*>(g_dragonPowerAbsorbFXS));
    }

    void RetargetModelEffect(
        RE::ModelReferenceEffect& a_effect,
        RE::TESObjectREFR* a_newTarget,
        RE::TESObjectREFR* a_newFacingTarget)
    {
        if (!a_newTarget) {
            return;
        }

        a_effect.target = a_newTarget->CreateRefHandle();
        if (a_newFacingTarget) {
            a_effect.aimAtTarget = a_newFacingTarget->CreateRefHandle();
        }
        a_effect.UpdatePosition();
    }

    void RetargetShaderEffect(RE::ShaderReferenceEffect& a_effect, RE::TESObjectREFR* a_newTarget)
    {
        if (!a_newTarget) {
            return;
        }

        // ShaderReferenceEffect caches its old attach root. Suspend/Resume plus clearing
        // the cached roots makes it resolve the new target instead of staying on player.
        a_effect.Suspend();
        a_effect.target = a_newTarget->CreateRefHandle();
        a_effect.lastRootNode.reset();
        a_effect.targetRoot.reset();
        a_effect.Resume();
        a_effect.UpdatePosition();
    }

    void PollEffects(std::uint64_t a_generation)
    {
        if (a_generation != g_generation.load(std::memory_order_acquire) ||
            g_visualTransferred.load(std::memory_order_acquire)) {
            return;
        }

        auto dragon = g_dragonHandle.get();
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* target = g_target ? g_target : ResolveTarget();
        if (!dragon || !player || !target) {
            return;
        }
        g_target = target;

        auto* processLists = RE::ProcessLists::GetSingleton();
        if (!processLists) {
            return;
        }

        const auto targetHandle = target->CreateRefHandle();
        bool sawDragonStream = false;
        bool sawManStream = false;
        bool sawPlayerShader = false;

        processLists->ForEachModelEffect(
            [&](RE::ModelReferenceEffect& a_effect) {
                if (g_dragonAbsorbEffect &&
                    a_effect.artObject == g_dragonAbsorbEffect->data.artObject &&
                    SameRef(a_effect.target, dragon.get()) &&
                    SameRef(a_effect.aimAtTarget, player)) {

                    a_effect.aimAtTarget = targetHandle;
                    a_effect.UpdatePosition();
                    sawDragonStream = true;
                    SKSE::log::info("Retargeted DragonAbsorbEffect facing target to realdragonborn");
                }

                if (g_dragonAbsorbManEffect &&
                    a_effect.artObject == g_dragonAbsorbManEffect->data.artObject &&
                    SameRef(a_effect.target, player)) {

                    RetargetModelEffect(a_effect, target, dragon.get());
                    sawManStream = true;
                    SKSE::log::info("Retargeted DragonAbsorbManEffect from player to realdragonborn");
                }

                return RE::BSContainer::ForEachResult::kContinue;
            });

        processLists->ForEachShaderEffect(
            [&](RE::ShaderReferenceEffect& a_effect) {
                if (g_dragonPowerAbsorbFXS &&
                    a_effect.effectData == g_dragonPowerAbsorbFXS &&
                    SameRef(a_effect.target, player)) {

                    RetargetShaderEffect(a_effect, target);
                    sawPlayerShader = true;
                    SKSE::log::info("Retargeted DragonPowerAbsorbFXS from player to realdragonborn");
                }

                return RE::BSContainer::ForEachResult::kContinue;
            });

        // The shader is the reliable marker that vanilla MQKillDragon has actually entered
        // the soul-absorption sequence. Once it has been moved, the mechanical vanilla
        // sequence is deliberately left untouched.
        if (sawPlayerShader) {
            g_visualTransferred.store(true, std::memory_order_release);
            SKSE::log::info(
                "Dragon soul visual transfer complete. dragon={:08X}, target={:08X}, dragonStream={}, manStream={}",
                dragon->GetFormID(),
                target->GetFormID(),
                sawDragonStream,
                sawManStream);
        }
    }

    void StartWatch(RE::Actor* a_dragon)
    {
        if (!a_dragon) {
            return;
        }

        auto* target = g_target ? g_target : ResolveTarget();
        if (!target) {
            SKSE::log::warn("Dragon died, but realdragonborn could not be resolved");
            return;
        }
        g_target = target;

        if (!target->Is3DLoaded()) {
            SKSE::log::info(
                "Ignoring dragon {:08X}: realdragonborn {:08X} has no loaded 3D",
                a_dragon->GetFormID(),
                target->GetFormID());
            return;
        }

        const float radiusSq = kRadius * kRadius;
        const float distanceSq = DistanceSquared(a_dragon, target);
        if (distanceSq > radiusSq) {
            SKSE::log::info(
                "Ignoring dragon {:08X}: realdragonborn is outside radius (distance {:.1f}, radius {:.1f})",
                a_dragon->GetFormID(),
                std::sqrt(distanceSq),
                kRadius);
            return;
        }

        g_dragonHandle = a_dragon->CreateRefHandle();
        g_visualTransferred.store(false, std::memory_order_release);
        const auto generation = g_generation.fetch_add(1, std::memory_order_acq_rel) + 1;

        SKSE::log::info(
            "Watching dragon {:08X} for vanilla soul FX; target realdragonborn={:08X}; generation={}",
            a_dragon->GetFormID(),
            target->GetFormID(),
            generation);

        std::thread([generation]() {
            const auto deadline = std::chrono::steady_clock::now() + kWatchWindow;
            while (std::chrono::steady_clock::now() < deadline) {
                if (generation != g_generation.load(std::memory_order_acquire) ||
                    g_visualTransferred.load(std::memory_order_acquire)) {
                    break;
                }

                if (auto* tasks = SKSE::GetTaskInterface()) {
                    tasks->AddTask([generation]() { PollEffects(generation); });
                }
                std::this_thread::sleep_for(kPollInterval);
            }

            if (generation == g_generation.load(std::memory_order_acquire) &&
                !g_visualTransferred.load(std::memory_order_acquire)) {
                SKSE::log::warn("Soul FX watch window expired without seeing DragonPowerAbsorbFXS");
            }
        }).detach();
    }

    class DeathEventSink final : public RE::BSTEventSink<RE::TESDeathEvent>
    {
    public:
        static DeathEventSink* GetSingleton()
        {
            static DeathEventSink singleton;
            return std::addressof(singleton);
        }

        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESDeathEvent* a_event,
            RE::BSTEventSource<RE::TESDeathEvent>*) override
        {
            if (!a_event || !a_event->dead || !a_event->actorDying) {
                return RE::BSEventNotifyControl::kContinue;
            }

            auto* actor = a_event->actorDying->As<RE::Actor>();
            if (!actor || !actor->IsDragon()) {
                return RE::BSEventNotifyControl::kContinue;
            }

            StartWatch(actor);
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    void OnDataLoaded()
    {
        g_target = ResolveTarget();
        ResolveVanillaEffects();

        if (auto* eventSource = RE::ScriptEventSourceHolder::GetSingleton()) {
            eventSource->AddEventSink(DeathEventSink::GetSingleton());
            SKSE::log::info("TESDeathEvent sink registered");
        } else {
            SKSE::log::error("Could not register TESDeathEvent sink");
        }
    }

    void OnSKSEMessage(SKSE::MessagingInterface::Message* a_message)
    {
        if (a_message && a_message->type == SKSE::MessagingInterface::kDataLoaded) {
            OnDataLoaded();
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SetupLog();
    SKSE::Init(a_skse);
    SKSE::log::info("ThorinDragonSoulVisuals v0.2.0 loading");

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        SKSE::log::critical("SKSE messaging interface unavailable");
        return false;
    }

    return messaging->RegisterListener(OnSKSEMessage);
}
