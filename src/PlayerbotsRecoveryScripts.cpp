/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include <cmath>
#include <ctime>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>

#include "Bot/Engine/AiObjectContext.h"
#include "Bot/Extension/PlayerbotExtension.h"
#include "Bot/PlayerbotAI.h"
#include "Bot/Recovery/PlayerbotRecovery.h"
#include "Bot/Recovery/PlayerbotRecoveryAction.h"
#include "Bot/Recovery/PlayerbotRecoveryState.h"
#include "Engine.h"
#include "LastMovementValue.h"
#include "NamedObjectContext.h"
#include "NewRpgInfo.h"
#include "Player.h"
#include "ServerFacade.h"
#include "Strategy.h"
#include "Timer.h"
#include "TravelMgr.h"
#include "Trigger.h"
#include "Value.h"

namespace
{
constexpr std::uint64_t OBSERVATION_INTERVAL_MS = 5000;

std::shared_ptr<PlayerbotRecoveryState> RecoveryState(PlayerbotAI* botAI)
{
    Player* const bot = botAI ? botAI->GetBot() : nullptr;
    return bot ? GetPlayerbotRecoveryStateStore().Get(bot->GetGUID().GetCounter()) : nullptr;
}

PlayerbotLoopObjectiveKind RecoveryKind(PlayerbotObjectiveKind kind)
{
    switch (kind)
    {
        case PlayerbotObjectiveKind::Quest:
            return PlayerbotLoopObjectiveKind::Quest;
        case PlayerbotObjectiveKind::Grind:
            return PlayerbotLoopObjectiveKind::Grind;
        case PlayerbotObjectiveKind::Profession:
            return PlayerbotLoopObjectiveKind::Profession;
    }
    return PlayerbotLoopObjectiveKind::None;
}

std::uint64_t RecoveryKey(PlayerbotObjective const& objective)
{
    switch (objective.kind)
    {
        case PlayerbotObjectiveKind::Quest:
            return PlayerbotObjectiveQuarantine::QuestKey(objective.subjectId, objective.objectiveIndex);
        case PlayerbotObjectiveKind::Grind:
            return PlayerbotObjectiveQuarantine::PositionKey(objective.mapId, objective.x, objective.y, objective.z);
        case PlayerbotObjectiveKind::Profession:
            return objective.subjectId;
    }
    return 0;
}

void ObserveProgress(PlayerbotAI* botAI, PlayerbotRecoveryState& state, std::uint64_t timestampMs)
{
    Player* const bot = botAI->GetBot();
    PlayerbotLoopProgressObservation observation = {
        .timestampMs = timestampMs,
        .mapId = bot->GetMapId(),
        .x = bot->GetPositionX(),
        .y = bot->GetPositionY(),
        .z = bot->GetPositionZ(),
    };

    auto setObjective = [bot, &observation](PlayerbotLoopObjectiveKind kind, std::uint64_t key, std::string_view title,
                                            WorldPosition const& position)
    {
        observation.objective = {.kind = kind, .key = key, .title = std::string(title)};
        if (position.GetMapId() != bot->GetMapId())
        {
            observation.distanceToObjective = std::numeric_limits<float>::max();
            return;
        }

        float const x = bot->GetPositionX() - position.GetPositionX();
        float const y = bot->GetPositionY() - position.GetPositionY();
        float const z = bot->GetPositionZ() - position.GetPositionZ();
        observation.distanceToObjective = std::sqrt(x * x + y * y + z * z);
    };

    std::visit(
        [&setObjective](auto const& objective)
        {
            using Objective = std::decay_t<decltype(objective)>;
            if constexpr (std::is_same_v<Objective, NewRpgInfo::GoGrind>)
            {
                std::uint64_t const key = PlayerbotObjectiveQuarantine::PositionKey(
                    objective.pos.GetMapId(), objective.pos.GetPositionX(), objective.pos.GetPositionY(),
                    objective.pos.GetPositionZ());
                setObjective(PlayerbotLoopObjectiveKind::Grind, key, "grind", objective.pos);
            }
            else if constexpr (std::is_same_v<Objective, NewRpgInfo::DoQuest>)
            {
                std::uint64_t const key =
                    PlayerbotObjectiveQuarantine::QuestKey(objective.questId, objective.objectiveIdx);
                std::string const title = objective.quest ? objective.quest->GetTitle() : "quest";
                setObjective(PlayerbotLoopObjectiveKind::Quest, key, title, objective.pos);
            }
        },
        botAI->rpgInfo.data);

    observation.progress =
        PlayerbotRecoveryObjectiveProgress(botAI, observation.objective.kind, observation.objective.key);
    state.loopMonitor.ObserveProgress(observation);
}

class PlayerbotsRecoveryActionContext final : public NamedObjectContext<Action>
{
public:
    PlayerbotsRecoveryActionContext()
    {
        creators["recover stuck movement"] = [](PlayerbotAI* botAI) { return new PlayerbotRecoveryAction(botAI); };
    }
};

class PlayerbotsMovementStuckTrigger final : public Trigger
{
public:
    explicit PlayerbotsMovementStuckTrigger(PlayerbotAI* botAI) : Trigger(botAI, "playerbots movement stuck", 5) {}

