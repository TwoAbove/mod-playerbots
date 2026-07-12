
#include "Playerbots.h"
#include "BroadcastHelper.h"
#include "ServerFacade.h"
#include "Channel.h"
#include "AiFactory.h"

namespace
{
using Placeholders = std::map<std::string, std::string>;
using ChannelList = std::list<std::pair<BroadcastHelper::ToChannel, uint32>>;

ChannelList const DefaultChannels = { {BroadcastHelper::TO_GUILD, 50}, {BroadcastHelper::TO_WORLD, 50},
    {BroadcastHelper::TO_GENERAL, 100} };

Placeholders BasePlaceholders(PlayerbotAI* ai, Player* bot)
{
    Placeholders placeholders;
    AreaTableEntry const* currentArea = ai->GetCurrentArea();
    AreaTableEntry const* currentZone = ai->GetCurrentZone();
    placeholders["%area_name"] = currentArea ? ai->GetLocalizedAreaName(currentArea) : PlayerbotTextMgr::instance().GetBotText("string_unknown_area");
    placeholders["%zone_name"] = currentZone ? ai->GetLocalizedAreaName(currentZone) : PlayerbotTextMgr::instance().GetBotText("string_unknown_area");
    placeholders["%my_class"] = ai->GetChatHelper()->FormatClass(bot->getClass());
    placeholders["%my_race"] = ai->GetChatHelper()->FormatRace(bot->getRace());
    placeholders["%my_level"] = std::to_string(bot->GetLevel());
    return placeholders;
}

bool RollAndBroadcast(PlayerbotAI* ai, uint32 chance, std::string const& textKey, Placeholders const& placeholders,
    ChannelList const& channels = DefaultChannels, bool* passedChance = nullptr)
{
    if (passedChance)
        *passedChance = false;

    if (urand(1, sPlayerbotAIConfig.broadcastChanceMaxValue) <= chance)
    {
        if (passedChance)
            *passedChance = true;
        return BroadcastHelper::BroadcastToChannelWithGlobalChance(
            ai, PlayerbotTextMgr::instance().GetBotText(textKey, placeholders), channels);
    }

    return false;
}

template <typename PlaceholderFactory>
bool RollAndBroadcastLazy(PlayerbotAI* ai, uint32 chance, std::string const& textKey, PlaceholderFactory&& placeholderFactory,
    ChannelList const& channels = DefaultChannels)
{
    if (urand(1, sPlayerbotAIConfig.broadcastChanceMaxValue) <= chance)
    {
        Placeholders placeholders = placeholderFactory();
        return BroadcastHelper::BroadcastToChannelWithGlobalChance(
            ai, PlayerbotTextMgr::instance().GetBotText(textKey, placeholders), channels);
    }

    return false;
}
}

BroadcastHelper::BroadcastHelper() {}

uint8 BroadcastHelper::GetLocale()
{
    uint8 locale = sWorld->GetDefaultDbcLocale();
    // -- In case we're using auto detect on config file^M
    if (locale >= TOTAL_LOCALES)
        locale = LocaleConstant::LOCALE_enUS;
    return locale;
}

