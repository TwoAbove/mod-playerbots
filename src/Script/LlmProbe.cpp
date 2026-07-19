// EXPLORATORY: instrumentation for the llmind control-loop investigation.
// `.playerbots llmtap <name>` dumps one bot's full decision-relevant state as JSON
// to /tmp/llmtap.json; `.playerbots llmsteer <name> <verb> [args]` pokes one
// steering surface. Not a shipping surface; delete freely.

#include "LlmProbe.h"

#include <atomic>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include "AiObjectContext.h"
#include "Bag.h"
#include "Battleground.h"
#include "DBCStores.h"
#include "Chat.h"
#include "Group.h"
#include "GuildMgr.h"
#include "Guild.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Personality/BotPersonality.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "QuestDef.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "TravelMgr.h"

namespace LlmProbe
{
std::atomic<TellSink> s_tellSink{nullptr};

void SetTellSink(TellSink sink) { s_tellSink.store(sink); }

void NoteTell(Player* bot, std::string const& text)
{
    if (TellSink sink = s_tellSink.load(std::memory_order_relaxed))
        sink(bot, text);
}

namespace
{
std::string Esc(std::string const& in)
{
    std::string out;
    out.reserve(in.size() + 4);
    for (char c : in)
    {
        if (c == '"' || c == '\\')
            out.push_back('\\');
        if (static_cast<unsigned char>(c) >= 0x20)
            out.push_back(c);
    }
    return out;
}

PlayerbotAI* ResolveBot(ChatHandler* handler, std::string const& name, Player*& bot)
{
    bot = ObjectAccessor::FindPlayerByName(name);
    if (!bot)
    {
        handler->SendSysMessage("llmprobe: no online player by that name");
        return nullptr;
    }
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        handler->SendSysMessage("llmprobe: not a bot");
    return botAI;
}

void AppendUnitList(std::ostringstream& os, PlayerbotAI* botAI, Player* bot, char const* valueName,
                    bool includeHealth = true, bool includeBuffs = false)
{
    os << '[';
    bool first = true;
    for (ObjectGuid const& guid : botAI->GetAiObjectContext()->GetValue<GuidVector>(valueName)->Get())
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;
        if (!first)
            os << ',';
        first = false;
        os << "{\"name\":\"" << Esc(unit->GetName()) << "\",\"level\":" << uint32(unit->GetLevel());
        if (includeHealth)
            os << ",\"hp_pct\":" << uint32(unit->GetHealthPct());
        os << ",\"dist\":" << uint32(bot->GetDistance(unit));
        if (includeBuffs)
        {
            // The buffs the bot can see on this unit (visible positive auras).
            os << ",\"buffs\":[";
            bool firstBuff = true;
            std::set<std::string> seen;
            for (auto const& [spellId, aurApp] : unit->GetAppliedAuras())
            {
                Aura const* aura = aurApp ? aurApp->GetBase() : nullptr;
                SpellInfo const* info = aura ? aura->GetSpellInfo() : nullptr;
                if (!info || info->IsPassive() || !info->SpellName[0] || !aurApp->IsPositive())
                    continue;
                if (seen.size() >= 12)
                    break;
                if (!seen.insert(info->SpellName[0]).second)
                    continue;
                os << (firstBuff ? "" : ",") << '"' << Esc(info->SpellName[0]) << '"';
                firstBuff = false;
            }
            os << ']';
        }
        os << '}';
    }
    os << ']';
}
}  // namespace

