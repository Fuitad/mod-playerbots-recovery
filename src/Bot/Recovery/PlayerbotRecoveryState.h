/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_PLAYERBOTRECOVERYSTATE_H
#define PLAYERBOTS_PLAYERBOTRECOVERYSTATE_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "Bot/Recovery/PlayerbotLoopMonitor.h"
#include "Bot/Recovery/PlayerbotObjectiveQuarantine.h"

struct PlayerbotRecoveryState
{
    PlayerbotLoopMonitor loopMonitor;
    PlayerbotObjectiveQuarantine objectiveQuarantine;
    std::atomic<std::uint64_t> nextObservationMs{0};
};

class PlayerbotRecoveryStateStore
{
public:
    [[nodiscard]] std::shared_ptr<PlayerbotRecoveryState> Get(std::uint32_t botGuid);
    [[nodiscard]] std::shared_ptr<PlayerbotRecoveryState> Find(std::uint32_t botGuid) const;
    void Erase(std::uint32_t botGuid);
    [[nodiscard]] std::size_t Size() const;

private:
    mutable std::mutex mutex;
    std::unordered_map<std::uint32_t, std::shared_ptr<PlayerbotRecoveryState>> states;
};

PlayerbotRecoveryStateStore& GetPlayerbotRecoveryStateStore();

#endif