bool BroadcastHelper::BroadcastTest(PlayerbotAI* ai, Player* /* bot */)
{
    //return something to ignore the logic
    return false;

    std::map<std::string, std::string> placeholders;
    placeholders["%rand1"] = std::to_string(urand(0, 1));
    placeholders["%rand2"] = std::to_string(urand(0, 1));
    placeholders["%rand3"] = std::to_string(urand(0, 1));

    int32 rand = urand(0, 1);

    if (rand == 1 && ai->SayToChannel(PlayerbotTextMgr::instance().GetBotText("Posted to trade, %rand1, %rand2, %rand3", placeholders), ChatChannelId::TRADE))
        return true;
    else if (ai->SayToChannel(PlayerbotTextMgr::instance().GetBotText("Posted to GuildRecruitment, %rand1, %rand2, %rand3", placeholders), ChatChannelId::GUILD_RECRUITMENT))
        return true;

    return ai->SayToChannel(PlayerbotTextMgr::instance().GetBotText("Posted to trade, %rand1, %rand2, %rand3", placeholders), ChatChannelId::TRADE);

    //int32 rand = urand(1, 8);
    if (rand == 1 && ai->SayToGuild(PlayerbotTextMgr::instance().GetBotText("Posted to guild, %rand1, %rand2, %rand3", placeholders)))
        return true;
    else if (rand == 2 && ai->SayToWorld(PlayerbotTextMgr::instance().GetBotText("Posted to world, %rand1, %rand2, %rand3", placeholders)))
        return true;
    else if (rand == 3 && ai->SayToChannel(PlayerbotTextMgr::instance().GetBotText("Posted to general, %rand1, %rand2, %rand3", placeholders), ChatChannelId::GENERAL))
        return true;
    else if (rand == 4 && ai->SayToChannel(PlayerbotTextMgr::instance().GetBotText("Posted to trade, %rand1, %rand2, %rand3", placeholders), ChatChannelId::TRADE))
        return true;
    else if (rand == 5 && ai->SayToChannel(PlayerbotTextMgr::instance().GetBotText("Posted to LFG, %rand1, %rand2, %rand3", placeholders), ChatChannelId::LOOKING_FOR_GROUP))
        return true;
    else if (rand == 6 && ai->SayToChannel(PlayerbotTextMgr::instance().GetBotText("Posted to LocalDefense, %rand1, %rand2, %rand3", placeholders), ChatChannelId::LOCAL_DEFENSE))
        return true;
    else if (rand == 7 && ai->SayToChannel(PlayerbotTextMgr::instance().GetBotText("Posted to WorldDefense, %rand1, %rand2, %rand3", placeholders), ChatChannelId::WORLD_DEFENSE))
        return true;
    else if (rand == 8 && ai->SayToChannel(PlayerbotTextMgr::instance().GetBotText("Posted to GuildRecruitment, %rand1, %rand2, %rand3", placeholders), ChatChannelId::GUILD_RECRUITMENT))
        return true;

    return false;
}

/**
@param toChannels - map of (ToChannel, chance), where chance is in range 0-100 as uint32 (unless global chance is not 100%)

@return true if said to the channel, false otherwise
*/
bool BroadcastHelper::BroadcastToChannelWithGlobalChance(PlayerbotAI* ai, std::string message, std::list<std::pair<ToChannel, uint32>> toChannels)
{
    if (!sPlayerbotAIConfig.enableBroadcasts)
        return false;
    if (message.empty())
    {
        return false;
    }

    for (auto const& pair : toChannels)
    {
        uint32 roll = urand(1, 100);
        uint32 chance = pair.second;
        uint32 broadcastRoll = urand(1, sPlayerbotAIConfig.broadcastChanceMaxValue);

        switch (pair.first)
        {
            case TO_GUILD:
            {
                if (roll <= chance
                    && broadcastRoll <= sPlayerbotAIConfig.broadcastToGuildGlobalChance
                    && ai->SayToGuild(message))
                {
                    return true;
                }
                break;
            }
            case TO_WORLD:
            {
                if (roll <= chance
                    && broadcastRoll <= sPlayerbotAIConfig.broadcastToWorldGlobalChance
                    && ai->SayToWorld(message))
                {
                    return true;
                }
                break;
            }
            case TO_GENERAL:
            {
                if (roll <= chance
                    && broadcastRoll <= sPlayerbotAIConfig.broadcastToGeneralGlobalChance
                    && ai->SayToChannel(message, ChatChannelId::GENERAL))
                {
                    return true;
                }
                break;
            }
            case TO_TRADE:
            {
                if (roll <= chance
                    && broadcastRoll <= sPlayerbotAIConfig.broadcastToTradeGlobalChance
                    && ai->SayToChannel(message, ChatChannelId::TRADE))
                {
                    return true;
                }
                break;
            }
            case TO_LOOKING_FOR_GROUP:
            {
                if (roll <= chance
                    && broadcastRoll <= sPlayerbotAIConfig.broadcastToLFGGlobalChance
                    && ai->SayToChannel(message, ChatChannelId::LOOKING_FOR_GROUP))
                {
                    return true;
                }
                break;
            }
            case TO_LOCAL_DEFENSE:
            {
                if (roll <= chance
                    && broadcastRoll <= sPlayerbotAIConfig.broadcastToLocalDefenseGlobalChance
                    && ai->SayToChannel(message, ChatChannelId::LOCAL_DEFENSE))
                {
                    return true;
                }
                break;
            }
            case TO_WORLD_DEFENSE:
            {
                if (roll <= chance
                    && broadcastRoll <= sPlayerbotAIConfig.broadcastToWorldDefenseGlobalChance
                    && ai->SayToChannel(message, ChatChannelId::WORLD_DEFENSE))
                {
                    return true;
                }
                break;
            }
            case TO_GUILD_RECRUITMENT:
            {
                if (roll <= chance
                    && broadcastRoll <= sPlayerbotAIConfig.broadcastToGuildRecruitmentGlobalChance
                    && ai->SayToChannel(message, ChatChannelId::GUILD_RECRUITMENT))
                {
                    return true;
                }
                break;
            }
            default:
                break;
        }
    }

    return false;
}

