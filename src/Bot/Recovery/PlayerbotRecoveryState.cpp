/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "PlayerbotRecoveryState.h"

std::shared_ptr<PlayerbotRecoveryState> PlayerbotRecoveryStateStore::Get(std::uint32_t botGuid)
{
    std::scoped_lock lock(mutex);
    std::shared_ptr<PlayerbotRecoveryState>& state = states[botGuid];
    if (!state)
        state = std::make_shared<PlayerbotRecoveryState>();
    return state;
}

std::shared_ptr<PlayerbotRecoveryState> PlayerbotRecoveryStateStore::Find(std::uint32_t botGuid) const
{
    std::scoped_lock lock(mutex);
    auto const found = states.find(botGuid);
    return found == states.end() ? nullptr : found->second;
}

void PlayerbotRecoveryStateStore::Erase(std::uint32_t botGuid)
{
    std::scoped_lock lock(mutex);
    states.erase(botGuid);
}

std::size_t PlayerbotRecoveryStateStore::Size() const
{
    std::scoped_lock lock(mutex);
    return states.size();
}

PlayerbotRecoveryStateStore& GetPlayerbotRecoveryStateStore()
{
    static PlayerbotRecoveryStateStore store;
    return store;
}
