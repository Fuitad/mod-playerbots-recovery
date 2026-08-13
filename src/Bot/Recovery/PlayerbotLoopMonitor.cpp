/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "PlayerbotLoopMonitor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
constexpr uint64 MOVEMENT_MINIMUM_SPAN_MS = 90 * 1000;
constexpr uint64 MOVEMENT_WINDOW_MS = 120 * 1000;
constexpr uint64 ACTION_WINDOW_MS = 60 * 1000;
constexpr uint64 ACTION_MINIMUM_SPAN_MS = 30 * 1000;
constexpr uint64 DEATH_WINDOW_MS = 15 * 60 * 1000;
constexpr uint64 RECOVERY_WINDOW_MS = 10 * 60 * 1000;
constexpr float MINIMUM_OBJECTIVE_PROGRESS = 5.0f;
constexpr float MAXIMUM_STATIONARY_TRAVEL = 5.0f;
constexpr float MINIMUM_OSCILLATION_TRAVEL = 40.0f;
constexpr float MAXIMUM_OSCILLATION_DISPLACEMENT = 10.0f;
constexpr uint32 REPEATED_ACTION_THRESHOLD = 8;
constexpr uint32 DEATH_RELAPSE_THRESHOLD = 3;
constexpr uint32 RECOVERY_RELAPSE_THRESHOLD = 2;

template <std::size_t Capacity>
void CopyText(std::array<char, Capacity>& destination, std::string_view source)
{
    std::size_t const length = std::min(source.size(), Capacity - 1);
    std::copy_n(source.data(), length, destination.data());
    destination[length] = '\0';
}

template <std::size_t Capacity>
float Distance(std::array<float, Capacity> const& first, std::array<float, Capacity> const& second)
{
    float squaredDistance = 0.0f;
    for (std::size_t index = 0; index < Capacity; ++index)
    {
        float const difference = first[index] - second[index];
        squaredDistance += difference * difference;
    }
    return std::sqrt(squaredDistance);
}

bool IsInWindow(uint64 timestampMs, uint64 nowMs, uint64 windowMs)
{
    return timestampMs <= nowMs && nowMs - timestampMs <= windowMs;
}
}  // namespace

void PlayerbotLoopMonitor::ObserveProgress(PlayerbotLoopProgressObservation const& observation)
{
    std::lock_guard<std::mutex> lock(mutex);
    MovementSample& sample = movement.values[movement.next];
    sample = {
        .timestampMs = observation.timestampMs,
        .mapId = observation.mapId,
        .x = observation.x,
        .y = observation.y,
        .z = observation.z,
        .distanceToObjective = observation.distanceToObjective,
        .progress = observation.progress,
        .objective = {.kind = observation.objective.kind, .key = observation.objective.key},
    };
    CopyText(sample.objective.title, observation.objective.title);
    movement.next = (movement.next + 1) % PLAYERBOT_LOOP_MOVEMENT_CAPACITY;
    movement.count = std::min(movement.count + 1, PLAYERBOT_LOOP_MOVEMENT_CAPACITY);
}

void PlayerbotLoopMonitor::RecordActionAttempt(std::string_view actionName, bool success, uint64 timestampMs)
{
    std::lock_guard<std::mutex> lock(mutex);
    ActionSample& sample = actions.values[actions.next];
    sample = ActionSample{};
    sample.timestampMs = timestampMs;
    sample.success = success;
    if (movement.count)
    {
        MovementSample const& latestMovement =
            movement.values[(movement.next + PLAYERBOT_LOOP_MOVEMENT_CAPACITY - 1) % PLAYERBOT_LOOP_MOVEMENT_CAPACITY];
        sample.progress = latestMovement.progress;
        sample.objective = latestMovement.objective;
    }
    CopyText(sample.actionName, actionName);
    actions.next = (actions.next + 1) % PLAYERBOT_LOOP_ACTION_CAPACITY;
    actions.count = std::min(actions.count + 1, PLAYERBOT_LOOP_ACTION_CAPACITY);
}

void PlayerbotLoopMonitor::RecordDeath(uint64 timestampMs)
{
    std::lock_guard<std::mutex> lock(mutex);
    deaths.values[deaths.next] = timestampMs;
    deaths.next = (deaths.next + 1) % PLAYERBOT_LOOP_EVENT_CAPACITY;
    deaths.count = std::min(deaths.count + 1, PLAYERBOT_LOOP_EVENT_CAPACITY);
}