bool BroadcastHelper::BroadcastLootingItem(PlayerbotAI* ai, Player* bot, ItemTemplate const* proto)
{
    if (!sPlayerbotAIConfig.enableBroadcasts)
        return false;
    Placeholders placeholders = BasePlaceholders(ai, bot);
    placeholders["%item_link"] = ai->GetChatHelper()->FormatItem(proto);

    switch (proto->Quality)
    {
        case ITEM_QUALITY_POOR:
            return RollAndBroadcast(ai, sPlayerbotAIConfig.broadcastChanceLootingItemPoor, "broadcast_looting_item_poor", placeholders);
        case ITEM_QUALITY_NORMAL:
            return RollAndBroadcast(ai, sPlayerbotAIConfig.broadcastChanceLootingItemNormal, "broadcast_looting_item_normal", placeholders);
        case ITEM_QUALITY_UNCOMMON:
            return RollAndBroadcast(ai, sPlayerbotAIConfig.broadcastChanceLootingItemUncommon, "broadcast_looting_item_uncommon", placeholders);
        case ITEM_QUALITY_RARE:
            return RollAndBroadcast(ai, sPlayerbotAIConfig.broadcastChanceLootingItemRare, "broadcast_looting_item_rare", placeholders);
        case ITEM_QUALITY_EPIC:
            return RollAndBroadcast(ai, sPlayerbotAIConfig.broadcastChanceLootingItemEpic, "broadcast_looting_item_epic", placeholders);
        case ITEM_QUALITY_LEGENDARY:
            return RollAndBroadcast(ai, sPlayerbotAIConfig.broadcastChanceLootingItemLegendary, "broadcast_looting_item_legendary", placeholders);
        case ITEM_QUALITY_ARTIFACT:
            return RollAndBroadcast(ai, sPlayerbotAIConfig.broadcastChanceLootingItemArtifact, "broadcast_looting_item_artifact", placeholders);
        default:
            break;
    }

    return false;
}

bool BroadcastHelper::BroadcastQuestAccepted(PlayerbotAI* ai, Player* bot, const Quest* quest)
{
    if (!sPlayerbotAIConfig.enableBroadcasts)
        return false;
    return RollAndBroadcastLazy(ai, sPlayerbotAIConfig.broadcastChanceQuestAccepted, "broadcast_quest_accepted_generic",
        [&]
        {
            Placeholders placeholders = BasePlaceholders(ai, bot);
            placeholders["%quest_link"] = ai->GetChatHelper()->FormatQuest(quest);
            return placeholders;
        });
}

bool BroadcastHelper::BroadcastQuestUpdateAddKill(PlayerbotAI* ai, Player* bot, Quest const* quest, uint32 availableCount, uint32 requiredCount, std::string obectiveName)
{
    if (!sPlayerbotAIConfig.enableBroadcasts)
        return false;
    Placeholders placeholders = BasePlaceholders(ai, bot);
    placeholders["%quest_link"] = ai->GetChatHelper()->FormatQuest(quest);
    placeholders["%quest_obj_name"] = obectiveName;
    placeholders["%quest_obj_available"] = std::to_string(availableCount);
    placeholders["%quest_obj_required"] = std::to_string(requiredCount);
    placeholders["%quest_obj_missing"] = std::to_string(requiredCount - std::min(availableCount, requiredCount));
    placeholders["%quest_obj_full_formatted"] = ai->GetChatHelper()->FormatQuestObjective(obectiveName, availableCount, requiredCount);

    if (availableCount < requiredCount)
    {
        return RollAndBroadcast(ai, sPlayerbotAIConfig.broadcastChanceQuestUpdateObjectiveProgress,
            "broadcast_quest_update_add_kill_objective_progress", placeholders);
    }
    else if (availableCount == requiredCount)
    {
        return RollAndBroadcast(ai, sPlayerbotAIConfig.broadcastChanceQuestUpdateObjectiveCompleted,
            "broadcast_quest_update_add_kill_objective_completed", placeholders);
    }

    return false;
}