void AppendBotJson(std::ostringstream& os, Player* bot, PlayerbotAI* botAI)
{
    AiObjectContext* context = botAI->GetAiObjectContext();
    os << "{\"t\":" << time(nullptr);
    os << ",\"name\":\"" << Esc(bot->GetName()) << "\",\"guid\":" << bot->GetGUID().GetCounter();
    os << ",\"race\":" << uint32(bot->getRace()) << ",\"class\":" << uint32(bot->getClass());
    os << ",\"level\":" << uint32(bot->GetLevel());

    AreaTableEntry const* area = botAI->GetCurrentArea();
    AreaTableEntry const* zone = botAI->GetCurrentZone();
    os << ",\"pos\":{\"map\":" << bot->GetMapId() << ",\"zone\":\""
       << Esc(zone ? PlayerbotAI::GetLocalizedAreaName(zone) : "") << "\",\"area\":\""
       << Esc(area ? PlayerbotAI::GetLocalizedAreaName(area) : "") << "\",\"x\":" << bot->GetPositionX()
       << ",\"y\":" << bot->GetPositionY() << ",\"z\":" << bot->GetPositionZ() << '}';

    os << ",\"vitals\":{\"hp_pct\":" << uint32(bot->GetHealthPct())
       << ",\"mana_pct\":" << uint32(bot->GetPowerPct(POWER_MANA))
       << ",\"dead\":" << (bot->isDead() ? 1 : 0)
       << ",\"ghost\":" << (bot->HasPlayerFlag(PLAYER_FLAGS_GHOST) ? 1 : 0)
       << ",\"in_combat\":" << (bot->IsInCombat() ? 1 : 0)
       << ",\"mounted\":" << (bot->IsMounted() ? 1 : 0)
       << ",\"in_flight\":" << (bot->IsInFlight() ? 1 : 0)
       << ",\"resting\":" << (bot->HasPlayerFlag(PLAYER_FLAGS_RESTING) ? 1 : 0)
       << ",\"xp_pct\":" << (bot->GetUInt32Value(PLAYER_NEXT_LEVEL_XP)
                                 ? bot->GetUInt32Value(PLAYER_XP) * 100 /
                                       bot->GetUInt32Value(PLAYER_NEXT_LEVEL_XP)
                                 : 0)
       << '}';

    char const* state = botAI->GetState() == BOT_STATE_COMBAT ? "combat"
                        : botAI->GetState() == BOT_STATE_DEAD ? "dead"
                                                              : "noncombat";
    os << ",\"ai\":{\"state\":\"" << state << '"';
    os << ",\"rpg\":\"" << Esc(botAI->rpgInfo.ToString()) << '"';

    // Structured "what am I actually doing" — the bot's own life-steering
    // state, resolved to names so the mind narrates truth instead of guessing.
    os << ",\"doing\":";
    {
        NewRpgInfo& rpg = botAI->rpgInfo;
        auto areaName = [](WorldPosition pos) { return pos.getAreaName(false, false); };
        uint32 flightDest = 0;
        if (bot->IsInFlight() && !bot->m_taxi.GetPath().empty())
            flightDest = bot->m_taxi.GetPath().back();
        if (flightDest)
        {
            TaxiNodesEntry const* node = sTaxiNodesStore.LookupEntry(flightDest);
            os << "{\"status\":\"flight\",\"to\":\"" << Esc(node ? node->name[0] : "") << "\"}";
        }
        else switch (rpg.GetStatus())
        {
            case RPG_DO_QUEST:
            {
                auto& d = std::get<NewRpgInfo::DoQuest>(rpg.data);
                Quest const* quest = d.quest ? d.quest : sObjectMgr->GetQuestTemplate(d.questId);
                os << "{\"status\":\"quest\",\"quest\":\"" << Esc(quest ? quest->GetTitle() : "") << '"';
                std::string objective;
                if (quest && d.objectiveIdx >= 0 && d.objectiveIdx < QUEST_OBJECTIVES_COUNT)
                {
                    objective = quest->ObjectiveText[d.objectiveIdx];
                    if (objective.empty())
                    {
                        int32 entry = quest->RequiredNpcOrGo[d.objectiveIdx];
                        if (entry > 0)
                        {
                            if (CreatureTemplate const* creature = sObjectMgr->GetCreatureTemplate(entry))
                                objective = creature->Name;
                        }
                        else if (entry < 0)
                        {
                            if (GameObjectTemplate const* go = sObjectMgr->GetGameObjectTemplate(-entry))
                                objective = go->name;
                        }
                    }
                }
                if (!objective.empty())
                    os << ",\"objective\":\"" << Esc(objective) << '"';
                os << '}';
                break;
            }
            case RPG_GO_GRIND:
            {
                auto& d = std::get<NewRpgInfo::GoGrind>(rpg.data);
                os << "{\"status\":\"grind\",\"near\":\"" << Esc(areaName(d.pos)) << "\"}";
                break;
            }
            case RPG_GO_CAMP:
            {
                auto& d = std::get<NewRpgInfo::GoCamp>(rpg.data);
                os << "{\"status\":\"camp\",\"near\":\"" << Esc(areaName(d.pos)) << "\"}";
                break;
            }
            case RPG_TRAVEL_FLIGHT:
            {
                auto& d = std::get<NewRpgInfo::TravelFlight>(rpg.data);
                uint32 destNode = d.path.empty() ? 0 : d.path.back();
                TaxiNodesEntry const* node = destNode ? sTaxiNodesStore.LookupEntry(destNode) : nullptr;
                os << "{\"status\":\"heading_to_flight\",\"to\":\"" << Esc(node ? node->name[0] : "") << "\"}";
                break;
            }
            case RPG_WANDER_NPC:
            {
                auto& d = std::get<NewRpgInfo::WanderNpc>(rpg.data);
                std::string npc;
                if (d.npcOrGo)
                {
                    if (Creature* creature = ObjectAccessor::GetCreature(*bot, d.npcOrGo))
                        npc = creature->GetName();
                    else if (GameObject* go = ObjectAccessor::GetGameObject(*bot, d.npcOrGo))
                        npc = go->GetName();
                }
                os << "{\"status\":\"visit_npc\"";
                if (!npc.empty())
                    os << ",\"npc\":\"" << Esc(npc) << '"';
                os << '}';
                break;
            }
            case RPG_WANDER_RANDOM: os << "{\"status\":\"wander\"}"; break;
            case RPG_REST:          os << "{\"status\":\"rest\"}"; break;
            case RPG_OUTDOOR_PVP:   os << "{\"status\":\"outdoor_pvp\"}"; break;
            default:                os << "{\"status\":\"idle\"}"; break;
        }
    }
    for (auto const& [key, botState] :
         {std::pair{"co", BOT_STATE_COMBAT}, std::pair{"nc", BOT_STATE_NON_COMBAT}})
    {
        os << ",\"" << key << "_strategies\":[";
        bool first = true;
        for (std::string const& strategy : botAI->GetStrategies(botState))
        {
            os << (first ? "\"" : ",\"") << Esc(strategy) << '"';
            first = false;
        }
        os << ']';
    }
    os << '}';

    os << ",\"social\":{\"master\":\"" << Esc(botAI->GetMaster() ? botAI->GetMaster()->GetName() : "") << '"';
    Guild* guild = bot->GetGuildId() ? sGuildMgr->GetGuildById(bot->GetGuildId()) : nullptr;
    os << ",\"guild\":\"" << Esc(guild ? guild->GetName() : "") << '"';
    os << ",\"group\":[";
    if (Group* group = bot->GetGroup())
    {
        bool first = true;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member == bot)
                continue;
            os << (first ? "" : ",") << "{\"name\":\"" << Esc(member->GetName())
               << "\",\"class\":" << uint32(member->getClass()) << ",\"level\":" << uint32(member->GetLevel())
               << ",\"hp_pct\":" << uint32(member->GetHealthPct())
               << ",\"bot\":" << (GET_PLAYERBOT_AI(member) ? 1 : 0)
               << ",\"same_guild\":"
               << ((bot->GetGuildId() && member->GetGuildId() == bot->GetGuildId()) ? 1 : 0)
               << '}';
            first = false;
        }
    }
    os << "]}";

    if (Battleground* bg = bot->GetBattleground())
    {
        os << ",\"bg\":{\"type\":" << uint32(bg->GetBgTypeID(true))
           << ",\"arena\":" << (bg->isArena() ? 1 : 0)
           << ",\"status\":" << uint32(bg->GetStatus())
           << ",\"team\":" << uint32(bot->GetBgTeamId())
           << ",\"role\":" << context->GetValue<uint32>("bg role")->Get() << '}';
    }

    os << ",\"env\":{\"victim\":";
    if (Unit* victim = bot->GetVictim())
        os << "{\"name\":\"" << Esc(victim->GetName()) << "\",\"level\":" << uint32(victim->GetLevel())
           << ",\"hp_pct\":" << uint32(victim->GetHealthPct()) << '}';
    else
        os << "null";
    os << ",\"attackers\":";
    AppendUnitList(os, botAI, bot, "attackers");
    os << ",\"enemy_players\":";
    AppendUnitList(os, botAI, bot, "nearest enemy players", true, true);
    os << ",\"rpg_targets\":"
       << context->GetValue<GuidVector>("possible new rpg targets")->Get().size() << '}';


    uint32 freeSlots = 0;
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (!bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            ++freeSlots;
    for (uint8 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
        if (Bag* bag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            freeSlots += bag->GetFreeSlots();

    os << ",\"quests\":[";
    bool firstQuest = true;
    for (auto const& [questId, questStatus] : bot->getQuestStatusMap())
    {
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest || questStatus.Status == QUEST_STATUS_NONE)
            continue;
        os << (firstQuest ? "" : ",") << "{\"id\":" << questId << ",\"title\":\""
           << Esc(quest->GetTitle()) << "\",\"status\":" << uint32(questStatus.Status) << '}';
        firstQuest = false;
    }
    os << ']';


    std::array<float, BotPersonality::Matrix::AxisCount> facets{};
    bool const hasFacets = BotPersonality::GetBaselines(bot->GetGUID().GetCounter(), facets);

    struct ProfessionDef
    {
        char const* name;
        uint32 skill;
    };
    static ProfessionDef const professionDefs[] = {
        {"alchemy", SKILL_ALCHEMY},
        {"blacksmithing", SKILL_BLACKSMITHING},
        {"enchanting", SKILL_ENCHANTING},
        {"engineering", SKILL_ENGINEERING},
        {"herbalism", SKILL_HERBALISM},
        {"inscription", SKILL_INSCRIPTION},
        {"jewelcrafting", SKILL_JEWELCRAFTING},
        {"leatherworking", SKILL_LEATHERWORKING},
        {"mining", SKILL_MINING},
        {"skinning", SKILL_SKINNING},
        {"tailoring", SKILL_TAILORING},
        {"fishing", SKILL_FISHING},
        {"cooking", SKILL_COOKING},
        {"first aid", SKILL_FIRST_AID},
    };
    std::array<std::set<std::string>, 14> knownRecipes;
    uint32 mountsKnown = 0;
    for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
    {
        if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED)
            continue;
        SpellInfo const* info = sSpellMgr->CheckSpellInfo(spellId);
        if (!info || !info->SpellName[0])
            continue;

        bool mount = false;
        SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
        for (auto itr = bounds.first; itr != bounds.second; ++itr)
        {
            SkillLineAbilityEntry const* ability = itr->second;
            if (!ability)
                continue;
            if (ability->SkillLine == SKILL_MOUNTS)
                mount = true;
            if (info->IsPassive() || info->IsProfession())
                continue;
            for (std::size_t i = 0; i < 14; ++i)
                if (ability->SkillLine == professionDefs[i].skill && bot->HasSkill(professionDefs[i].skill))
                    knownRecipes[i].insert(info->SpellName[0]);
        }
        if (mount)
            ++mountsKnown;
    }

    uint32 const freeTalentPoints = bot->GetFreeTalentPoints();
    uint32 const totalTalentPoints = bot->CalculateTalentsPoints();
    os << ",\"stable\":{\"spec\":{\"tab\":" << uint32(bot->GetActiveSpec())
       << ",\"points_spent\":" << (totalTalentPoints > freeTalentPoints ? totalTalentPoints - freeTalentPoints : 0)
       << ",\"free_points\":" << freeTalentPoints << '}';

    os << ",\"spells\":[";
    bool firstSpell = true;
    for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
    {
        // Active=false marks superseded lower ranks; spec mask gates dual-spec books.
        if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active ||
            !playerSpell->IsInSpec(bot->GetActiveSpec()))
            continue;
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo || spellInfo->IsPassive() || !spellInfo->SpellName[0] || !*spellInfo->SpellName[0])
            continue;
        // Profession casts, gathering, and generic craft spells live in
        // known_recipes / inventory verbs already.
        bool craft = false;
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            switch (spellInfo->Effects[i].Effect)
            {
                case SPELL_EFFECT_TRADE_SKILL:
                case SPELL_EFFECT_OPEN_LOCK:
                case SPELL_EFFECT_SKINNING:
                case SPELL_EFFECT_DISENCHANT:
                case SPELL_EFFECT_PROSPECTING:
                    craft = true;
                    break;
                case SPELL_EFFECT_CREATE_ITEM:
                    if (spellInfo->SpellFamilyName == SPELLFAMILY_GENERIC)
                        craft = true;
                    break;
                default:
                    break;
            }
        if (craft)
            continue;
        char const* targets = spellInfo->NeedsExplicitUnitTarget()
            ? (spellInfo->IsPositive() ? "others" : "enemy")
            : "self";
        if (!firstSpell)
            os << ',';
        firstSpell = false;
        os << "{\"id\":" << spellId << ",\"n\":\"" << Esc(spellInfo->SpellName[0]) << '"';
        if (spellInfo->Rank[0] && *spellInfo->Rank[0])
            os << ",\"r\":\"" << Esc(spellInfo->Rank[0]) << '"';
        os << ",\"t\":\"" << targets << "\"}";
    }
    os << ']';

    os << ",\"known_recipes\":{";
    bool firstProfession = true;
    for (std::size_t i = 0; i < 14; ++i)
    {
        if (!bot->HasSkill(professionDefs[i].skill))
            continue;
        os << (firstProfession ? "\"" : ",\"") << professionDefs[i].name << "\":[";
        bool firstRecipe = true;
        for (std::string const& recipe : knownRecipes[i])
        {
            os << (firstRecipe ? "\"" : ",\"") << Esc(recipe) << '"';
            firstRecipe = false;
        }
        os << ']';
        firstProfession = false;
    }
    os << '}';

    static char const* const equipmentSlotNames[] = {
        "head", "neck", "shoulders", "shirt", "chest", "waist", "legs", "feet", "wrists", "hands",
        "finger1", "finger2", "trinket1", "trinket2", "back", "main_hand", "off_hand", "ranged", "tabard",
    };
    os << ",\"gear\":[";
    bool firstGear = true;
    std::map<uint32, uint32> equippedSets;  // set id -> equipped pieces
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        ItemTemplate const* itemTemplate = item ? item->GetTemplate() : nullptr;
        if (!item || !itemTemplate)
            continue;
        uint32 const durability = item->GetUInt32Value(ITEM_FIELD_DURABILITY);
        uint32 const maxDurability = item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY);
        uint32 const durabilityPct = maxDurability ? durability * 100 / maxDurability : 100;
        os << (firstGear ? "" : ",") << "{\"slot\":\"" << equipmentSlotNames[slot] << "\",\"name\":\""
           << Esc(itemTemplate->Name1) << "\",\"ilvl\":" << itemTemplate->ItemLevel
           << ",\"quality\":" << itemTemplate->Quality << ",\"durability_pct\":" << durabilityPct;
        if (itemTemplate->Armor)
            os << ",\"armor\":" << itemTemplate->Armor;
        if (itemTemplate->Damage[0].DamageMax > 0)
            os << ",\"dmg\":{\"min\":" << uint32(itemTemplate->Damage[0].DamageMin)
               << ",\"max\":" << uint32(itemTemplate->Damage[0].DamageMax)
               << ",\"speed\":" << itemTemplate->Delay / 1000.0 << '}';
        if (itemTemplate->StatsCount)
        {
            os << ",\"stats\":[";
            for (uint32 i = 0; i < itemTemplate->StatsCount; ++i)
            {
                if (i)
                    os << ',';
                os << "{\"t\":" << itemTemplate->ItemStat[i].ItemStatType
                   << ",\"v\":" << itemTemplate->ItemStat[i].ItemStatValue << '}';
            }
            os << ']';
        }
        bool firstEffect = true;
        for (_Spell const& effect : itemTemplate->Spells)
        {
            if (effect.SpellId <= 0 || effect.SpellTrigger > ITEM_SPELLTRIGGER_CHANCE_ON_HIT)
                continue;
            os << (firstEffect ? ",\"effects\":[" : ",") << "{\"trigger\":" << effect.SpellTrigger
               << ",\"id\":" << effect.SpellId << '}';
            firstEffect = false;
        }
        if (!firstEffect)
            os << ']';
        // Permanent enchant + gems; the DBC description is the tooltip line.
        bool firstEnchant = true;
        for (EnchantmentSlot enchantSlot : {PERM_ENCHANTMENT_SLOT, SOCK_ENCHANTMENT_SLOT,
                                            SOCK_ENCHANTMENT_SLOT_2, SOCK_ENCHANTMENT_SLOT_3})
        {
            SpellItemEnchantmentEntry const* enchant =
                sSpellItemEnchantmentStore.LookupEntry(item->GetEnchantmentId(enchantSlot));
            if (!enchant || !enchant->description[0] || !*enchant->description[0])
                continue;
            os << (firstEnchant ? ",\"enchants\":[\"" : ",\"") << Esc(enchant->description[0]) << '"';
            firstEnchant = false;
        }
        if (!firstEnchant)
            os << ']';
        if (itemTemplate->ItemSet)
        {
            os << ",\"set_id\":" << itemTemplate->ItemSet;
            ++equippedSets[itemTemplate->ItemSet];
        }
        os << '}';
        firstGear = false;
    }
    os << ']';

    os << ",\"gear_sets\":[";
    bool firstSet = true;
    for (auto const& [setId, equipped] : equippedSets)
    {
        ItemSetEntry const* set = sItemSetStore.LookupEntry(setId);
        if (!set)
            continue;
        os << (firstSet ? "" : ",") << "{\"name\":\"" << Esc(set->name[0])
           << "\",\"equipped\":" << equipped << ",\"bonuses\":[";
        bool firstBonus = true;
        for (uint8 i = 0; i < MAX_ITEM_SET_SPELLS; ++i)
        {
            if (!set->spells[i])
                continue;
            os << (firstBonus ? "" : ",") << "{\"pieces\":" << set->items_to_triggerspell[i]
               << ",\"id\":" << set->spells[i] << '}';
            firstBonus = false;
        }
        os << "]}";
        firstSet = false;
    }
    os << ']';

    os << ",\"professions\":{";
    firstProfession = true;
    for (ProfessionDef const& profession : professionDefs)
    {
        if (!bot->HasSkill(profession.skill))
            continue;
        os << (firstProfession ? "\"" : ",\"") << profession.name << "\":" << bot->GetSkillValue(profession.skill);
        firstProfession = false;
    }
    os << '}';

    std::string guildRank;
    if (guild)
        if (Guild::Member const* member = guild->GetMember(bot->GetGUID()))
            guildRank = guild->GetRankName(member->GetRankId());
    os << ",\"guild_rank\":\"" << Esc(guildRank) << '"';

    AreaTableEntry const* homeArea = sAreaTableStore.LookupEntry(bot->m_homebindAreaId);
    os << ",\"hearthbind\":{\"area\":\""
       << Esc(homeArea ? PlayerbotAI::GetLocalizedAreaName(homeArea) : "") << "\"}";
    os << ",\"mounts_known\":" << mountsKnown;

    os << ",\"dials\":{";
    bool firstDial = true;
    for (auto const& dial : BotPersonality::Matrix::Dials)
    {
        os << (firstDial ? "\"" : ",\"") << dial.configName << "\":"
           << context->GetValue<float>(dial.valueName)->Get();
        firstDial = false;
    }
    os << '}';

    os << ",\"facets_z\":[";
    if (hasFacets)
        for (std::size_t i = 0; i < facets.size(); ++i)
            os << (i ? "," : "") << facets[i];
    os << ']';
    os << ",\"pvp_bio_share\":" << BotPersonality::PvpBiographyShare(bot->GetGUID().GetCounter()) << '}';

    // Full inventory, aggregated by item name — the mind can recall the
    // complete list; nothing is dropped.
    os << ",\"wake\":{\"bags\":[";
    std::map<std::string, std::pair<uint32, uint32>> bagItems;  // name -> {count, quality}
    auto collectBagItem = [&](Item* item)
    {
        ItemTemplate const* itemTemplate = item ? item->GetTemplate() : nullptr;
        if (!item || !itemTemplate)
            return;
        auto& entry = bagItems[itemTemplate->Name1];
        entry.first += item->GetCount();
        entry.second = itemTemplate->Quality;
    };
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        collectBagItem(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
    {
        Bag* bag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bagSlot);
        if (!bag)
            continue;
        for (uint8 slot = 0; slot < bag->GetBagSize(); ++slot)
            collectBagItem(bag->GetItemByPos(slot));
    }
    bool firstBagItem = true;
    for (auto const& [name, entry] : bagItems)
    {
        os << (firstBagItem ? "" : ",") << "{\"name\":\"" << Esc(name)
           << "\",\"count\":" << entry.first << ",\"quality\":" << entry.second << '}';
        firstBagItem = false;
    }
    os << "],\"free_bag_slots\":" << freeSlots;

    uint32 const money = bot->GetMoney();
    os << ",\"money\":{\"g\":" << money / 10000 << ",\"s\":" << money / 100 % 100
       << ",\"c\":" << money % 100 << '}';

    // Character-sheet aggregates; unit fields already include gear and auras,
    // so the numbers match what the player would see on their sheet right now.
    {
        int32 spellPower = 0;
        float spellCrit = 0.0f;
        for (uint8 school = SPELL_SCHOOL_HOLY; school < MAX_SPELL_SCHOOL; ++school)
        {
            spellPower = std::max(spellPower,
                bot->SpellBaseDamageBonusDone(SpellSchoolMask(1 << school)));
            spellCrit = std::max(spellCrit,
                bot->GetFloatValue(static_cast<uint16>(PLAYER_SPELL_CRIT_PERCENTAGE1) + school));
        }
        auto rating = [bot](CombatRating cr)
        { return bot->GetUInt32Value(static_cast<uint16>(PLAYER_FIELD_COMBAT_RATING_1) + cr); };
        os << ",\"stats\":{"
           << "\"health\":" << bot->GetMaxHealth()
           << ",\"mana\":" << bot->GetMaxPower(POWER_MANA)
           << ",\"strength\":" << uint32(bot->GetStat(STAT_STRENGTH))
           << ",\"agility\":" << uint32(bot->GetStat(STAT_AGILITY))
           << ",\"stamina\":" << uint32(bot->GetStat(STAT_STAMINA))
           << ",\"intellect\":" << uint32(bot->GetStat(STAT_INTELLECT))
           << ",\"spirit\":" << uint32(bot->GetStat(STAT_SPIRIT))
           << ",\"armor\":" << bot->GetArmor()
           << ",\"attack_power\":" << uint32(bot->GetTotalAttackPowerValue(BASE_ATTACK))
           << ",\"ranged_attack_power\":" << uint32(bot->GetTotalAttackPowerValue(RANGED_ATTACK))
           << ",\"spell_power\":" << spellPower
           << ",\"healing_power\":" << bot->SpellBaseHealingBonusDone(SPELL_SCHOOL_MASK_ALL)
           << ",\"melee_crit\":" << bot->GetFloatValue(PLAYER_CRIT_PERCENTAGE)
           << ",\"ranged_crit\":" << bot->GetFloatValue(PLAYER_RANGED_CRIT_PERCENTAGE)
           << ",\"spell_crit\":" << spellCrit
           << ",\"hit_rating\":" << rating(CR_HIT_MELEE)
           << ",\"haste_rating\":" << rating(CR_HASTE_MELEE)
           << ",\"expertise_rating\":" << rating(CR_EXPERTISE)
           << ",\"resilience\":" << rating(CR_CRIT_TAKEN_MELEE)
           << ",\"defense\":" << bot->GetDefenseSkillValue()
           << ",\"dodge\":" << bot->GetFloatValue(PLAYER_DODGE_PERCENTAGE)
           << ",\"parry\":" << bot->GetFloatValue(PLAYER_PARRY_PERCENTAGE)
           << ",\"block\":" << bot->GetFloatValue(PLAYER_BLOCK_PERCENTAGE)
           << ",\"resist\":{"
           << "\"fire\":" << bot->GetResistance(SPELL_SCHOOL_MASK_FIRE)
           << ",\"nature\":" << bot->GetResistance(SPELL_SCHOOL_MASK_NATURE)
           << ",\"frost\":" << bot->GetResistance(SPELL_SCHOOL_MASK_FROST)
           << ",\"shadow\":" << bot->GetResistance(SPELL_SCHOOL_MASK_SHADOW)
           << ",\"arcane\":" << bot->GetResistance(SPELL_SCHOOL_MASK_ARCANE)
           << "}}";
    }

    os << ",\"auras\":[";
    {
        bool first = true;
        std::set<std::string> seen;
        for (auto const& [spellId, aurApp] : bot->GetAppliedAuras())
        {
            Aura const* aura = aurApp ? aurApp->GetBase() : nullptr;
            SpellInfo const* info = aura ? aura->GetSpellInfo() : nullptr;
            if (!info || info->IsPassive() || !info->SpellName[0])
                continue;
            if (!seen.insert(info->SpellName[0]).second)
                continue;
            int32 const duration = aura->GetDuration();
            os << (first ? "" : ",") << "{\"id\":" << info->Id
               << ",\"name\":\"" << Esc(info->SpellName[0])
               << "\",\"positive\":" << (aurApp->IsPositive() ? 1 : 0)
               << ",\"remaining_s\":" << (duration < 0 ? -1 : duration / 1000) << '}';
            first = false;
        }
    }
    os << ']';

    // Multiple ranks/category entries share a spell name; keep the longest
    // remaining cooldown per name.
    os << ",\"cooldowns\":[";
    std::map<std::string, uint32> cooldowns;  // name -> remaining ms
    for (auto const& spellCooldown : bot->GetSpellCooldownMap())
    {
        uint32 const spellId = spellCooldown.first;
        uint32 const remaining = bot->GetSpellCooldownDelay(spellId);
        // +MONTH marks "cooldown starts when the effect ends" (see
        // infinityCooldownDelay); that is not a ticking cooldown.
        SpellInfo const* info = remaining && remaining < infinityCooldownDelayCheck
            ? sSpellMgr->CheckSpellInfo(spellId) : nullptr;
        if (!info || !info->SpellName[0])
            continue;
        uint32& slot = cooldowns[info->SpellName[0]];
        slot = std::max(slot, remaining);
    }
    bool firstCooldown = true;
    for (auto const& [name, remaining] : cooldowns)
    {
        os << (firstCooldown ? "" : ",") << "{\"spell\":\"" << Esc(name)
           << "\",\"remaining_s\":" << (remaining + 999) / 1000 << '}';
        firstCooldown = false;
    }
    os << ']';

    os << ",\"group\":[";
    if (Group* group = bot->GetGroup())
    {
        bool first = true;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member == bot)
                continue;
            char const* role = PlayerbotAI::IsTank(member, true) ? "tank"
                               : PlayerbotAI::IsHeal(member, true) ? "healer"
                               : PlayerbotAI::IsDps(member, true)  ? "dps"
                                                                  : "unknown";
            os << (first ? "" : ",") << "{\"name\":\"" << Esc(member->GetName())
               << "\",\"class\":" << uint32(member->getClass()) << ",\"role\":\"" << role
               << "\",\"hp_pct\":" << uint32(member->GetHealthPct())
               << ",\"same_guild\":"
               << ((bot->GetGuildId() && member->GetGuildId() == bot->GetGuildId()) ? 1 : 0)
               << '}';
            first = false;
        }
    }
    os << ']';

    os << ",\"target_of_target\":";
    Unit* victim = bot->GetVictim();
    Unit* targetOfTarget = victim ? victim->GetVictim() : nullptr;
    if (targetOfTarget)
        os << "{\"name\":\"" << Esc(targetOfTarget->GetName()) << "\"}";
    else
        os << "null";

    os << ",\"nearby_friendly\":";
    AppendUnitList(os, botAI, bot, "nearest friendly players", false, true);
    os << ",\"bg_role\":";
    if (bot->GetBattleground())
        os << context->GetValue<uint32>("bg role")->Get();
    else
        os << "null";
    os << '}';
    os << '}';
}

