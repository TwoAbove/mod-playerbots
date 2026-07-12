/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "AcceptDuelAction.h"

#include <algorithm>

#include "Event.h"
#include "Playerbots.h"

bool AcceptDuelAction::Execute(Event event)
{
    WorldPacket p(event.getPacket());

    ObjectGuid flagGuid;
    p >> flagGuid;
    ObjectGuid playerGuid;
    p >> playerGuid;

    float const duelAcceptThreshold = std::clamp(
        botAI->GetAiObjectContext()->GetValue<float>("trait duel accept")->Get(), 0.0f, 100.0f);

    // do not auto duel with low hp
    if ((!botAI->HasRealPlayerMaster() || (botAI->GetMaster() && botAI->GetMaster()->GetGUID() != playerGuid)) &&
        AI_VALUE2(uint8, "health", "self target") < duelAcceptThreshold)
    {
        WorldPacket packet(CMSG_DUEL_CANCELLED, 8);
        packet << flagGuid;
        bot->GetSession()->HandleDuelCancelledOpcode(packet);
        return false;
    }

    WorldPacket packet(CMSG_DUEL_ACCEPTED, 8);
    packet << flagGuid;
    bot->GetSession()->HandleDuelAcceptedOpcode(packet);

    botAI->ResetStrategies();
    return true;
}