bool BroadcastHelper::BroadcastQuestUpdateAddItem(PlayerbotAI* ai, Player* bot, Quest const* quest, uint32 availableCount, uint32 requiredCount, const ItemTemplate* proto)
{
    if (!sPlayerbotAIConfig.enableBroadcasts)
        return false;
    Placeholders placeholders = BasePlaceholders(ai, bot);
    placeholders["%quest_link"] = ai->GetChatHelper()->FormatQuest(quest);
    std::string itemLinkFormatted = ai->GetChatHelper()->FormatItem(proto);
    placeholders["%item_link"] = itemLinkFormatted;
    placeholders["%quest_obj_available"] = std::to_string(availableCount);
    placeholders["%quest_obj_required"] = std::to_string(requiredCount);
    placeholders["%quest_obj_missing"] = std::to_string(requiredCount - std::min(availableCount, requiredCount));
    placeholders["%quest_obj_full_formatted"] = ai->GetChatHelper()->FormatQuestObjective(itemLinkFormatted, availableCount, requiredCount);

    if (availableCount < requiredCount)
    {
        return RollAndBroadcast(ai, sPlayerbotAIConfig.broadcastChanceQuestUpdateObjectiveProgress,
            "broadcast_quest_update_add_item_objective_progress", placeholders);
    }
    else if (availableCount == requiredCount)
    {
        return RollAndBroadcast(ai, sPlayerbotAIConfig.broadcastChanceQuestUpdateObjectiveCompleted,
            "broadcast_quest_update_add_item_objective_completed", placeholders);
    }

    return false;
}

bool BroadcastHelper::BroadcastQuestUpdateFailedTimer(PlayerbotAI* ai, Player* bot, Quest const* quest)
{
    if (!sPlayerbotAIConfig.enableBroadcasts)
        return false;
    return RollAndBroadcastLazy(ai, sPlayerbotAIConfig.broadcastChanceQuestUpdateFailedTimer, "broadcast_quest_update_failed_timer",
        [&]
        {
            Placeholders placeholders = BasePlaceholders(ai, bot);
            placeholders["%quest_link"] = ai->GetChatHelper()->FormatQuest(quest);
            return placeholders;
        });
}

bool BroadcastHelper::BroadcastQuestUpdateComplete(PlayerbotAI* ai, Player* bot, Quest const* quest)
{
    if (!sPlayerbotAIConfig.enableBroadcasts)
        return false;
    return RollAndBroadcastLazy(ai, sPlayerbotAIConfig.broadcastChanceQuestUpdateComplete, "broadcast_quest_update_complete",
        [&]
        {
            Placeholders placeholders = BasePlaceholders(ai, bot);
            placeholders["%quest_link"] = ai->GetChatHelper()->FormatQuest(quest);
            return placeholders;
        });
}

bool BroadcastHelper::BroadcastQuestTurnedIn(PlayerbotAI* ai, Player* bot, Quest const* quest)
{
    if (!sPlayerbotAIConfig.enableBroadcasts)
        return false;
    return RollAndBroadcastLazy(ai, sPlayerbotAIConfig.broadcastChanceQuestTurnedIn, "broadcast_quest_turned_in",
        [&]
        {
            Placeholders placeholders = BasePlaceholders(ai, bot);
            placeholders["%quest_link"] = ai->GetChatHelper()->FormatQuest(quest);
            return placeholders;
        });
}

