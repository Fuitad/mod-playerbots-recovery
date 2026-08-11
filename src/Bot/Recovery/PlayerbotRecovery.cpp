/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "PlayerbotRecovery.h"

#include <G3D/g3dmath.h>

#include <cmath>
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>
#include <type_traits>

#include "BattlegroundMgr.h"
#include "Bot/Engine/AiObjectContext.h"
#include "Bot/PlayerbotAI.h"
#include "Bot/Recovery/PlayerbotRecoveryState.h"
#include "LastMovementValue.h"
#include "MapMgr.h"
#include "NewRpgInfo.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Timer.h"
#include "TravelMgr.h"

namespace
{
std::uint64_t MixObjectiveProgress(std::uint64_t hash, std::uint64_t value)
{
    return (hash ^ value) * 1099511628211ULL;
}

std::shared_ptr<PlayerbotRecoveryState> RecoveryState(PlayerbotAI* botAI)
{
    Player* const bot = botAI ? botAI->GetBot() : nullptr;
    return bot ? GetPlayerbotRecoveryStateStore().Get(bot->GetGUID().GetCounter()) : nullptr;
}

bool IsHomebindPosition(Player const* bot, std::uint32_t mapId, float x, float y, float z)
{
    return mapId == bot->m_homebindMapId && G3D::fuzzyEq(x, bot->m_homebindX) && G3D::fuzzyEq(y, bot->m_homebindY) &&
           G3D::fuzzyEq(z, bot->m_homebindZ);
}

bool IsValidHomebind(Player const* bot)
{
    return MapMgr::IsValidMapCoord(bot->m_homebindMapId, bot->m_homebindX, bot->m_homebindY, bot->m_homebindZ);
}

void QuarantineCurrentObjective(PlayerbotAI* botAI, PlayerbotRecoveryState& state, std::uint64_t nowMs)
{
    std::visit(
        [botAI, &state, nowMs](auto const& objective)
        {
            using Objective = std::decay_t<decltype(objective)>;
            if constexpr (std::is_same_v<Objective, NewRpgInfo::GoGrind>)
            {
                WorldPosition const& position = objective.pos;
                std::uint64_t const key = PlayerbotObjectiveQuarantine::PositionKey(
                    position.GetMapId(), position.GetPositionX(), position.GetPositionY(), position.GetPositionZ());
                std::uint64_t const progress =
                    PlayerbotRecoveryObjectiveProgress(botAI, PlayerbotLoopObjectiveKind::Grind, key);
                state.objectiveQuarantine.Quarantine(PlayerbotLoopObjectiveKind::Grind, key, progress, nowMs);
            }
            else if constexpr (std::is_same_v<Objective, NewRpgInfo::DoQuest>)
            {
                std::uint64_t const key =
                    PlayerbotObjectiveQuarantine::QuestKey(objective.questId, objective.objectiveIdx);
                std::uint64_t const progress =
                    PlayerbotRecoveryObjectiveProgress(botAI, PlayerbotLoopObjectiveKind::Quest, key);
                state.objectiveQuarantine.Quarantine(PlayerbotLoopObjectiveKind::Quest, key, progress, nowMs);
            }
        },
        botAI->rpgInfo.data);
}
}  // namespace

std::uint64_t PlayerbotRecoveryObjectiveProgress(PlayerbotAI* botAI, PlayerbotLoopObjectiveKind kind, std::uint64_t key)
{
    Player* const bot = botAI ? botAI->GetBot() : nullptr;
    if (!bot)
        return 0;

    std::uint64_t progress = MixObjectiveProgress(1469598103934665603ULL, bot->GetLevel());
    progress = MixObjectiveProgress(progress, bot->GetUInt32Value(PLAYER_XP));

    if (kind == PlayerbotLoopObjectiveKind::Quest)
    {
        std::uint32_t const questId = static_cast<std::uint32_t>(key >> 32);
        auto const status = bot->getQuestStatusMap().find(questId);
        if (status == bot->getQuestStatusMap().end())
            return MixObjectiveProgress(progress, questId);

        std::uint64_t questProgress = MixObjectiveProgress(1469598103934665603ULL, questId);
        questProgress = MixObjectiveProgress(questProgress, status->second.Status);
        questProgress = std::accumulate(
            std::begin(status->second.CreatureOrGOCount), std::end(status->second.CreatureOrGOCount), questProgress,
            [](std::uint64_t hash, std::uint16_t count) { return MixObjectiveProgress(hash, count); });
        questProgress =
            std::accumulate(std::begin(status->second.ItemCount), std::end(status->second.ItemCount), questProgress,
                            [](std::uint64_t hash, std::uint16_t count) { return MixObjectiveProgress(hash, count); });
        return MixObjectiveProgress(progress, questProgress);
    }

    if (kind != PlayerbotLoopObjectiveKind::Profession)
        return progress;

    std::uint64_t skillAggregate = 0;
    std::uint64_t skillCount = 0;
    for (auto const& [skillId, status] : bot->GetSkillStatusMap())
    {
        if (status.uState == SKILL_DELETED)
            continue;

        std::uint64_t skillProgress = MixObjectiveProgress(1469598103934665603ULL, skillId);
        skillProgress = MixObjectiveProgress(skillProgress, bot->GetSkillValue(skillId));
        skillAggregate ^= skillProgress;
        ++skillCount;
    }
    progress = MixObjectiveProgress(progress, skillAggregate);
    return MixObjectiveProgress(progress, skillCount);
}