bool HandleTap(ChatHandler* handler, char const* args)
{
    Player* bot = nullptr;
    PlayerbotAI* botAI = ResolveBot(handler, args ? args : "", bot);
    if (!botAI)
        return false;

    std::ostringstream os;
    AppendBotJson(os, bot, botAI);
    std::string const json = os.str();
    if (FILE* f = fopen("/tmp/llmtap.json", "w"))
    {
        fwrite(json.data(), 1, json.size(), f);
        fclose(f);
    }
    handler->PSendSysMessage("llmtap: {} bytes -> /tmp/llmtap.json", uint32(json.size()));
    return true;
}

bool HandleSnap(ChatHandler* handler, char const* args)
{
    std::string filter = args ? args : "";
    filter.erase(0, filter.find_first_not_of(' '));
    filter.erase(filter.find_last_not_of(' ') + 1);

    FILE* f = fopen("/tmp/llmsnap.jsonl", "w");
    if (!f)
    {
        handler->SendSysMessage("llmsnap: cannot open /tmp/llmsnap.jsonl");
        return false;
    }

    uint32 count = 0;
    for (auto it = sRandomPlayerbotMgr.GetPlayerBotsBegin(); it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
    {
        Player* bot = it->second;
        if (!bot || !bot->IsInWorld())
            continue;
        if (!filter.empty() && bot->GetName() != filter)
            continue;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI)
            continue;
        std::ostringstream os;
        AppendBotJson(os, bot, botAI);
        std::string const json = os.str();
        fwrite(json.data(), 1, json.size(), f);
        fputc('\n', f);
        ++count;
    }
    fclose(f);
    handler->PSendSysMessage("llmsnap: {} bots -> /tmp/llmsnap.jsonl", count);
    return true;
}