bool BroadcastHelper::BroadcastKill(PlayerbotAI* ai, Player* bot, Creature *creature)
{
    if (!sPlayerbotAIConfig.enableBroadcasts)
        return false;
    Placeholders placeholders = BasePlaceholders(ai, bot);
    placeholders["%victim_name"] = creature->GetName();
    placeholders["%victim_level"] = std::to_string(creature->GetLevel());

    //if ((creature->IsElite() && !creature->GetMap()->IsDungeon())
    //if creature->IsWorldBoss()
    //if creature->GetLevel() > DEFAULT_MAX_LEVEL + 1
    //if creature->GetLevel() > bot->GetLevel() + 4

    if (creature->IsPet())
    {
        return RollAndBroadcast(ai, sPlayerbotAIConfig.broadcastChanceKillPet, "broadcast_killed_pet", placeholders);
    }
    else if (creature->IsPlayer())
    {
        return RollAndBroadcastLazy(ai, sPlayerbotAIConfig.broadcastChanceKillPlayer, "broadcast_killed_player",
            [&]
            {
                placeholders["%victim_class"] = ai->GetChatHelper()->FormatClass(creature->getClass());
                return placeholders;
            },
            { {TO_WORLD_DEFENSE, 50}, {TO_LOCAL_DEFENSE, 50}, {TO_GUILD, 50}, {TO_WORLD, 50}, {TO_GENERAL, 100} });
    }
    else
    {
        switch (creature->GetCreatureTemplate()->rank)
        {
            case CREATURE_ELITE_NORMAL:
                return RollAndBroadcast(ai, sPlayerbotAIConfig.broadcastChanceKillNormal, "broadcast_killed_normal", placeholders);
            case CREATURE_ELITE_ELITE:
                return RollAndBroadcast(ai, sPlayerbotAIConfig.broadcastChanceKillElite, "broadcast_killed_elite", placeholders);
            case CREATURE_ELITE_RAREELITE:
                return RollAndBroadcast(ai, sPlayerbotAIConfig.broadcastChanceKillRareelite, "broadcast_killed_rareelite", placeholders);
            case CREATURE_ELITE_WORLDBOSS:
                return RollAndBroadcast(ai, sPlayerbotAIConfig.broadcastChanceKillWorldboss, "broadcast_killed_worldboss", placeholders);
            case CREATURE_ELITE_RARE:
                return RollAndBroadcast(ai, sPlayerbotAIConfig.broadcastChanceKillRare, "broadcast_killed_rare", placeholders);
            case CREATURE_UNKNOWN:
                return RollAndBroadcast(ai, sPlayerbotAIConfig.broadcastChanceKillUnknown, "broadcast_killed_unknown", placeholders);
            default:
                break;
        }
    }

    return false;
}

bool BroadcastHelper::BroadcastLevelup(PlayerbotAI* ai, Player* bot)
{
    if (!sPlayerbotAIConfig.enableBroadcasts)
        return false;
    uint32 level = bot->GetLevel();

    Placeholders placeholders = BasePlaceholders(ai, bot);

    bool passedChance;
    bool result;
    if (level == sPlayerbotAIConfig.randomBotMaxLevel)
    {
        result = RollAndBroadcast(ai, sPlayerbotAIConfig.broadcastChanceLevelupMaxLevel, "broadcast_levelup_max_level",
            placeholders, { {TO_GUILD, 30}, {TO_WORLD, 90}, {TO_GENERAL, 100} }, &passedChance);
        if (passedChance)
            return result;
    }

    if (level % 10 == 0)
    {
        result = RollAndBroadcast(ai, sPlayerbotAIConfig.broadcastChanceLevelupTenX, "broadcast_levelup_10x",
            placeholders, { {TO_GUILD, 50}, {TO_WORLD, 90}, {TO_GENERAL, 100} }, &passedChance);
        if (passedChance)
            return result;
    }

    return RollAndBroadcast(ai, sPlayerbotAIConfig.broadcastChanceLevelupGeneric, "broadcast_levelup_generic",
        placeholders, { {TO_GUILD, 90}, {TO_WORLD, 90}, {TO_GENERAL, 100} });
}

bool BroadcastHelper::BroadcastGuildMemberPromotion(PlayerbotAI* ai, Player* /* bot */, Player* player)
{
    if (!sPlayerbotAIConfig.enableBroadcasts)
        return false;
    if (urand(1, sPlayerbotAIConfig.broadcastChanceMaxValue) <= sPlayerbotAIConfig.broadcastChanceGuildManagement)
    {
        std::map<std::string, std::string> placeholders;
        placeholders["%other_name"] = player->GetName();
        placeholders["%other_class"] = ai->GetChatHelper()->FormatClass(player->getClass());
        placeholders["%other_race"] = ai->GetChatHelper()->FormatRace(player->getRace());
        placeholders["%other_level"] = std::to_string(player->GetLevel());

        return ai->SayToGuild(PlayerbotTextMgr::instance().GetBotText("broadcast_guild_promotion", placeholders));
    }

    return false;
}

