#ifndef MOD_PLAYERBOTS_PERSONALITY_MATRIX_H
#define MOD_PLAYERBOTS_PERSONALITY_MATRIX_H

#include <array>
#include <cstdint>

namespace BotPersonality::Matrix
{
constexpr std::uint32_t SchemaVersion = 1;
constexpr float FacetNoise = 0.775f;
constexpr float FacetFactor = 0.632f;
// Altruism loadings are normalized to unit variance: .8165^2 + 2*.4082^2 = 1.0.
constexpr float AltruismNoise = 0.8165f;
constexpr float AltruismEmotion = 0.4082f;
constexpr float AltruismAgreeableness = 0.4082f;
constexpr float DialLogisticScale = 1.5f;
constexpr float AdaptationCap = 0.5f;
constexpr float EventImpulseCap = 0.1f;
constexpr float PriorOffsetCap = 0.5f;
constexpr float DecayHalfLifeDays = 30.0f;
constexpr float GrudgeHalfLifeDays = 7.0f;
constexpr float GrudgeNoticeFloor = 0.05f;
// Contaminated normal; TailRenorm = 1/sqrt(1-p+p*s^2) keeps sigma at 1.
constexpr float TailProbability = 0.08f;
constexpr float TailScale = 1.9f;
constexpr float TailRenorm = 0.90955f;
// Couples traits to own-spec choice; population composition barely moves.
constexpr float SpecAffinityTemperature = 3.0f;
constexpr float SpecMultiplierFloor = 0.1f;
constexpr float SpecMultiplierCeiling = 10.0f;

// Order mirrors the Axes table in BotPersonality.cpp (static_asserted there).
enum Axis : std::uint8_t
{
    sinc, fair, greed, modest,          // honesty-humility
    fear, anx, dep, sent,               // emotionality
    esteem, bold, social, lively,       // extraversion
    forgive, gentle, flex, patient,     // agreeableness
    organized, diligent, perfect, prud, // conscientiousness
    aesth, inquis, creative, unconv,    // openness
    altru,
    v_sec, v_aff, v_ben, v_mast, v_wealth, v_auto, v_nov, v_duty
};
constexpr std::size_t AxisCount = v_duty + 1;

// Weights multiply facet z-scores; drift impulses, prior offsets, and caps are sigma shifts.
struct AxisCoefficient
{
    std::uint8_t axis;
    float weight;
};

struct DialDefinition
{
    char const* valueName;
    char const* configName;
    float low;
    float neutral;
    float high;
    std::array<AxisCoefficient, 8> coefficients;
    std::uint8_t count;
};

// Neutral is the stock-equivalent value; raw 0 maps here.
constexpr std::array<DialDefinition, 25> Dials = {{
    {"trait caution", "Caution", 0.6f, 1.0f, 1.8f, {{{fear,.8f},{anx,.5f},{prud,.4f},{v_sec,.3f},{v_nov,-.3f}}}, 5},
    {"trait bravado", "Bravado", 0.7f, 1.0f, 1.5f, {{{fear,-.7f},{bold,.5f},{v_mast,.3f}}}, 3},
    {"trait threat care", "ThreatCare", 0.7f, 1.0f, 1.4f, {{{prud,.6f},{patient,.4f},{bold,-.3f}}}, 3},
    {"trait thrift", "Thrift", 0.6f, 1.0f, 1.6f, {{{prud,.7f},{v_sec,.4f},{perfect,.2f}}}, 3},
    {"trait gank", "Gank", 0.0f, 1.0f, 1.6f, {{{patient,-.5f},{gentle,-.4f},{altru,-.4f},{bold,.5f},{v_mast,.4f},{fear,-.4f}}}, 6},
    {"trait duel accept", "DuelAccept", 75.0f, 90.0f, 97.0f, {{{bold,.5f},{v_mast,.4f},{fear,-.4f}}}, 3},
    {"trait duel start", "DuelStart", 0.0f, 0.25f, 0.6f, {{{bold,.6f},{v_mast,.4f},{gentle,-.3f}}}, 3},
    {"trait mode grind", "ModeGrind", 0.4f, 1.0f, 2.5f, {{{diligent,.5f},{v_mast,.4f},{v_wealth,.3f},{v_nov,-.2f}}}, 4},
    {"trait mode quest", "ModeQuest", 0.4f, 1.0f, 2.5f, {{{inquis,.5f},{diligent,.4f},{v_duty,.3f}}}, 3},
    {"trait mode pvp", "ModePvp", 0.4f, 1.0f, 2.5f, {{{fear,-.5f},{bold,.4f},{v_mast,.4f},{altru,-.3f}}}, 4},
    {"trait mode rest", "ModeRest", 0.4f, 1.0f, 2.5f, {{{prud,.4f},{v_sec,.3f},{diligent,-.4f}}}, 3},
    {"trait mode wander", "ModeWander", 0.4f, 1.0f, 2.5f, {{{inquis,.5f},{v_nov,.4f},{unconv,.3f}}}, 3},
    {"trait reply", "Reply", 0.3f, 1.0f, 3.0f, {{{social,.6f},{lively,.4f},{bold,.2f}}}, 3},
    {"trait expressive", "Expressive", 0.3f, 1.0f, 3.0f, {{{lively,.5f},{social,.4f},{v_aff,.3f}}}, 3},
    {"trait warmth", "Warmth", 0.0f, 0.5f, 1.0f, {{{altru,.6f},{gentle,.4f},{social,.3f}}}, 3},
    {"trait grouper", "Grouper", 0.0f, 0.5f, 1.0f, {{{social,.6f},{dep,.4f},{v_aff,.3f},{bold,.6f},{diligent,.4f}}}, 5},
    // Combat-experience appetites; SpecStatExperience.h maps them onto stats per spec.
    {"trait safety appetite", "AppetiteSafety", 0.75f, 1.0f, 1.3333f, {{{fear,.5f},{v_sec,.4f},{anx,.3f},{bold,-.2f}}}, 4},
    {"trait reliability appetite", "AppetiteReliability", 0.75f, 1.0f, 1.3333f, {{{perfect,.5f},{v_mast,.4f},{organized,.2f}}}, 3},
    {"trait volatility appetite", "AppetiteVolatility", 0.75f, 1.0f, 1.3333f, {{{v_nov,.4f},{fear,-.4f},{prud,-.2f}}}, 3},
    {"trait tempo appetite", "AppetiteTempo", 0.75f, 1.0f, 1.3333f, {{{lively,.4f},{v_nov,.3f}}}, 2},
    {"trait selfsufficiency appetite", "AppetiteSelfSufficiency", 0.75f, 1.0f, 1.3333f, {{{v_auto,.4f},{prud,.3f}}}, 2},
    // Attachment and Upkeep are one-sided: neutral == an endpoint == stock behavior.
    // Vengeance above 0.5 hunts remembered gankers, below 0.5 avoids them.
    {"trait gear standards", "GearStandards", 0.90f, 1.0f, 1.30f, {{{prud,.45f},{greed,.3f},{perfect,-.4f},{v_wealth,-.35f}}}, 4},
    {"trait gear attachment", "GearAttachment", 1.0f, 1.0f, 1.12f, {{{sent,.5f},{v_wealth,.3f},{greed,-.2f}}}, 3},
    {"trait gear upkeep", "GearUpkeep", 0.55f, 1.0f, 1.0f, {{{organized,.4f},{diligent,.35f},{perfect,.2f},{v_duty,.2f}}}, 4},
    {"trait vengeance", "Vengeance", 0.0f, 0.5f, 1.0f, {{{forgive,-.6f},{patient,-.3f},{bold,.3f}}}, 3}
}};

struct DriftDefinition
{
    char const* event;
    std::array<AxisCoefficient, 3> impulses;
    std::uint8_t count;
};

constexpr std::array<DriftDefinition, 10> Drift = {{
    {"died to creature", {{{fear,.03f},{v_sec,.02f}}}, 2},
    {"died in battleground", {}, 0},
    {"ganked in world", {{{fear,.08f},{v_sec,.03f},{forgive,-.03f}}}, 3},
    {"killed in battleground", {{{bold,.01f}}}, 1},
    {"killed player in world", {{{bold,.05f},{v_mast,.03f}}}, 2},
    {"level up", {{{diligent,.01f},{v_mast,.01f}}}, 2},
    {"quest complete", {{{v_duty,.01f},{inquis,.005f}}}, 2},
    {"joined group", {{{social,.02f},{v_aff,.01f}}}, 2},
    {"won battleground", {{{esteem,.02f},{bold,.01f}}}, 2},
    {"lost battleground", {{{esteem,-.02f},{anx,.01f}}}, 2}
}};

struct ClassPriorDefinition
{
    std::array<AxisCoefficient, 5> offsets;
    std::uint8_t count;
};

constexpr ClassPriorDefinition Prior(
    AxisCoefficient first, AxisCoefficient second, AxisCoefficient third, AxisCoefficient fourth)
{
    return {std::array<AxisCoefficient, 5>{first, second, third, fourth}, 4};
}

constexpr ClassPriorDefinition Prior(
    AxisCoefficient first, AxisCoefficient second, AxisCoefficient third, AxisCoefficient fourth,
    AxisCoefficient fifth)
{
    return {std::array<AxisCoefficient, 5>{first, second, third, fourth, fifth}, 5};
}

constexpr ClassPriorDefinition Prior(AxisCoefficient first, AxisCoefficient second, AxisCoefficient third)
{
    return {std::array<AxisCoefficient, 5>{first, second, third}, 3};
}

// ChrClasses.dbc ids; slots 0 and 10 empty.
constexpr std::array<ClassPriorDefinition, 12> ClassPriors = {
    ClassPriorDefinition{},
    Prior({fear,-.25f}, {bold,.20f}, {diligent,.20f}, {v_duty,.25f}, {gentle,-.15f}),   // warrior: low fear, bold, dutiful conformity, blunt
    Prior({fair,.25f}, {forgive,.20f}, {v_duty,.30f}, {diligent,.15f}, {sinc,.15f}),     // paladin: clergy honesty-humility + obedience
    Prior({inquis,.20f}, {v_auto,.30f}, {diligent,.20f}, {social,-.10f}, {prud,.15f}),  // hunter: autonomous wilderness self-selection
    Prior({fair,-.30f}, {diligent,.25f}, {prud,.20f}, {unconv,.15f}, {bold,-.10f}),   // rogue: organized offender — planful, not impulsive
    Prior({forgive,.25f}, {altru,.25f}, {fair,.20f}, {anx,-.20f}, {v_ben,.20f}),    // priest: burnout-resistant caregiver
    Prior({sent,-.30f}, {anx,.25f}, {patient,-.20f}, {forgive,-.20f}, {v_duty,.20f}),  // death knight: numbing, hypervigilance, hostility
    Prior({inquis,.15f}, {unconv,.25f}, {v_aff,.20f}, {aesth,.15f}),              // shaman: self-transcendent openness
    Prior({inquis,.30f}, {diligent,.20f}, {fair,-.15f}, {social,-.15f}, {v_mast,.20f}),  // mage: Feist scientist profile
    Prior({unconv,.30f}, {v_mast,.25f}, {fair,-.25f}, {inquis,.20f}),              // warlock: heterodox power-seeker
    ClassPriorDefinition{},
    Prior({aesth,.25f}, {flex,.20f}, {v_auto,.20f}, {altru,.15f})               // druid: nature-related openness, flexible
};

// Summed with class priors, then the total is clamped to +-PriorOffsetCap per axis.
// ChrRaces.dbc ids; slots 0 and 9 unused.
constexpr std::array<ClassPriorDefinition, 12> RacePriors = {
    ClassPriorDefinition{},
    ClassPriorDefinition{},                                          // human: reference anchor
    Prior({bold,.25f}, {patient,-.20f}, {fair,.15f}, {v_duty,.20f}),                // orc: honor culture, post-Thrall reform
    Prior({organized,.25f}, {diligent,.20f}, {v_duty,.20f}, {unconv,-.15f}),              // dwarf: tight culture, craft guilds
    Prior({patient,.30f}, {social,-.20f}, {aesth,.25f}, {v_auto,.15f}),              // night elf: long time-horizon, isolationist
    Prior({sent,-.30f}, {fear,-.20f}, {social,-.20f}, {anx,.15f}),               // forsaken: numbing, rejection, post-mortem fear extinction
    Prior({altru,.25f}, {v_duty,.20f}, {forgive,.15f}, {aesth,.15f}),               // tauren: cooperative pastoral collectivism
    Prior({inquis,.30f}, {anx,.20f}, {organized,.15f}, {social,-.15f}),               // gnome: maker culture, fresh homeland loss
    Prior({unconv,.25f}, {fear,-.15f}, {flex,.20f}),                          // troll: loose culture, pragmatic spiritism
    ClassPriorDefinition{},                                          // goblin: not playable in wotlk
    Prior({v_wealth,.30f}, {modest,-.25f}, {anx,.20f}, {patient,-.15f}),               // blood elf: status culture, arcane withdrawal
    Prior({v_aff,.25f}, {anx,-.20f}, {v_duty,.15f}, {forgive,.15f})                // draenei: faith-cohesive refugee diaspora
};

struct SpecAffinityDefinition
{
    std::array<AxisCoefficient, 3> coefficients;
    std::uint8_t count;
};

constexpr SpecAffinityDefinition Affinity(AxisCoefficient first)
{
    return {std::array<AxisCoefficient, 3>{first}, 1};
}

constexpr SpecAffinityDefinition Affinity(AxisCoefficient first, AxisCoefficient second)
{
    return {std::array<AxisCoefficient, 3>{first, second}, 2};
}

using ClassSpecAffinities = std::array<SpecAffinityDefinition, 3>;

// ChrClasses.dbc ids; slots 0 and 10 empty.
constexpr std::array<ClassSpecAffinities, 12> SpecAffinities = {
    ClassSpecAffinities{},
    // warrior: arms mastery-duelist / fury impulsive-aggression / prot guardian
    ClassSpecAffinities{Affinity({v_mast,.25f}, {bold,.15f}), Affinity({lively,.25f}, {patient,-.20f}), Affinity({v_sec,.20f}, {diligent,.15f})},
    // paladin: holy caregiver / prot guardian / ret zealous achiever
    ClassSpecAffinities{Affinity({altru,.30f}, {forgive,.15f}), Affinity({v_sec,.25f}, {v_duty,.15f}), Affinity({v_mast,.25f}, {sinc,.15f})},
    // hunter: BM animal-bonded / MM methodical marksman / surv self-reliant improviser
    ClassSpecAffinities{Affinity({sent,.30f}), Affinity({prud,.25f}, {diligent,.15f}), Affinity({v_auto,.25f}, {inquis,.15f})},
    // rogue: assassination planful contract work / combat brash brawler / subtlety low-visibility
    ClassSpecAffinities{Affinity({diligent,.30f}, {fair,-.20f}), Affinity({lively,.20f}, {bold,.15f}), Affinity({bold,-.25f}, {unconv,.15f})},
    // priest: disc disciplined rule-keeper / holy caregiver / shadow heterodox achiever
    ClassSpecAffinities{Affinity({v_duty,.25f}, {diligent,.15f}), Affinity({altru,.30f}, {forgive,.15f}), Affinity({unconv,.25f}, {v_mast,.15f})},
    // death knight: blood vigilant guardian / frost cold executor / unholy transgressive
    ClassSpecAffinities{Affinity({diligent,.20f}, {anx,.15f}), Affinity({patient,-.20f}, {diligent,.20f}), Affinity({fair,-.25f}, {unconv,.20f})},
    // shaman: ele curious elementalist / enh exuberant brawler / resto communal caregiver
    ClassSpecAffinities{Affinity({inquis,.25f}, {unconv,.15f}), Affinity({lively,.20f}, {bold,.15f}), Affinity({altru,.30f}, {v_aff,.15f})},
    // mage: arcane perfectionist scholar / fire showman / frost risk-averse controller
    ClassSpecAffinities{Affinity({inquis,.35f}, {perfect,.15f}), Affinity({lively,.25f}, {unconv,.15f}), Affinity({prud,.30f})},
    // warlock: affl patient attritionist / demo dominance-seeker / destro flamboyant spender
    ClassSpecAffinities{Affinity({patient,.30f}, {fair,-.15f}), Affinity({v_mast,.25f}, {fair,-.15f}), Affinity({lively,.25f}, {v_wealth,.15f})},
    ClassSpecAffinities{},
    // druid: balance aesthetic mystic / feral autonomous predator / resto gentle caregiver
    ClassSpecAffinities{Affinity({aesth,.30f}), Affinity({v_auto,.30f}), Affinity({gentle,.25f}, {altru,.15f})}
};

struct SpecAffordanceDefinition
{
    std::uint8_t race;
    std::uint8_t playerClass;
    std::uint8_t tab;
    float multiplier;
};

// Unlisted cells are 1.0. [LI] = lore inference.
constexpr std::array<SpecAffordanceDefinition, 21> SpecAffordances = {{
    {5, 5, 2, 2.1f},   // forsaken priest -> shadow: Cult of Forgotten Shadow, the Forsaken faith
    {3, 3, 1, 1.8f},   // dwarf hunter -> marksmanship: Ironforge rifleman tradition
    {10, 8, 0, 1.7f},  // blood elf mage -> arcane: Sunwell addiction + Magister caste
    {10, 2, 2, 1.6f},  // blood elf paladin -> retribution: Blood Knights order
    {8, 3, 0, 1.6f},   // troll hunter -> beast mastery: raptor bonds, loa Gonk
    {2, 1, 1, 1.5f},   // orc warrior -> fury: berserker/blademaster tradition (orc-original)
    {1, 4, 2, 1.5f},   // human rogue -> subtlety: SI:7 espionage institution
    {8, 5, 2, 1.5f},   // troll priest -> shadow: voodoo/witch-doctor loa tradition [LI]
    {7, 9, 1, 1.5f},   // gnome warlock -> demonology: canonical minion-bossing disposition
    {3, 1, 2, 1.4f},   // dwarf warrior -> protection: Mountain King elite tradition
    {4, 11, 1, 1.4f},  // night elf druid -> feral: Druids of the Claw/Fang oldest lineage [LI]
    {6, 3, 2, 1.4f},   // tauren hunter -> survival: nomadic plains-hunter culture [LI]
    {11, 2, 1, 1.4f},  // draenei paladin -> protection: Vindicator caste
    {11, 5, 1, 1.4f},  // draenei priest -> holy: Velen's unbroken Light tradition
    {5, 9, 0, 1.4f},   // forsaken warlock -> affliction: Royal Apothecary Society
    {5, 4, 0, 1.4f},   // forsaken rogue -> assassination: Deathstalkers network
    {1, 2, 2, 1.3f},   // human paladin -> retribution: Silver Hand martial knighthood
    {4, 3, 1, 1.3f},   // night elf hunter -> marksmanship: Sentinel archer corps
    {6, 11, 0, 1.3f},  // tauren druid -> balance: Hamuul Runetotem / Earthmother harmony
    {11, 7, 0, 1.3f},  // draenei shaman -> elemental: Nobundo Farseer lineage [LI]
    {10, 3, 1, 1.3f}   // blood elf hunter -> marksmanship: Farstriders ranger corps
}};

constexpr float SpecAffordance(std::uint8_t race, std::uint8_t playerClass, std::uint8_t tab)
{
    for (SpecAffordanceDefinition const& entry : SpecAffordances)
        if (entry.race == race && entry.playerClass == playerClass && entry.tab == tab)
            return entry.multiplier;
    return 1.0f;
}
}

#endif