bool HandleSteer(ChatHandler* handler, char const* args)
{
    std::istringstream in(args ? args : "");
    std::string name, verb;
    in >> name >> verb;
    Player* bot = nullptr;
    PlayerbotAI* botAI = ResolveBot(handler, name, bot);
    if (!botAI || verb.empty())
        return false;

    std::string rest;
    std::getline(in, rest);
    if (!rest.empty() && rest.front() == ' ')
        rest.erase(0, 1);

    if (verb == "rpg")
    {
        NewRpgInfo& info = botAI->rpgInfo;
        if (rest == "idle") info.ChangeToIdle();
        else if (rest == "rest") info.ChangeToRest();
        else if (rest == "wander") info.ChangeToWanderRandom();
        else if (rest == "wandernpc") info.ChangeToWanderNpc();
        else if (rest == "pvp") info.ChangeToOutdoorPvp();
        else if (rest == "grindhere") info.ChangeToGoGrind(WorldPosition(bot));
        else
        {
            handler->SendSysMessage("llmsteer rpg: idle|rest|wander|wandernpc|pvp|grindhere");
            return false;
        }
        handler->PSendSysMessage("llmsteer: rpg -> {}", botAI->rpgInfo.ToString());
        return true;
    }
    if (verb == "strategy")
    {
        std::string scope = rest.substr(0, rest.find(' '));
        std::string list = rest.size() > scope.size() ? rest.substr(scope.size() + 1) : "";
        BotState botState = scope == "co" ? BOT_STATE_COMBAT : BOT_STATE_NON_COMBAT;
        botAI->ChangeStrategy(list, botState);
        std::string joined;
        for (std::string const& strategy : botAI->GetStrategies(botState))
            joined += (joined.empty() ? "" : ",") + strategy;
        handler->PSendSysMessage("llmsteer: {} strategies now {}", scope, joined);
        return true;
    }
    if (verb == "goto")
    {
        float x, y, z;
        std::istringstream coords(rest);
        if (coords >> x >> y >> z)
        {
            bot->GetMotionMaster()->MovePoint(0, x, y, z);
            handler->SendSysMessage("llmsteer: MovePoint issued");
            return true;
        }
        handler->SendSysMessage("llmsteer goto: <x> <y> <z>");
        return false;
    }
    if (verb == "say")
    {
        bot->Say(rest, LANG_UNIVERSAL);
        return true;
    }
    if (verb == "dial")
    {
        std::size_t const split = rest.rfind(' ');
        if (split == std::string::npos)
            return false;
        std::string const dialName = rest.substr(0, split);
        float const value = strtof(rest.c_str() + split + 1, nullptr);
        botAI->GetAiObjectContext()->GetValue<float>(dialName)->Set(value);
        handler->PSendSysMessage("llmsteer: {} = {}", dialName, value);
        return true;
    }
    if (verb == "action")
    {
        bool const ok = botAI->DoSpecificAction(rest, Event(), true);
        handler->PSendSysMessage("llmsteer: action '{}' -> {}", rest, ok ? "ok" : "failed");
        return true;
    }
    handler->SendSysMessage("llmsteer verbs: rpg|strategy|goto|say|dial|action");
    return false;
}
}  // namespace LlmProbe