bool BroadcastHelper::BroadcastGuildMemberDemotion(PlayerbotAI* ai, Player* /* bot */, Player* player)
{
    if (!sPlayerbotAIConfig.enableBroadcasts)
        return false;

    if (urand(1, sPlayerbotAIConfig.broadcastChanceMaxValue) <= sPlayerbotAIConfig.broadcastChanceGuildManagement)
    {
        std::map<std::string, std::string> placeholders;
        placeholders["%other_name"] = player->GetName();
        placeholders["%other_class"] = ai->GetChatHelper()->FormatClass(player->getClass());
        placeholders["%other_race"] = ai->GetChatHelper()->FormatRace(player->getRace());
        placeholders["%other_level"] = std::to_string(player->GetLevel());

        return ai->SayToGuild(PlayerbotTextMgr::instance().GetBotText("broadcast_guild_demotion", placeholders));
    }

    return false;
}

bool BroadcastHelper::BroadcastGuildGroupOrRaidInvite(PlayerbotAI* ai, Player* /* bot */, Player* player, Group* group)
{
    if (!sPlayerbotAIConfig.enableBroadcasts)
        return false;
    std::map<std::string, std::string> placeholders;
    placeholders["%name"] = player->GetName();
    AreaTableEntry const* current_area = ai->GetCurrentArea();
    AreaTableEntry const* current_zone = ai->GetCurrentZone();
    placeholders["%area_name"] = current_area ? ai->GetLocalizedAreaName(current_area) : PlayerbotTextMgr::instance().GetBotText("string_unknown_area");
    placeholders["%zone_name"] = current_zone ? ai->GetLocalizedAreaName(current_zone) : PlayerbotTextMgr::instance().GetBotText("string_unknown_area");

    //TODO move texts to sql!
    if (group && group->isRaidGroup())
    {
        if (urand(0, 3))
        {
            return ai->SayToGuild(PlayerbotTextMgr::instance().GetBotText("Hey anyone want to raid in %zone_name", placeholders));
        }
        else
        {
            return ai->SayToGuild(PlayerbotTextMgr::instance().GetBotText("Hey %name I'm raiding in %zone_name do you wan to join me?", placeholders));
        }
    }
    else
    {
        //(bot->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH)
        if (urand(0, 3))
        {
            return ai->SayToGuild(PlayerbotTextMgr::instance().GetBotText("Hey anyone wanna group up in %zone_name?", placeholders));
        }
        else
        {
            return ai->SayToGuild(PlayerbotTextMgr::instance().GetBotText("Hey %name do you want join my group? I'm heading for %zone_name", placeholders));
        }
    }

    return false;
}

bool BroadcastHelper::BroadcastSuggestInstance(PlayerbotAI* ai, std::vector<std::string>& allowedInstances, Player* bot)
{
    if (!sPlayerbotAIConfig.enableBroadcasts)
        return false;
    return RollAndBroadcastLazy(ai, sPlayerbotAIConfig.broadcastChanceSuggestInstance, "suggest_instance",
        [&]
        {
            Placeholders placeholders;
            placeholders["%my_role"] = ChatHelper::FormatClass(bot, AiFactory::GetPlayerSpecTab(bot));

            std::ostringstream itemout;
            itemout << allowedInstances[urand(0, allowedInstances.size() - 1)];
            placeholders["%instance_name"] = itemout.str();
            placeholders["%my_class"] = ai->GetChatHelper()->FormatClass(bot->getClass());
            placeholders["%my_race"] = ai->GetChatHelper()->FormatRace(bot->getRace());
            placeholders["%my_level"] = std::to_string(bot->GetLevel());
            return placeholders;
        },
        { {TO_LOOKING_FOR_GROUP, 50}, {TO_GUILD, 50}, {TO_WORLD, 50}, {TO_GENERAL, 100} });
}

