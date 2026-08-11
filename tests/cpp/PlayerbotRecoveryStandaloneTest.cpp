#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "Bot/Recovery/PlayerbotGrindDestinationPolicy.h"
#include "Bot/Recovery/PlayerbotLoopMonitor.h"
#include "Bot/Recovery/PlayerbotObjectiveQuarantine.h"
#include "Bot/Recovery/PlayerbotRecoveryState.h"

namespace
{
constexpr uint64 SECOND_MS = 1000;

void Require(bool condition, std::string_view message)
{
    if (condition)
        return;

    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

PlayerbotLoopProgressObservation Observation(uint64 timestampMs, float x, float distance, uint64 progress = 1)
{
    return {
        .timestampMs = timestampMs,
        .mapId = 0,
        .x = x,
        .distanceToObjective = distance,
        .progress = progress,
        .objective = {PlayerbotLoopObjectiveKind::Quest, 101, "quest"},
    };
}

bool HasClassifier(PlayerbotLoopAnomalySnapshot const& snapshot, PlayerbotLoopClassifier classifier)
{
    for (std::size_t index = 0; index < snapshot.count; ++index)
        if (snapshot.anomalies[index].classifier == classifier)
            return true;
    return false;
}
}  // namespace

int main()
{
    std::string objectiveTitle = "A Durable Quest Title";
    PlayerbotLoopObjective objective = {PlayerbotLoopObjectiveKind::Quest, 101, objectiveTitle};
    objectiveTitle.assign("Caller storage was reused");
    Require(objective.title == "A Durable Quest Title",
            "loop progress observations did not own their deferred objective title");

    PlayerbotLoopMonitor monitor;
    monitor.ObserveProgress(Observation(0, 10.0f, 100.0f));
    monitor.ObserveProgress(Observation(45 * SECOND_MS, 10.2f, 99.8f));
    monitor.ObserveProgress(Observation(90 * SECOND_MS, 10.1f, 99.9f));
    Require(HasClassifier(monitor.CopyAnomalies(90 * SECOND_MS), PlayerbotLoopClassifier::StationaryMovement),
            "stationary movement was not detected");

    PlayerbotObjectiveQuarantine quarantine;
    uint64 const questKey = PlayerbotObjectiveQuarantine::QuestKey(101, 2);
    quarantine.Quarantine(PlayerbotLoopObjectiveKind::Quest, questKey, 7, 100);
    Require(quarantine.IsQuarantined(PlayerbotLoopObjectiveKind::Quest, questKey, 7, 200),
            "failed objective was not quarantined");
    Require(!quarantine.IsQuarantined(PlayerbotLoopObjectiveKind::Quest, questKey, 8, 300),
            "authoritative progress did not release the objective");

    PlayerbotGrindSpawn const spawns[] = {
        {.x = 0.0f, .y = 0.0f, .z = 0.0f, .minimumLevel = 5, .maximumLevel = 7},
        {.x = 10.0f, .y = 0.0f, .z = 0.0f, .minimumLevel = 6, .maximumLevel = 9},
        {.x = 9.0f, .y = 1.0f, .z = 0.0f, .minimumLevel = 7, .maximumLevel = 8},
    };
    PlayerbotGrindCell const cell = PlayerbotGrindDestinationPolicy::BuildCell(spawns);
    Require(cell.available && cell.x == 9.0f && cell.minimumLevel == 5 && cell.maximumLevel == 9,
            "grind cell did not preserve a reachable representative and level range");

    PlayerbotRecoveryStateStore store;
    std::shared_ptr<PlayerbotRecoveryState> const first = store.Get(42);
    std::shared_ptr<PlayerbotRecoveryState> const same = store.Get(42);
    std::shared_ptr<PlayerbotRecoveryState> const second = store.Get(43);
    Require(first == same, "one bot did not retain one recovery state");
    Require(first != second && store.Size() == 2, "recovery state leaked between bot identities");
    store.Erase(42);
    Require(!store.Find(42) && store.Find(43) && store.Size() == 1,
            "bot removal did not erase only the matching recovery state");
    return EXIT_SUCCESS;
}
