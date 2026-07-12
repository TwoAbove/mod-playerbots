/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "MarkAction.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <vector>

#include "Event.h"
#include "Playerbots.h"
#include "RtiTargetValue.h"

namespace
{
std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
        [](unsigned char character) { return std::tolower(character); });
    return text;
}

std::string Trim(std::string text)
{
    std::string::size_type const first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};

    std::string::size_type const last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

uint8 ClassFromName(std::string const& name)
{
    if (name == "warrior")
        return CLASS_WARRIOR;
    if (name == "paladin")
        return CLASS_PALADIN;
    if (name == "hunter")
        return CLASS_HUNTER;
    if (name == "rogue")
        return CLASS_ROGUE;
    if (name == "priest")
        return CLASS_PRIEST;
    if (name == "shaman")
        return CLASS_SHAMAN;
    if (name == "mage")
        return CLASS_MAGE;
    if (name == "warlock")
        return CLASS_WARLOCK;
    if (name == "druid")
        return CLASS_DRUID;
    if (name == "dk" || name == "death knight")
        return CLASS_DEATH_KNIGHT;

    return 0;
}
}

bool MarkAction::Execute(Event event)
{
    Player* requester = event.getOwner();

    std::istringstream input(event.getParam());
    std::string icon;
    input >> icon;
    icon = Lower(icon);

    int32 const iconIndex = RtiTargetValue::GetRtiIndex(icon);
    if (iconIndex < 0)
    {
        botAI->TellPlayer(requester, "Invalid raid target icon. Use star, circle, diamond, triangle, moon, square, cross, or skull.");
        return true;
    }

    Group* group = bot->GetGroup();
    if (!group)
    {
        botAI->TellPlayer(requester, "Cannot mark a target without a group.");
        return true;
    }

    std::string description;
    std::getline(input, description);
    description = Lower(Trim(description));

    GuidVector candidates = AI_VALUE(GuidVector, "possible targets");
    GuidVector const enemyPlayers = AI_VALUE(GuidVector, "nearest enemy players");
    for (ObjectGuid const guid : enemyPlayers)
        if (std::find(candidates.begin(), candidates.end(), guid) == candidates.end())
            candidates.push_back(guid);

    Unit* target = nullptr;
    std::vector<Unit*> matches;

    if (description.empty() || description == "target")
    {
        if (requester)
        {
            Unit* selected = requester->GetSelectedUnit();
            if (selected && std::find(candidates.begin(), candidates.end(), selected->GetGUID()) != candidates.end())
                target = selected;
        }
    }
    else
    {
        uint8 const targetClass = ClassFromName(description);
        for (ObjectGuid const guid : candidates)
        {
            Unit* unit = botAI->GetUnit(guid);
            if (unit && Lower(unit->GetName()) == description)
                matches.push_back(unit);
        }

        if (matches.empty())
        {
            if (targetClass)
            {
                for (ObjectGuid const guid : candidates)
                {
                    Unit* unit = botAI->GetUnit(guid);
                    if (unit && unit->IsPlayer() && unit->getClass() == targetClass)
                        matches.push_back(unit);
                }
            }
        }

        if (matches.empty())
        {
            for (ObjectGuid const guid : candidates)
            {
                Unit* unit = botAI->GetUnit(guid);
                if (unit && unit->IsCreature() && Lower(unit->GetName()).find(description) != std::string::npos)
                    matches.push_back(unit);
            }
        }

        float nearestDistance = std::numeric_limits<float>::max();
        for (Unit* match : matches)
        {
            float const distance = bot->GetDistance(match);
            if (distance < nearestDistance)
            {
                nearestDistance = distance;
                target = match;
            }
        }
    }

    if (!target)
    {
        botAI->TellPlayer(requester, "No nearby enemy matched the mark target.");
        return true;
    }

    group->SetTargetIcon(static_cast<uint8>(iconIndex), bot->GetGUID(), target->GetGUID());

    std::ostringstream marked;
    marked << "Marked " << target->GetName() << " with " << icon;
    if (matches.size() > 1)
        marked << " (nearest of " << matches.size() << " matches)";
    marked << ".";
    botAI->TellPlayer(requester, marked.str());
    return true;
}
