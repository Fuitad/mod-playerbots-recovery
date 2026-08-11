/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_PLAYERBOTLOOPMONITOR_H
#define PLAYERBOTS_PLAYERBOTLOOPMONITOR_H

#include <array>
#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>

#include "Define.h"

inline constexpr std::size_t PLAYERBOT_LOOP_MOVEMENT_CAPACITY = 32;
inline constexpr std::size_t PLAYERBOT_LOOP_ACTION_CAPACITY = 64;
inline constexpr std::size_t PLAYERBOT_LOOP_EVENT_CAPACITY = 16;
inline constexpr std::size_t PLAYERBOT_LOOP_ANOMALY_CAPACITY = 5;
inline constexpr std::size_t PLAYERBOT_LOOP_TEXT_CAPACITY = 128;

enum class PlayerbotLoopClassifier : uint8
{
    StationaryMovement,
    MovementOscillation,
    RepeatedAction,
    DeathRelapse,
    RecoveryRelapse
};

enum class PlayerbotLoopObjectiveKind : uint8
{
    None,
    Quest,
    Grind,
    Profession
};

struct PlayerbotLoopObjective
{
    PlayerbotLoopObjectiveKind kind = PlayerbotLoopObjectiveKind::None;
    uint64 key = 0;
    std::string title;
};

struct PlayerbotLoopProgressObservation
{
    uint64 timestampMs = 0;
    uint32 mapId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float distanceToObjective = 0.0f;
    uint64 progress = 0;
    PlayerbotLoopObjective objective;
};

struct PlayerbotLoopAnomaly
{
    PlayerbotLoopClassifier classifier = PlayerbotLoopClassifier::StationaryMovement;
    PlayerbotLoopObjectiveKind objectiveKind = PlayerbotLoopObjectiveKind::None;
    uint64 objectiveKey = 0;
    std::array<char, PLAYERBOT_LOOP_TEXT_CAPACITY> objectiveTitle{};
    std::array<char, PLAYERBOT_LOOP_TEXT_CAPACITY> actionName{};
    uint64 firstEvidenceMs = 0;
    uint64 lastEvidenceMs = 0;
    float progressDelta = 0.0f;
    uint32 evidenceCount = 0;
    uint32 deathCount = 0;
    uint32 recoveryCount = 0;

    bool operator==(PlayerbotLoopAnomaly const&) const = default;
};

struct PlayerbotLoopAnomalySnapshot
{
    std::array<PlayerbotLoopAnomaly, PLAYERBOT_LOOP_ANOMALY_CAPACITY> anomalies{};
    std::size_t count = 0;

    bool operator==(PlayerbotLoopAnomalySnapshot const&) const = default;
};

class PlayerbotLoopMonitor
{
public:
    void ObserveProgress(PlayerbotLoopProgressObservation const& observation);
    void RecordActionAttempt(std::string_view actionName, bool success, uint64 timestampMs);
    void RecordDeath(uint64 timestampMs);
    void RecordRecovery(uint64 timestampMs);
    [[nodiscard]] PlayerbotLoopAnomalySnapshot CopyAnomalies(uint64 nowMs) const;

private:
    struct StoredObjective
    {
        PlayerbotLoopObjectiveKind kind = PlayerbotLoopObjectiveKind::None;
        uint64 key = 0;
        std::array<char, PLAYERBOT_LOOP_TEXT_CAPACITY> title{};
    };

    struct MovementSample
    {
        uint64 timestampMs = 0;
        uint32 mapId = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float distanceToObjective = 0.0f;
        uint64 progress = 0;
        StoredObjective objective;
    };

    struct ActionSample
    {
        uint64 timestampMs = 0;
        bool success = false;
        uint64 progress = 0;
        StoredObjective objective;
        std::array<char, PLAYERBOT_LOOP_TEXT_CAPACITY> actionName{};
    };

    template <typename T, std::size_t Capacity>
    struct Ring
    {
        std::array<T, Capacity> values{};
        std::size_t next = 0;
        std::size_t count = 0;
    };

    Ring<MovementSample, PLAYERBOT_LOOP_MOVEMENT_CAPACITY> movement;
    Ring<ActionSample, PLAYERBOT_LOOP_ACTION_CAPACITY> actions;
    Ring<uint64, PLAYERBOT_LOOP_EVENT_CAPACITY> deaths;
    Ring<uint64, PLAYERBOT_LOOP_EVENT_CAPACITY> recoveries;
    mutable std::mutex mutex;
};

#endif
