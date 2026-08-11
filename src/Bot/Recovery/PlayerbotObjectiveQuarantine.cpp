/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "PlayerbotObjectiveQuarantine.h"

#include <algorithm>
#include <cmath>

namespace
{
uint64 Mix(uint64 hash, uint64 value) { return (hash ^ value) * 1099511628211ULL; }
}  // namespace

uint64 PlayerbotObjectiveQuarantine::QuestKey(uint32 questId, int32 objectiveIndex)
{
    return (static_cast<uint64>(questId) << 32) | static_cast<uint32>(objectiveIndex);
}

uint64 PlayerbotObjectiveQuarantine::PositionKey(uint32 mapId, float x, float y, float z)
{
    uint64 hash = Mix(1469598103934665603ULL, mapId);
    hash = Mix(hash, static_cast<uint64>(std::lround(x / 5.0f)));
    hash = Mix(hash, static_cast<uint64>(std::lround(y / 5.0f)));
    return Mix(hash, static_cast<uint64>(std::lround(z / 5.0f)));
}

void PlayerbotObjectiveQuarantine::Quarantine(PlayerbotLoopObjectiveKind kind, uint64 key, uint64 progress,
                                              uint64 nowMs)
{
    if (kind == PlayerbotLoopObjectiveKind::None)
        return;

    std::lock_guard<std::mutex> lock(mutex);
    auto existing = std::find_if(objectives.begin(), objectives.end(), [kind, key](auto const& objective)
                                 { return objective.kind == kind && objective.key == key; });
    PlayerbotQuarantinedObjective& objective = existing != objectives.end() ? *existing : objectives[next];
    objective = {
        .kind = kind,
        .key = key,
        .failedProgress = progress,
        .failedAtMs = nowMs,
        .expiresAtMs = nowMs + PLAYERBOT_OBJECTIVE_QUARANTINE_DURATION_MS,
    };
    if (existing == objectives.end())
        next = (next + 1) % PLAYERBOT_OBJECTIVE_QUARANTINE_CAPACITY;
}

bool PlayerbotObjectiveQuarantine::IsQuarantined(PlayerbotLoopObjectiveKind kind, uint64 key, uint64 progress,
                                                 uint64 nowMs)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto existing = std::find_if(objectives.begin(), objectives.end(), [kind, key](auto const& objective)
                                 { return objective.kind == kind && objective.key == key; });
    if (existing == objectives.end())
        return false;

    if (nowMs >= existing->expiresAtMs || progress != existing->failedProgress)
    {
        *existing = {};
        return false;
    }

    return true;
}

PlayerbotObjectiveQuarantineSnapshot PlayerbotObjectiveQuarantine::CopySnapshot(uint64 nowMs) const
{
    std::lock_guard<std::mutex> lock(mutex);
    PlayerbotObjectiveQuarantineSnapshot result;
    for (PlayerbotQuarantinedObjective const& objective : objectives)
    {
        if (objective.kind == PlayerbotLoopObjectiveKind::None || nowMs >= objective.expiresAtMs)
            continue;

        result.objectives[result.count++] = objective;
    }
    return result;
}
