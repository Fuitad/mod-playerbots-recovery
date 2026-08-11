/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_PLAYERBOTOBJECTIVEQUARANTINE_H
#define PLAYERBOTS_PLAYERBOTOBJECTIVEQUARANTINE_H

#include <array>
#include <cstddef>
#include <mutex>

#include "Bot/Recovery/PlayerbotLoopMonitor.h"
#include "Define.h"

inline constexpr std::size_t PLAYERBOT_OBJECTIVE_QUARANTINE_CAPACITY = 8;
inline constexpr uint64 PLAYERBOT_OBJECTIVE_QUARANTINE_DURATION_MS = 15 * 60 * 1000;

struct PlayerbotQuarantinedObjective
{
    PlayerbotLoopObjectiveKind kind = PlayerbotLoopObjectiveKind::None;
    uint64 key = 0;
    uint64 failedProgress = 0;
    uint64 failedAtMs = 0;
    uint64 expiresAtMs = 0;

    bool operator==(PlayerbotQuarantinedObjective const&) const = default;
};

struct PlayerbotObjectiveQuarantineSnapshot
{
    std::array<PlayerbotQuarantinedObjective, PLAYERBOT_OBJECTIVE_QUARANTINE_CAPACITY> objectives{};
    std::size_t count = 0;

    bool operator==(PlayerbotObjectiveQuarantineSnapshot const&) const = default;
};

class PlayerbotObjectiveQuarantine
{
public:
    [[nodiscard]] static uint64 QuestKey(uint32 questId, int32 objectiveIndex);
    [[nodiscard]] static uint64 PositionKey(uint32 mapId, float x, float y, float z);
    void Quarantine(PlayerbotLoopObjectiveKind kind, uint64 key, uint64 progress, uint64 nowMs);
    bool IsQuarantined(PlayerbotLoopObjectiveKind kind, uint64 key, uint64 progress, uint64 nowMs);
    [[nodiscard]] PlayerbotObjectiveQuarantineSnapshot CopySnapshot(uint64 nowMs) const;

private:
    std::array<PlayerbotQuarantinedObjective, PLAYERBOT_OBJECTIVE_QUARANTINE_CAPACITY> objectives{};
    std::size_t next = 0;
    mutable std::mutex mutex;
};

#endif