void PlayerbotLoopMonitor::RecordRecovery(uint64 timestampMs)
{
    std::lock_guard<std::mutex> lock(mutex);
    recoveries.values[recoveries.next] = timestampMs;
    recoveries.next = (recoveries.next + 1) % PLAYERBOT_LOOP_EVENT_CAPACITY;
    recoveries.count = std::min(recoveries.count + 1, PLAYERBOT_LOOP_EVENT_CAPACITY);
}

PlayerbotLoopAnomalySnapshot PlayerbotLoopMonitor::CopyAnomalies(uint64 nowMs) const
{
    std::lock_guard<std::mutex> lock(mutex);
    PlayerbotLoopAnomalySnapshot result;

    auto countRecentEvents = [nowMs](auto const& events, uint64 windowMs)
    {
        uint32 count = 0;
        std::size_t const first = events.count == PLAYERBOT_LOOP_EVENT_CAPACITY ? events.next : 0;
        for (std::size_t offset = 0; offset < events.count; ++offset)
            if (IsInWindow(events.values[(first + offset) % PLAYERBOT_LOOP_EVENT_CAPACITY], nowMs, windowMs))
                ++count;
        return count;
    };
    uint32 const recentDeathCount = countRecentEvents(deaths, DEATH_WINDOW_MS);
    uint32 const recentRecoveryCount = countRecentEvents(recoveries, RECOVERY_WINDOW_MS);
    ActionSample const* latestAction =
        actions.count
            ? &actions.values[(actions.next + PLAYERBOT_LOOP_ACTION_CAPACITY - 1) % PLAYERBOT_LOOP_ACTION_CAPACITY]
            : nullptr;

    auto appendAnomaly = [&result, latestAction, recentDeathCount, recentRecoveryCount](PlayerbotLoopAnomaly anomaly)
    {
        if (result.count < PLAYERBOT_LOOP_ANOMALY_CAPACITY)
        {
            if (latestAction && !anomaly.actionName[0])
                anomaly.actionName = latestAction->actionName;
            anomaly.deathCount = recentDeathCount;
            anomaly.recoveryCount = recentRecoveryCount;
            result.anomalies[result.count++] = anomaly;
        }
    };

    MovementSample const* latestMovement = nullptr;
    std::array<MovementSample const*, PLAYERBOT_LOOP_MOVEMENT_CAPACITY> movementEvidence{};
    std::size_t movementEvidenceCount = 0;
    if (movement.count)
    {
        latestMovement =
            &movement.values[(movement.next + PLAYERBOT_LOOP_MOVEMENT_CAPACITY - 1) % PLAYERBOT_LOOP_MOVEMENT_CAPACITY];
        std::size_t const first = movement.count == PLAYERBOT_LOOP_MOVEMENT_CAPACITY ? movement.next : 0;
        for (std::size_t offset = 0; offset < movement.count; ++offset)
        {
            MovementSample const& sample = movement.values[(first + offset) % PLAYERBOT_LOOP_MOVEMENT_CAPACITY];
            if (sample.mapId == latestMovement->mapId && sample.objective.kind == latestMovement->objective.kind &&
                sample.objective.key == latestMovement->objective.key &&
                IsInWindow(sample.timestampMs, nowMs, MOVEMENT_WINDOW_MS))
                movementEvidence[movementEvidenceCount++] = &sample;
        }
    }

    if (movementEvidenceCount >= 2)
    {
        MovementSample const& first = *movementEvidence[0];
        MovementSample const& last = *movementEvidence[movementEvidenceCount - 1];
        uint64 const evidenceSpan = last.timestampMs - first.timestampMs;
        float const objectiveProgress = first.distanceToObjective - last.distanceToObjective;
        float pathDistance = 0.0f;
        for (std::size_t index = 1; index < movementEvidenceCount; ++index)
        {
            MovementSample const& previous = *movementEvidence[index - 1];
            MovementSample const& current = *movementEvidence[index];
            pathDistance +=
                Distance(std::array{previous.x, previous.y, previous.z}, std::array{current.x, current.y, current.z});
        }
        float const displacement = Distance(std::array{first.x, first.y, first.z}, std::array{last.x, last.y, last.z});
        bool const noProgress = first.progress == last.progress && objectiveProgress < MINIMUM_OBJECTIVE_PROGRESS;

        auto movementAnomaly = [&](PlayerbotLoopClassifier classifier)
        {
            PlayerbotLoopAnomaly anomaly = {
                .classifier = classifier,
                .objectiveKind = last.objective.kind,
                .objectiveKey = last.objective.key,
                .firstEvidenceMs = first.timestampMs,
                .lastEvidenceMs = last.timestampMs,
                .progressDelta = objectiveProgress,
                .evidenceCount = static_cast<uint32>(movementEvidenceCount),
            };
            anomaly.objectiveTitle = last.objective.title;
            appendAnomaly(anomaly);
        };

        if (latestMovement->objective.kind != PlayerbotLoopObjectiveKind::None &&
            evidenceSpan >= MOVEMENT_MINIMUM_SPAN_MS && noProgress)
        {
            if (pathDistance >= MINIMUM_OSCILLATION_TRAVEL && displacement <= MAXIMUM_OSCILLATION_DISPLACEMENT)
                movementAnomaly(PlayerbotLoopClassifier::MovementOscillation);
            else if (pathDistance <= MAXIMUM_STATIONARY_TRAVEL)
                movementAnomaly(PlayerbotLoopClassifier::StationaryMovement);
        }
    }

    if (actions.count)
    {
        ActionSample const& latest = *latestAction;
        std::size_t const first = actions.count == PLAYERBOT_LOOP_ACTION_CAPACITY ? actions.next : 0;
        uint32 repeatedCount = 0;
        uint64 firstEvidenceMs = latest.timestampMs;
        for (std::size_t offset = 0; offset < actions.count; ++offset)
        {
            ActionSample const& sample = actions.values[(first + offset) % PLAYERBOT_LOOP_ACTION_CAPACITY];
            if (sample.progress == latest.progress && sample.objective.kind == latest.objective.kind &&
                sample.objective.key == latest.objective.key &&
                std::strcmp(sample.actionName.data(), latest.actionName.data()) == 0 &&
                IsInWindow(sample.timestampMs, nowMs, ACTION_WINDOW_MS))
            {
                if (!repeatedCount)
                    firstEvidenceMs = sample.timestampMs;
                ++repeatedCount;
            }
        }

        if (repeatedCount >= REPEATED_ACTION_THRESHOLD &&
            latest.timestampMs - firstEvidenceMs >= ACTION_MINIMUM_SPAN_MS)
        {
            PlayerbotLoopAnomaly anomaly = {
                .classifier = PlayerbotLoopClassifier::RepeatedAction,
                .objectiveKind = latest.objective.kind,
                .objectiveKey = latest.objective.key,
                .firstEvidenceMs = firstEvidenceMs,
                .lastEvidenceMs = latest.timestampMs,
                .evidenceCount = repeatedCount,
            };
            anomaly.objectiveTitle = latest.objective.title;
            anomaly.actionName = latest.actionName;
            appendAnomaly(anomaly);
        }
    }

    auto appendRelapse = [&](auto const& events, uint64 windowMs, uint32 threshold, PlayerbotLoopClassifier classifier)
    {
        std::size_t const first = events.count == PLAYERBOT_LOOP_EVENT_CAPACITY ? events.next : 0;
        uint32 count = 0;
        uint64 firstEvidenceMs = 0;
        uint64 lastEvidenceMs = 0;
        for (std::size_t offset = 0; offset < events.count; ++offset)
        {
            uint64 const timestampMs = events.values[(first + offset) % PLAYERBOT_LOOP_EVENT_CAPACITY];
            if (!IsInWindow(timestampMs, nowMs, windowMs))
                continue;

            if (!count)
                firstEvidenceMs = timestampMs;
            lastEvidenceMs = timestampMs;
            ++count;
        }

        if (count < threshold)
            return;

        PlayerbotLoopAnomaly anomaly = {
            .classifier = classifier,
            .firstEvidenceMs = firstEvidenceMs,
            .lastEvidenceMs = lastEvidenceMs,
            .evidenceCount = count,
        };
        if (latestMovement)
        {
            anomaly.objectiveKind = latestMovement->objective.kind;
            anomaly.objectiveKey = latestMovement->objective.key;
            anomaly.objectiveTitle = latestMovement->objective.title;
        }
        appendAnomaly(anomaly);
    };

    appendRelapse(deaths, DEATH_WINDOW_MS, DEATH_RELAPSE_THRESHOLD, PlayerbotLoopClassifier::DeathRelapse);
    appendRelapse(recoveries, RECOVERY_WINDOW_MS, RECOVERY_RELAPSE_THRESHOLD, PlayerbotLoopClassifier::RecoveryRelapse);

    return result;
}