bool BroadcastHelper::BroadcastSuggestQuest(PlayerbotAI* ai, std::vector<uint32>& quests, Player* bot)
{
    if (!sPlayerbotAIConfig.enableBroadcasts)
        return false;
    return RollAndBroadcastLazy(ai, sPlayerbotAIConfig.broadcastChanceSuggestQuest, "suggest_quest",
        [&]
        {
            int index = rand() % quests.size();
            Quest const* quest = sObjectMgr->GetQuestTemplate(quests[index]);

            Placeholders placeholders;
            placeholders["%my_role"] = ChatHelper::FormatClass(bot, AiFactory::GetPlayerSpecTab(bot));
            placeholders["%quest_link"] = ai->GetChatHelper()->FormatQuest(quest);
            placeholders["%quest_level"] = std::to_string(quest->GetQuestLevel());
            placeholders["%my_class"] = ai->GetChatHelper()->FormatClass(bot->getClass());
            placeholders["%my_race"] = ai->GetChatHelper()->FormatRace(bot->getRace());
            placeholders["%my_level"] = std::to_string(bot->GetLevel());
            return placeholders;
        },
        { {TO_LOOKING_FOR_GROUP, 50}, {TO_GUILD, 50}, {TO_WORLD, 50}, {TO_GENERAL, 100} });
}

bool BroadcastHelper::BroadcastSuggestGrindMaterials(PlayerbotAI* ai, std::string item, Player* bot)
{
    if (!sPlayerbotAIConfig.enableBroadcasts)
        return false;
    return RollAndBroadcastLazy(ai, sPlayerbotAIConfig.broadcastChanceSuggestGrindMaterials, "suggest_trade",
        [&]
        {
            Placeholders placeholders;
            placeholders["%my_role"] = ChatHelper::FormatClass(bot, AiFactory::GetPlayerSpecTab(bot));
            placeholders["%category"] = item;
            placeholders["%my_class"] = ai->GetChatHelper()->FormatClass(bot->getClass());
            placeholders["%my_race"] = ai->GetChatHelper()->FormatRace(bot->getRace());
            placeholders["%my_level"] = std::to_string(bot->GetLevel());
            return placeholders;
        },
        { {TO_TRADE, 50}, {TO_LOOKING_FOR_GROUP, 50}, {TO_GUILD, 50}, {TO_WORLD, 50}, {TO_GENERAL, 100} });
}

bool BroadcastHelper::BroadcastSuggestGrindReputation(PlayerbotAI* ai, std::vector<std::string> levels, std::vector<std::string> allowedFactions, Player* bot)
{
    if (!sPlayerbotAIConfig.enableBroadcasts)
        return false;
    return RollAndBroadcastLazy(ai, sPlayerbotAIConfig.broadcastChanceSuggestGrindReputation, "suggest_faction",
        [&]
        {
            Placeholders placeholders;
            placeholders["%my_role"] = ChatHelper::FormatClass(bot, AiFactory::GetPlayerSpecTab(bot));
            placeholders["%rep_level"] = levels[urand(0, 2)];
            std::ostringstream rnd;
            rnd << urand(1, 5) << "K";
            placeholders["%rndK"] = rnd.str();

            std::ostringstream itemout;
            itemout << allowedFactions[urand(0, allowedFactions.size() - 1)];
            placeholders["%faction"] = itemout.str();
            placeholders["%my_class"] = ai->GetChatHelper()->FormatClass(bot->getClass());
            placeholders["%my_race"] = ai->GetChatHelper()->FormatRace(bot->getRace());
            placeholders["%my_level"] = std::to_string(bot->GetLevel());
            return placeholders;
        },
        { {TO_LOOKING_FOR_GROUP, 50}, {TO_GUILD, 50}, {TO_WORLD, 50}, {TO_GENERAL, 100} });
}

bool BroadcastHelper::BroadcastSuggestSell(PlayerbotAI* ai, const ItemTemplate* proto, uint32 count, uint32 price, Player* bot)
{
    if (!sPlayerbotAIConfig.enableBroadcasts)
        return false;
    return RollAndBroadcastLazy(ai, sPlayerbotAIConfig.broadcastChanceSuggestSell, "suggest_sell",
        [&]
        {
            Placeholders placeholders;
            placeholders["%item_link"] = ai->GetChatHelper()->FormatItem(proto, 0);
            placeholders["%item_formatted_link"] = ai->GetChatHelper()->FormatItem(proto, count);
            placeholders["%item_count"] = std::to_string(count);
            placeholders["%cost_gold"] = ai->GetChatHelper()->formatMoney(price);
            placeholders["%my_class"] = ai->GetChatHelper()->FormatClass(bot->getClass());
            placeholders["%my_race"] = ai->GetChatHelper()->FormatRace(bot->getRace());
            placeholders["%my_level"] = std::to_string(bot->GetLevel());
            return placeholders;
        },
        { {TO_TRADE, 90}, {TO_GENERAL, 100} });
}

