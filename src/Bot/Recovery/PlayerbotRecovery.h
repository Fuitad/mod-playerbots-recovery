/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_PLAYERBOTRECOVERY_H
#define PLAYERBOTS_PLAYERBOTRECOVERY_H

#include <cstdint>

#include "Bot/Recovery/PlayerbotLoopMonitor.h"
#include "Bot/Recovery/PlayerbotObjectiveQuarantine.h"

class PlayerbotAI;

enum class PlayerbotHomebindRecoveryResult : std::uint8_t
{
    Recovered,
    AlreadyAtCurrentHomebind,
    AlreadyPendingHomebind,
    NotInWorld,
    Dead,
    InCombat,
    Rooted,
    InFlight,
    BattlegroundQueue,
    Battleground,
    Arena,
    OnTransport,
    TeleportInProgress,
    InvalidHomebind,
    TeleportRejected
};

[[nodiscard]] std::uint64_t PlayerbotRecoveryObjectiveProgress(PlayerbotAI* botAI, PlayerbotLoopObjectiveKind kind,
                                                               std::uint64_t key);
[[nodiscard]] PlayerbotLoopAnomalySnapshot PlayerbotRecoveryCopyAnomalies(PlayerbotAI* botAI, std::uint64_t nowMs);
[[nodiscard]] PlayerbotObjectiveQuarantineSnapshot PlayerbotRecoveryCopyQuarantine(PlayerbotAI* botAI,
                                                                                   std::uint64_t nowMs);
void PlayerbotRecoveryResetStuckState(PlayerbotAI* botAI);
[[nodiscard]] PlayerbotHomebindRecoveryResult PlayerbotRecoverToHomebind(PlayerbotAI* botAI);

#endif