    bool IsActive() override
    {
        if (IsRealPlayer(botAI->GetMaster()) || !botAI->AllowActivity(ALL_ACTIVITY))
            return false;

        WorldPosition botPosition(bot);
        LastMovement const& lastMovement = context->GetValue<LastMovement&>("last movement")->Get();
        if (!lastMovement.msTime || lastMovement.lastMoveToMapId != bot->GetMapId() ||
            lastMovement.msTime + sPlayerbotAIConfig.maxWaitForMove >= getMSTime() ||
            botPosition.fDist(lastMovement.lastMoveShort) <= sPlayerbotAIConfig.tooCloseDistance)
            return false;

        auto* position = dynamic_cast<LogCalculatedValue<WorldPosition>*>(context->GetUntypedValue("current position"));
        if (!position)
            return false;

        if (position->LastChangeDelay() > 5 * MINUTE)
            return true;

        bool stationaryForTenMinutes = false;
        for (auto const& recordedPosition : position->ValueLog())
        {
            std::time_t const elapsed = std::time(nullptr) - recordedPosition.second;
            if (elapsed <= 10 * MINUTE)
                continue;
            if (botPosition.fDist(recordedPosition.first) > 50.0f)
                return false;
            stationaryForTenMinutes = true;
        }
        return stationaryForTenMinutes;
    }
};

class PlayerbotsCombatGroupStuckTrigger final : public Trigger
{
public:
    explicit PlayerbotsCombatGroupStuckTrigger(PlayerbotAI* botAI) : Trigger(botAI, "playerbots combat group stuck", 5)
    {
    }

    bool IsActive() override
    {
        if (!bot->IsInCombat() || !botAI->HasGameClientMaster() || !botAI->AllowActivity(ALL_ACTIVITY))
            return false;

        Player* const groupLeader = botAI->GetGroupLeader();
        if (!groupLeader || !groupLeader->IsInWorld() || !groupLeader->IsAlive() || groupLeader->IsInCombat() ||
            groupLeader->GetMapId() != bot->GetMapId() ||
            ServerFacade::instance().GetDistance2d(bot, groupLeader) <= sPlayerbotAIConfig.sightDistance)
            return false;

        auto* position = dynamic_cast<LogCalculatedValue<WorldPosition>*>(context->GetUntypedValue("current position"));
        auto* combat = dynamic_cast<MemoryCalculatedValue<bool>*>(context->GetUntypedValue("combat::self target"));
        return position && combat && position->LastChangeDelay() > 5 * MINUTE && combat->LastChangeDelay() > 5 * MINUTE;
    }
};

class PlayerbotsRecoveryTriggerContext final : public NamedObjectContext<Trigger>
{
public:
    PlayerbotsRecoveryTriggerContext()
    {
        creators["playerbots movement stuck"] = [](PlayerbotAI* botAI)
        { return new PlayerbotsMovementStuckTrigger(botAI); };
        creators["playerbots combat group stuck"] = [](PlayerbotAI* botAI)
        { return new PlayerbotsCombatGroupStuckTrigger(botAI); };
    }
};

class PlayerbotsRecoveryStrategy final : public Strategy
{
public:
    explicit PlayerbotsRecoveryStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

    std::string const getName() override { return "playerbots recovery"; }
    uint32 GetType() const override { return STRATEGY_TYPE_NONCOMBAT; }

    void InitTriggers(std::vector<TriggerNode*>& triggers) override
    {
        triggers.push_back(
            new TriggerNode("playerbots movement stuck", {NextAction("recover stuck movement", 100.0f)}));
    }
};

class PlayerbotsCombatRecoveryStrategy final : public Strategy
{
public:
    explicit PlayerbotsCombatRecoveryStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

