/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_PLAYERBOTGRINDDESTINATIONPOLICY_H
#define PLAYERBOTS_PLAYERBOTGRINDDESTINATIONPOLICY_H

#include <algorithm>
#include <limits>
#include <span>

#include "Define.h"

struct PlayerbotGrindSpawn
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    uint8 minimumLevel = 0;
    uint8 maximumLevel = 0;
};

struct PlayerbotGrindCell
{
    bool available = false;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    uint8 minimumLevel = 0;
    uint8 maximumLevel = 0;
};

class PlayerbotGrindDestinationPolicy
{
public:
    [[nodiscard]] static PlayerbotGrindCell BuildCell(std::span<PlayerbotGrindSpawn const> spawns)
    {
        if (spawns.empty())
            return {};

        float centroidX = 0.0f;
        float centroidY = 0.0f;
        float centroidZ = 0.0f;
        uint8 minimumLevel = std::numeric_limits<uint8>::max();
        uint8 maximumLevel = 0;
        for (PlayerbotGrindSpawn const& spawn : spawns)
        {
            centroidX += spawn.x;
            centroidY += spawn.y;
            centroidZ += spawn.z;
            minimumLevel = std::min(minimumLevel, spawn.minimumLevel);
            maximumLevel = std::max(maximumLevel, spawn.maximumLevel);
        }
        centroidX /= spawns.size();
        centroidY /= spawns.size();
        centroidZ /= spawns.size();

        PlayerbotGrindSpawn const* representative = &spawns.front();
        float nearestDistanceSquared = std::numeric_limits<float>::max();
        for (PlayerbotGrindSpawn const& spawn : spawns)
        {
            float const dx = spawn.x - centroidX;
            float const dy = spawn.y - centroidY;
            float const dz = spawn.z - centroidZ;
            float const distanceSquared = dx * dx + dy * dy + dz * dz;
            if (distanceSquared < nearestDistanceSquared)
            {
                representative = &spawn;
                nearestDistanceSquared = distanceSquared;
            }
        }

        return {
            .available = true,
            .x = representative->x,
            .y = representative->y,
            .z = representative->z,
            .minimumLevel = minimumLevel,
            .maximumLevel = maximumLevel,
        };
    }

    [[nodiscard]] static bool IsLevelRangeSafe(uint8 botLevel, uint8 minimumCreatureLevel, uint8 maximumCreatureLevel,
                                               uint32 lowerTolerance, uint32 higherTolerance)
    {
        return static_cast<uint16>(botLevel) + lowerTolerance >= maximumCreatureLevel &&
               botLevel <= static_cast<uint16>(minimumCreatureLevel) + higherTolerance;
    }
};

#endif
