/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_PLAYERBOTRECOVERYACTION_H
#define PLAYERBOTS_PLAYERBOTRECOVERYACTION_H

#include "Action.h"

class PlayerbotRecoveryAction : public Action
{
public:
    explicit PlayerbotRecoveryAction(PlayerbotAI* botAI) : Action(botAI, "recover stuck movement") {}

    bool Execute(Event event) override;
    bool isPossible() override;

protected:
    virtual bool IsAutonomousBot() const;
    virtual void RandomRelocate();
};

#endif