    std::string const getName() override { return "playerbots combat recovery"; }
    uint32 GetType() const override { return STRATEGY_TYPE_COMBAT; }

    void InitTriggers(std::vector<TriggerNode*>& triggers) override
    {
        triggers.push_back(
            new TriggerNode("playerbots combat group stuck", {NextAction("drop target", ACTION_EMERGENCY)}));
    }
};

class PlayerbotsRecoveryStrategyContext final : public NamedObjectContext<Strategy>
{
public:
    PlayerbotsRecoveryStrategyContext()
    {
        creators["playerbots recovery"] = [](PlayerbotAI* botAI) { return new PlayerbotsRecoveryStrategy(botAI); };
        creators["playerbots combat recovery"] = [](PlayerbotAI* botAI)
        { return new PlayerbotsCombatRecoveryStrategy(botAI); };
    }
};

class PlayerbotsRecoveryExtension final : public PlayerbotExtension
{
public:
    void AddActionContexts(SharedNamedObjectContextList<Action>& contexts) override
    {
        contexts.Add(new PlayerbotsRecoveryActionContext());
    }

    void AddTriggerContexts(SharedNamedObjectContextList<Trigger>& contexts) override
    {
        contexts.Add(new PlayerbotsRecoveryTriggerContext());
    }

    void AddStrategyContexts(SharedNamedObjectContextList<Strategy>& contexts) override
    {
        contexts.Add(new PlayerbotsRecoveryStrategyContext());
    }

    void AddDefaultNonCombatStrategies(Player*, PlayerbotAI*, Engine& engine) override
    {
        engine.addStrategy("playerbots recovery", false);
    }

    void AddDefaultCombatStrategies(Player*, PlayerbotAI*, Engine& engine) override
    {
        engine.addStrategy("playerbots combat recovery", false);
    }

    void OnBotUpdate(PlayerbotAI* botAI, PlayerbotAIUpdate const&) override
    {
        std::shared_ptr<PlayerbotRecoveryState> const state = RecoveryState(botAI);
        if (!state)
            return;

        std::uint64_t const nowMs = GetTimeMS().count();
        std::uint64_t const next = state->nextObservationMs.load(std::memory_order_relaxed);
        if (nowMs < next)
            return;
        state->nextObservationMs.store(nowMs + OBSERVATION_INTERVAL_MS, std::memory_order_relaxed);
        ObserveProgress(botAI, *state, nowMs);
    }

    void OnActionExecuted(PlayerbotAI* botAI, std::string_view name, bool success, std::uint64_t timestampMs) override
    {
        std::shared_ptr<PlayerbotRecoveryState> const state = RecoveryState(botAI);
        if (state)
            state->loopMonitor.RecordActionAttempt(name, success, timestampMs);
    }

    void OnBotDeath(PlayerbotAI* botAI, std::uint64_t timestampMs) override
    {
        std::shared_ptr<PlayerbotRecoveryState> const state = RecoveryState(botAI);
        if (state)
            state->loopMonitor.RecordDeath(timestampMs);
    }

    void OnBotRemoved(PlayerbotAI* botAI) override
    {
        Player* const bot = botAI ? botAI->GetBot() : nullptr;
        if (bot)
            GetPlayerbotRecoveryStateStore().Erase(bot->GetGUID().GetCounter());
    }

    void OnBotPurge(std::vector<std::uint32_t> const& botGuids) override
    {
        for (std::uint32_t guid : botGuids)
            GetPlayerbotRecoveryStateStore().Erase(guid);
    }

    bool IsObjectiveAvailable(PlayerbotAI* botAI, PlayerbotObjective const& objective,
                              std::uint64_t timestampMs) override
    {
        std::shared_ptr<PlayerbotRecoveryState> const state = RecoveryState(botAI);
        if (!state)
            return true;

        PlayerbotLoopObjectiveKind const kind = RecoveryKind(objective.kind);
        std::uint64_t const key = RecoveryKey(objective);
        std::uint64_t const progress = PlayerbotRecoveryObjectiveProgress(botAI, kind, key);
        return !state->objectiveQuarantine.IsQuarantined(kind, key, progress, timestampMs);
    }
};
}  // namespace

void AddPlayerbotsRecoveryScripts()
{
    static PlayerbotsRecoveryExtension extension;
    GetPlayerbotExtensionRegistry().Register(extension);
}
