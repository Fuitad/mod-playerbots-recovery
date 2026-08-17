/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "PlayerbotRecoveryAction.h"

#include "Bot/PlayerbotAI.h"
#include "Bot/Recovery/PlayerbotRecovery.h"
#include "Player.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "SharedDefines.h"

bool PlayerbotRecoveryAction::isPossible()
{
    return IsAutonomousBot() && bot->IsInWorld() && !bot->isDead() && !bot->IsInCombat() && !bot->IsBeingTeleported() &&
           !bot->IsRooted() && !bot->HasUnitState(UNIT_STATE_IN_FLIGHT) && !bot->InBattlegroundQueue() &&
           !bot->InBattleground() && !bot->InArena() && !bot->HasUnitMovementFlag(MOVEMENTFLAG_ONTRANSPORT);
}

bool PlayerbotRecoveryAction::Execute([[maybe_unused]] Event event)
{
    std::uint32_t const sourceMapId = bot->GetMapId();
    float const sourceX = bot->GetPositionX();
    float const sourceY = bot->GetPositionY();
    float const sourceZ = bot->GetPositionZ();

    PlayerbotHomebindRecoveryResult const result = PlayerbotRecoverToHomebind(botAI);
    if (result == PlayerbotHomebindRecoveryResult::Recovered)
    {
        LOG_INFO("playerbots",
                 "Stuck movement recovery accepted homebind teleport for bot {} from Map: {} ({}, {}, {}) to Map: "
                 "{} ({}, {}, {})",
                 bot->GetName(), sourceMapId, sourceX, sourceY, sourceZ, bot->m_homebindMapId, bot->m_homebindX,
                 bot->m_homebindY, bot->m_homebindZ);
        return true;
    }

    if (result == PlayerbotHomebindRecoveryResult::AlreadyAtCurrentHomebind ||
        result == PlayerbotHomebindRecoveryResult::AlreadyPendingHomebind)
        return true;

    if (result != PlayerbotHomebindRecoveryResult::InvalidHomebind &&
        result != PlayerbotHomebindRecoveryResult::TeleportRejected)
        return false;

    if (result == PlayerbotHomebindRecoveryResult::InvalidHomebind)
        PlayerbotRecoveryResetStuckState(botAI);

    LOG_WARN("playerbots",
             "Homebind teleport failed for stuck bot {} from Map: {} ({}, {}, {}). Requesting validated random "
             "relocation instead",
             bot->GetName(), sourceMapId, sourceX, sourceY, sourceZ);
    RandomRelocate();
    return true;
}

bool PlayerbotRecoveryAction::IsAutonomousBot() const
{
    return sRandomPlayerbotMgr.IsRandomBot(bot) && !IsRealPlayer(botAI->GetMaster()) &&
           botAI->AllowActivity(ALL_ACTIVITY);
}

void PlayerbotRecoveryAction::RandomRelocate() { sRandomPlayerbotMgr.RandomTeleportGrindForLevel(bot); }