bool BroadcastHelper::BroadcastSuggestSomething(PlayerbotAI* ai, Player* bot)
{
    if (!sPlayerbotAIConfig.enableBroadcasts)
        return false;
    return RollAndBroadcastLazy(ai, sPlayerbotAIConfig.broadcastChanceSuggestSomething, "suggest_something",
        [&]
        {
            Placeholders placeholders = BasePlaceholders(ai, bot);
            placeholders["%my_role"] = ChatHelper::FormatClass(bot, AiFactory::GetPlayerSpecTab(bot));
            return placeholders;
        },
        { {TO_GUILD, 10}, {TO_WORLD, 70}, {TO_GENERAL, 100} });
}

bool BroadcastHelper::BroadcastSuggestSomethingToxic(PlayerbotAI* ai, Player* bot)
{
    if (!sPlayerbotAIConfig.enableBroadcasts)
        return false;
    return RollAndBroadcastLazy(ai, sPlayerbotAIConfig.broadcastChanceSuggestSomethingToxic, "suggest_something_toxic",
        [&]
        {
            std::vector<Item*> botItems = ai->GetInventoryAndEquippedItems();
            Placeholders placeholders = BasePlaceholders(ai, bot);
            placeholders["%random_inventory_item_link"] = botItems.size() > 0 ? ai->GetChatHelper()->FormatItem(botItems[rand() % botItems.size()]->GetTemplate()) : PlayerbotTextMgr::instance().GetBotText("string_empty_link");
            placeholders["%my_role"] = ChatHelper::FormatClass(bot, AiFactory::GetPlayerSpecTab(bot));
            return placeholders;
        },
        { {TO_GUILD, 10}, {TO_WORLD, 70}, {TO_GENERAL, 100} });
}

bool BroadcastHelper::BroadcastSuggestToxicLinks(PlayerbotAI* ai, Player* bot)
{
    if (!sPlayerbotAIConfig.enableBroadcasts)
        return false;
    return RollAndBroadcastLazy(ai, sPlayerbotAIConfig.broadcastChanceSuggestToxicLinks, "suggest_toxic_links",
        [&]
        {
            std::vector<uint32> incompleteQuests;
            for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
            {
                uint32 questId = bot->GetQuestSlotQuestId(slot);
                if (!questId)
                    continue;

                QuestStatus status = bot->GetQuestStatus(questId);
                if (status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_NONE)
                    incompleteQuests.push_back(questId);
            }

            std::vector<Item*> botItems = ai->GetInventoryAndEquippedItems();
            Placeholders placeholders = BasePlaceholders(ai, bot);
            placeholders["%random_inventory_item_link"] = botItems.size() > 0 ? ai->GetChatHelper()->FormatItem(botItems[rand() % botItems.size()]->GetTemplate()) : PlayerbotTextMgr::instance().GetBotText("string_empty_link");
            placeholders["%prefix"] = sPlayerbotAIConfig.toxicLinksPrefix;

            if (incompleteQuests.size() > 0)
            {
                Quest const* quest = sObjectMgr->GetQuestTemplate(incompleteQuests[rand() % incompleteQuests.size()]);
                placeholders["%random_taken_quest_or_item_link"] = ai->GetChatHelper()->FormatQuest(quest);
            }
            else
            {
                placeholders["%random_taken_quest_or_item_link"] = placeholders["%random_inventory_item_link"];
            }

            placeholders["%my_role"] = ChatHelper::FormatClass(bot, AiFactory::GetPlayerSpecTab(bot));
            return placeholders;
        },
        { {TO_GUILD, 10}, {TO_WORLD, 70}, {TO_GENERAL, 100} });
}

bool BroadcastHelper::BroadcastSuggestThunderfury(PlayerbotAI* ai, Player* bot)
{
    if (!sPlayerbotAIConfig.enableBroadcasts)
        return false;

    return RollAndBroadcastLazy(ai, sPlayerbotAIConfig.broadcastChanceSuggestThunderfury, "thunderfury_spam",
        [&]
        {
            Placeholders placeholders;
            ItemTemplate const* thunderfuryProto = sObjectMgr->GetItemTemplate(19019);
            placeholders["%thunderfury_link"] = GET_PLAYERBOT_AI(bot)->GetChatHelper()->FormatItem(thunderfuryProto);
            return placeholders;
        },
        { {TO_WORLD, 70}, {TO_GENERAL, 100} });
}