PlayerbotLoopAnomalySnapshot PlayerbotRecoveryCopyAnomalies(PlayerbotAI* botAI, std::uint64_t nowMs)
{
    std::shared_ptr<PlayerbotRecoveryState> const state = RecoveryState(botAI);
    return state ? state->loopMonitor.CopyAnomalies(nowMs) : PlayerbotLoopAnomalySnapshot{};
}

PlayerbotObjectiveQuarantineSnapshot PlayerbotRecoveryCopyQuarantine(PlayerbotAI* botAI, std::uint64_t nowMs)
{
    std::shared_ptr<PlayerbotRecoveryState> const state = RecoveryState(botAI);
    return state ? state->objectiveQuarantine.CopySnapshot(nowMs) : PlayerbotObjectiveQuarantineSnapshot{};
}

void PlayerbotRecoveryResetStuckState(PlayerbotAI* botAI)
{
    std::shared_ptr<PlayerbotRecoveryState> const state = RecoveryState(botAI);
    if (!botAI || !botAI->GetBot() || !state)
        return;

    std::uint64_t const nowMs = GetTimeMS().count();
    QuarantineCurrentObjective(botAI, *state, nowMs);
    state->loopMonitor.RecordRecovery(nowMs);
    botAI->Reset(true);
    botAI->GetAiObjectContext()->GetValue<WorldPosition>("current position")->Reset();
    botAI->GetAiObjectContext()->GetValue<LastMovement&>("last movement")->Get().clear();
    LastMovement& lastTaxi = botAI->GetAiObjectContext()->GetValue<LastMovement&>("last taxi")->Get();
    lastTaxi.clear();
    lastTaxi.taxiNodes.clear();
    lastTaxi.taxiMaster = ObjectGuid::Empty;
}

PlayerbotHomebindRecoveryResult PlayerbotRecoverToHomebind(PlayerbotAI* botAI)
{
    Player* const bot = botAI ? botAI->GetBot() : nullptr;
    if (!bot || !bot->IsInWorld())
        return PlayerbotHomebindRecoveryResult::NotInWorld;
    if (bot->isDead())
        return PlayerbotHomebindRecoveryResult::Dead;
    if (bot->IsInCombat())
        return PlayerbotHomebindRecoveryResult::InCombat;
    if (bot->IsRooted())
        return PlayerbotHomebindRecoveryResult::Rooted;
    if (bot->HasUnitState(UNIT_STATE_IN_FLIGHT))
        return PlayerbotHomebindRecoveryResult::InFlight;
    if (bot->InBattlegroundQueue())
        return PlayerbotHomebindRecoveryResult::BattlegroundQueue;
    if (bot->InBattleground() && BattlegroundMgr::IsArenaType(bot->GetBattlegroundTypeId()))
        return PlayerbotHomebindRecoveryResult::Arena;
    if (bot->InBattleground())
        return PlayerbotHomebindRecoveryResult::Battleground;
    if (bot->HasUnitMovementFlag(MOVEMENTFLAG_ONTRANSPORT))
        return PlayerbotHomebindRecoveryResult::OnTransport;

    if (IsHomebindPosition(bot, bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()))
        return PlayerbotHomebindRecoveryResult::AlreadyAtCurrentHomebind;

    if (bot->IsBeingTeleported())
    {
        WorldLocation const& destination = bot->GetTeleportDest();
        if (IsHomebindPosition(bot, destination.GetMapId(), destination.GetPositionX(), destination.GetPositionY(),
                               destination.GetPositionZ()))
            return PlayerbotHomebindRecoveryResult::AlreadyPendingHomebind;
        return PlayerbotHomebindRecoveryResult::TeleportInProgress;
    }

    if (!IsValidHomebind(bot))
        return PlayerbotHomebindRecoveryResult::InvalidHomebind;

    PlayerbotRecoveryResetStuckState(botAI);
    bot->m_taxi.ClearTaxiDestinations();
    bool const accepted = bot->TeleportTo(bot->m_homebindMapId, bot->m_homebindX, bot->m_homebindY, bot->m_homebindZ,
                                          bot->GetOrientation());
    return accepted ? PlayerbotHomebindRecoveryResult::Recovered : PlayerbotHomebindRecoveryResult::TeleportRejected;
}
