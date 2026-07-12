/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef PLAYERBOTS_MARKACTION_H
#define PLAYERBOTS_MARKACTION_H

#include "Action.h"

class PlayerbotAI;

class MarkAction : public Action
{
public:
    MarkAction(PlayerbotAI* botAI) : Action(botAI, "mark") {}

    bool Execute(Event event) override;
};

#endif
