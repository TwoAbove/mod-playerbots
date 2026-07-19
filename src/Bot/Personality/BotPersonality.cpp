#include "BotPersonality.h"
#include "PersonalityMatrix.h"

#include "AiObjectContext.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Player.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <mutex>
#include <random>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <utility>

namespace BotPersonality
{
static void ApplyDials(Player* bot);

using Matrix::AxisCount;

struct Facets
{
    float z[AxisCount]{};
};

struct AxisInfo
{
    char const* key;
    char const* lowAdjective;
    char const* highAdjective;
};

constexpr std::array<AxisInfo, AxisCount> Axes = {{
    {"sincerity", "Sly", "Sincere"}, {"fairness", "Exploitative", "Fair"},
    {"greed_avoidance", "Greedy", "Unmaterialistic"}, {"modesty", "Proud", "Modest"},
    {"fearfulness", "Fearless", "Fearful"}, {"anxiety", "Calm", "Anxious"},
    {"dependence", "Self-reliant", "Dependent"}, {"sentimentality", "Tough", "Sentimental"},
    {"social_self_esteem", "Self-doubting", "Self-assured"}, {"social_boldness", "Shy", "Bold"},
    {"sociability", "Solitary", "Sociable"}, {"liveliness", "Reserved", "Lively"},
    {"forgivingness", "Vindictive", "Forgiving"}, {"gentleness", "Harsh", "Gentle"},
    {"flexibility", "Stubborn", "Flexible"}, {"patience", "Quick-tempered", "Patient"},
    {"organization", "Disorganized", "Organized"}, {"diligence", "Idle", "Diligent"},
    {"perfectionism", "Easygoing", "Perfectionistic"}, {"prudence", "Reckless", "Prudent"},
    {"aesthetic_appreciation", "Unaesthetic", "Aesthetic"}, {"inquisitiveness", "Uncurious", "Inquisitive"},
    {"creativity", "Conventional", "Creative"}, {"unconventionality", "Traditional", "Unconventional"},
    {"altruism", "Selfish", "Altruistic"}, {"v_security", "Daring", "Security-minded"},
    {"v_affiliation", "Independent", "Affiliative"}, {"v_benevolence", "Self-focused", "Benevolent"},
    {"v_mastery", "Yielding", "Mastery-driven"}, {"v_wealth_status", "Unstatus-seeking", "Status-seeking"},
    {"v_autonomy", "Conforming", "Autonomous"}, {"v_novelty", "Routine-seeking", "Novelty-seeking"},
    {"v_duty", "Unbound", "Dutiful"}
}};

static_assert(std::string_view(Axes[Matrix::fear].key) == "fearfulness");
static_assert(std::string_view(Axes[Matrix::prud].key) == "prudence");
static_assert(std::string_view(Axes[Matrix::altru].key) == "altruism");
static_assert(std::string_view(Axes[Matrix::v_duty].key) == "v_duty");

namespace
{
struct GankRecord
{
    uint32 attackerLowGuid = 0;
    uint64 unixTs = 0;
    float scale = 1.0f;
};

struct EventMemory
{
    std::vector<GankRecord> ganks;
    uint32 bgEvents = 0;
    uint32 totalEvents = 0;
    bool loaded = false;
    bool loading = false;
};

struct State
{
    Facets baseline{};
    std::array<float, AxisCount> mood{};  // volatile outing-scale overlay; never persisted
    std::array<uint32, Matrix::Drift.size()> novelty{};
    uint64 lastDecay = 0;
    uint32 appliedGeneration = 0;
    uint64 lastApplied = 0;
    EventMemory memory{};
};

struct PendingEvent
{
    std::string event;
    std::string detail;
};

std::mutex s_mutex;
std::unordered_map<uint32, State> s_states;
std::unordered_map<uint32, std::vector<PendingEvent>> s_pending;
bool s_enabled = true;
uint32 s_seed = 0;
uint32 s_generation = 1;
std::array<float, Matrix::Dials.size()> s_overrides{};

uint64 UnixTime()
{
    return static_cast<uint64>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

void IncrementBounded(uint32& value)
{
    if (value != std::numeric_limits<uint32>::max())
        ++value;
}

void AppendGank(EventMemory& memory, uint32 attackerLowGuid, uint64 unixTs, float scale = 1.0f)
{
    if (!attackerLowGuid)
        return;

    memory.ganks.push_back({attackerLowGuid, unixTs, std::clamp(scale, 0.0f, 1.0f)});
    uint64 const now = UnixTime();
    memory.ganks.erase(std::remove_if(memory.ganks.begin(), memory.ganks.end(),
        [now](GankRecord const& record)
        {
            double const ageDays = now > record.unixTs ? double(now - record.unixTs) / 86400.0 : 0.0;
            return record.scale * std::exp2(-ageDays / Matrix::GrudgeHalfLifeDays) < Matrix::GrudgeNoticeFloor;
        }), memory.ganks.end());
}

bool IsBattlegroundEvent(std::string_view event)
{
    return event == "killed in battleground" || event == "died in battleground" ||
           event == "won battleground" || event == "lost battleground";
}

uint32 ParseAttackerGuid(std::string const& detail)
{
    std::size_t marker = detail.find("guid:");
    if (marker == std::string::npos || (marker && detail[marker - 1] != '|'))
        return 0;

    char const* first = detail.data() + marker + 5;
    char const* last = detail.data() + detail.size();
    uint32 attackerLowGuid = 0;
    auto const [end, error] = std::from_chars(first, last, attackerLowGuid);
    if (error != std::errc{} || end == first || (end != last && *end != '|'))
        return 0;
    return attackerLowGuid;
}

EventMemory LoadEventMemory(uint32 guid)
{
    EventMemory memory;
    if (QueryResult result = PlayerbotsDatabase.Query(
        "SELECT event FROM playerbots_personality_ledger WHERE guid={} ORDER BY id DESC LIMIT 50", guid))
    {
        do
        {
            IncrementBounded(memory.totalEvents);
            if (IsBattlegroundEvent(result->Fetch()[0].Get<std::string>()))
                IncrementBounded(memory.bgEvents);
        } while (result->NextRow());
    }

    // Replay world ganks and revenges oldest-first so settlements survive restarts.
    uint64 const now = UnixTime();
    uint64 const horizonSecs = uint64(
        Matrix::GrudgeHalfLifeDays * std::log2(1.0f / Matrix::GrudgeNoticeFloor) * 86400.0f);
    if (QueryResult result = PlayerbotsDatabase.Query(
        "SELECT ts,event,detail FROM playerbots_personality_ledger WHERE guid={} AND "
        "event IN ('ganked in world','killed player in world') AND ts>{} ORDER BY id ASC",
        guid, now > horizonSecs ? now - horizonSecs : 0))
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 const otherLowGuid = ParseAttackerGuid(fields[2].Get<std::string>());
            if (!otherLowGuid)
                continue;
            if (fields[1].Get<std::string>() == "ganked in world")
                AppendGank(memory, otherLowGuid, fields[0].Get<uint32>());
            else
                for (GankRecord& record : memory.ganks)
                    if (record.attackerLowGuid == otherLowGuid)
                        record.scale *= 0.25f;
        } while (result->NextRow());
    }
    memory.loaded = true;
    return memory;
}

void MergeLiveMemory(EventMemory& loaded, EventMemory const& live)
{
    for (GankRecord const& record : live.ganks)
        AppendGank(loaded, record.attackerLowGuid, record.unixTs, record.scale);
    loaded.bgEvents = static_cast<uint32>(std::min<uint64>(std::numeric_limits<uint32>::max(),
        uint64(loaded.bgEvents) + live.bgEvents));
    loaded.totalEvents = static_cast<uint32>(std::min<uint64>(std::numeric_limits<uint32>::max(),
        uint64(loaded.totalEvents) + live.totalEvents));
    loaded.loading = false;
}

uint64 Mix(uint64 value)
{
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

uint32 StreamSeed(uint32 guid, uint32 token)
{
    uint64 value = Mix((uint64(s_seed) << 32) | guid);
    value = Mix(value ^ (uint64(Matrix::SchemaVersion) << 32) ^ token);
    return static_cast<uint32>(value ^ (value >> 32));
}

float Normal(uint32 guid, uint32 token)
{
    std::mt19937 engine(StreamSeed(guid, token));
    // Box-Muller avoids implementation-specific normal_distribution.
    double u1 = (double(engine()) + 1.0) / (double(std::mt19937::max()) + 2.0);
    double u2 = (double(engine()) + 1.0) / (double(std::mt19937::max()) + 2.0);
    double u3 = (double(engine()) + 1.0) / (double(std::mt19937::max()) + 2.0);
    float normal = float(std::sqrt(-2.0 * std::log(u1)) * std::cos(6.28318530717958647692 * u2));
    float scale = u3 < Matrix::TailProbability ? Matrix::TailScale : 1.0f;
    return normal * scale * Matrix::TailRenorm;
}

float ClampZ(float value)
{
    return std::clamp(value, -3.0f, 3.0f);
}

void LoadConfig()
{
    bool enabled = sConfigMgr->GetOption<bool>("AiPlayerbot.Personality.Enable", true);
    uint32 seed = sConfigMgr->GetOption<uint32>("AiPlayerbot.Personality.Seed", 0);
    std::array<float, Matrix::Dials.size()> overrides;
    for (std::size_t i = 0; i < overrides.size(); ++i)
    {
        float const value = sConfigMgr->GetOption<float>(
            std::string("AiPlayerbot.PersonalityDial.") + Matrix::Dials[i].configName,
            std::numeric_limits<float>::quiet_NaN(), /*showLogs*/ false);
        overrides[i] = std::isnan(value)
            ? value
            : std::clamp(value, Matrix::Dials[i].low, Matrix::Dials[i].high);
    }

    std::lock_guard<std::mutex> lock(s_mutex);
    s_enabled = enabled;
    s_seed = seed;
    s_overrides = overrides;
    ++s_generation;
}

void Decay(State& state, uint64 now)
{
    if (!state.lastDecay)
    {
        state.lastDecay = now;
        return;
    }
    if (now <= state.lastDecay)
        return;

    double elapsedMinutes = double(now - state.lastDecay) / 60.0;
    float factor = float(std::exp2(-elapsedMinutes / Matrix::MoodHalfLifeMinutes));
    for (float& value : state.mood)
        value *= factor;
    state.lastDecay = now;
}

Facets Expressed(State& state)
{
    Decay(state, UnixTime());
    Facets result;
    for (std::size_t i = 0; i < AxisCount; ++i)
        result.z[i] = ClampZ(state.baseline.z[i] + state.mood[i]);
    return result;
}

State Sample(uint32 guid, uint8 playerClass, uint8 race)
{
    State state;
    std::array<float, 8> factor{};
    for (uint32 i = 0; i < factor.size(); ++i)
        factor[i] = Normal(guid, 0x10000u + i);

    for (uint32 axis = 0; axis < 24; ++axis)
    {
        uint32 domain = axis / 4;
        state.baseline.z[axis] =
            Matrix::FacetNoise * Normal(guid, axis) + Matrix::FacetFactor * factor[domain];
    }

    using enum Matrix::Axis;
    state.baseline.z[altru] = Matrix::AltruismNoise * Normal(guid, altru) +
        Matrix::AltruismEmotion * factor[1] + Matrix::AltruismAgreeableness * factor[3];

    float m1 = factor[6];
    float m2 = factor[7];
    auto epsilon = [guid](uint32 axis) { return Normal(guid, axis); };
    // Loadings normalized to unit variance per axis.
    state.baseline.z[v_sec] = .814f * epsilon(v_sec) - .581f * m2;
    state.baseline.z[v_aff] = .889f * epsilon(v_aff) - .381f * m1 + .254f * factor[2];
    state.baseline.z[v_ben] = .814f * epsilon(v_ben) - .581f * m1;
    state.baseline.z[v_mast] = .894f * epsilon(v_mast) + .447f * m1;
    state.baseline.z[v_wealth] = .814f * epsilon(v_wealth) + .581f * m1;
    state.baseline.z[v_auto] = .841f * epsilon(v_auto) + .541f * m2;
    state.baseline.z[v_nov] = .814f * epsilon(v_nov) + .581f * m2;
    state.baseline.z[v_duty] = .841f * epsilon(v_duty) - .541f * m2;

    std::array<float, AxisCount> priorShift{};
    if (playerClass < Matrix::ClassPriors.size())
    {
        Matrix::ClassPriorDefinition const& prior = Matrix::ClassPriors[playerClass];
        for (uint8 i = 0; i < prior.count; ++i)
            priorShift[prior.offsets[i].axis] += prior.offsets[i].weight;
    }
    if (race < Matrix::RacePriors.size())
    {
        Matrix::ClassPriorDefinition const& prior = Matrix::RacePriors[race];
        for (uint8 i = 0; i < prior.count; ++i)
            priorShift[prior.offsets[i].axis] += prior.offsets[i].weight;
    }
    for (uint32 axis = 0; axis < AxisCount; ++axis)
        state.baseline.z[axis] +=
            std::clamp(priorShift[axis], -Matrix::PriorOffsetCap, Matrix::PriorOffsetCap);
    for (float& value : state.baseline.z)
        value = ClampZ(value);

    state.lastDecay = UnixTime();
    return state;
}

std::array<float, Matrix::Dials.size()> ComputeDials(Facets const& facets)
{
    std::array<float, Matrix::Dials.size()> values{};
    for (std::size_t dialIndex = 0; dialIndex < Matrix::Dials.size(); ++dialIndex)
    {
        Matrix::DialDefinition const& definition = Matrix::Dials[dialIndex];
        if (!std::isnan(s_overrides[dialIndex]))
        {
            values[dialIndex] = s_overrides[dialIndex];
            continue;
        }
        float raw = 0.0f;
        for (uint8 i = 0; i < definition.count; ++i)
            raw += definition.coefficients[i].weight * facets.z[definition.coefficients[i].axis];

        float centered = 2.0f / (1.0f + std::exp(-raw / Matrix::DialLogisticScale)) - 1.0f;
        float derived = centered < 0.0f
            ? definition.neutral + centered * (definition.neutral - definition.low)
            : definition.neutral + centered * (definition.high - definition.neutral);
        values[dialIndex] = derived;
    }
    return values;
}

char const* Intensity(float z)
{
    static char const* labels[] = {"Not", "Slightly", "Less", "Somewhat", "More", "Very", "Extremely"};
    double phi = .5 * (1.0 + std::erf(double(z) / std::sqrt(2.0)));
    double y = std::abs(2.0 * phi - 1.0);
    int category = int(std::lround(3.0 + 3.0 * y * std::sqrt(y)));
    return labels[std::clamp(category, 0, 6)];
}

std::string Describe(std::size_t axis, float z)
{
    std::ostringstream out;
    out << Intensity(z) << ' ' << (z < 0.0f ? Axes[axis].lowAdjective : Axes[axis].highAdjective);
    return out.str();
}

std::string EscapeSqlLiteral(std::string value, std::size_t maxLength)
{
    if (value.size() > maxLength)
        value.resize(maxLength);

    std::string escaped;
    escaped.reserve(value.size());
    for (char c : value)
    {
        switch (c)
        {
            case '\0': escaped += "\\0"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\\': escaped += "\\\\"; break;
            case '\'': escaped += "\\'"; break;
            case '"': escaped += "\\\""; break;
            case '\x1a': escaped += "\\Z"; break;
            default: escaped += c; break;
        }
    }
    return escaped;
}

void AppendLedger(uint32 guid, char const* event, std::string detail, std::string deltas)
{
    std::string safeEvent = EscapeSqlLiteral(event ? event : "", 32);
    detail = EscapeSqlLiteral(std::move(detail), 255);
    deltas = EscapeSqlLiteral(std::move(deltas), 255);
    PlayerbotsDatabase.Execute(
        "INSERT INTO playerbots_personality_ledger (guid,ts,event,detail,deltas) VALUES ({},{},'{}','{}','{}')",
        guid, uint32(UnixTime()), safeEvent, detail, deltas);
}

void PersistBaseline(uint32 guid, State const& state)
{
    PlayerbotsDatabaseTransaction transaction = PlayerbotsDatabase.BeginTransaction();
    uint32 now = uint32(UnixTime());
    for (uint32 axis = 0; axis < AxisCount; ++axis)
    {
        std::ostringstream sql;
        sql << std::setprecision(9);
        sql << "REPLACE INTO playerbots_personality (guid,axis,baseline,updated) VALUES ("
            << guid << ',' << axis << ',' << state.baseline.z[axis] << ',' << now << ')';
        transaction->Append(sql.str());
    }
    PlayerbotsDatabase.CommitTransaction(transaction);
}
}

void Initialize()
{
    LoadConfig();

    // Init and hydration are synchronous; gameplay ledger writes stay queued.
    PlayerbotsDatabase.DirectExecute("CREATE TABLE IF NOT EXISTS playerbots_personality (guid INT UNSIGNED NOT NULL, axis TINYINT UNSIGNED NOT NULL, baseline FLOAT NOT NULL, updated INT UNSIGNED NOT NULL DEFAULT 0, PRIMARY KEY (guid, axis)) ENGINE=InnoDB");
    PlayerbotsDatabase.DirectExecute("CREATE TABLE IF NOT EXISTS playerbots_personality_ledger (id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY, guid INT UNSIGNED NOT NULL, ts INT UNSIGNED NOT NULL, event VARCHAR(32) NOT NULL, detail VARCHAR(255) NOT NULL DEFAULT '', deltas VARCHAR(255) NOT NULL DEFAULT '', KEY guid_ts (guid, ts)) ENGINE=InnoDB");

    std::unordered_map<uint32, State> loaded;
    std::unordered_map<uint32, uint32> counts;
    if (QueryResult result = PlayerbotsDatabase.Query(
        "SELECT guid,axis,baseline FROM playerbots_personality ORDER BY guid,axis"))
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 guid = fields[0].Get<uint32>();
            uint8 axis = fields[1].Get<uint8>();
            if (axis >= AxisCount)
                continue;
            State& state = loaded[guid];
            state.baseline.z[axis] = fields[2].Get<float>();
            ++counts[guid];
        } while (result->NextRow());
    }

    std::size_t profileCount = 0;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_states.clear();
        for (auto& [guid, state] : loaded)
        {
            if (counts[guid] != AxisCount)
                continue;
            if (!state.lastDecay)
                state.lastDecay = UnixTime();
            s_states.emplace(guid, std::move(state));
        }
        profileCount = s_states.size();
    }
    LOG_INFO("server.loading", "Bot personality: loaded {} profiles", profileCount);
}

void ReloadConfig()
{
    LoadConfig();
}

void EnsureSeeded(Player* bot)
{
    PlayerbotAI* ai = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
    if (!ai)
        return;

    uint32 guid = bot->GetGUID().GetCounter();
    bool seeded = false;
    bool apply = false;
    bool loadMemory = false;
    State snapshot;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_enabled)
            return;
        auto [it, inserted] = s_states.try_emplace(guid);
        if (inserted)
        {
            it->second = Sample(guid, bot->getClass(), bot->getRace());
            snapshot = it->second;
            seeded = true;
        }
        if (!it->second.memory.loaded && !it->second.memory.loading)
        {
            it->second.memory.loading = true;
            loadMemory = true;
        }
        apply = it->second.appliedGeneration != s_generation;
    }

    if (loadMemory)
    {
        EventMemory loaded = LoadEventMemory(guid);
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_states.find(guid);
        if (it != s_states.end())
        {
            MergeLiveMemory(loaded, it->second.memory);
            it->second.memory = std::move(loaded);
        }
    }
    if (seeded)
    {
        PersistBaseline(guid, snapshot);
        AppendLedger(guid, "birth", bot->GetName(), "");
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_states.find(guid);
        if (it != s_states.end())
            IncrementBounded(it->second.memory.totalEvents);
    }
    if (seeded || apply)
        ApplyDials(bot);
}

bool GetBaselines(uint32 guid, std::array<float, AxisCount>& out)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_enabled)
        return false;
    auto const it = s_states.find(guid);
    if (it == s_states.end())
        return false;
    for (std::size_t axis = 0; axis < AxisCount; ++axis)
        out[axis] = it->second.baseline.z[axis];
    return true;
}

void NoteGank(uint32 victimGuid, uint32 attackerLowGuid)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    auto it = s_states.find(victimGuid);
    if (!s_enabled || it == s_states.end())
        return;
    AppendGank(it->second.memory, attackerLowGuid, UnixTime());
}

void GrudgeSettled(uint32 killerGuid, uint32 victimLowGuid)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    auto it = s_states.find(killerGuid);
    if (!s_enabled || it == s_states.end())
        return;

    for (GankRecord& record : it->second.memory.ganks)
        if (record.attackerLowGuid == victimLowGuid)
            record.scale *= 0.25f;
}

float GrudgeWeight(uint32 guid, uint32 attackerLowGuid)
{
    if (!guid || !attackerLowGuid)
        return 0.0f;

    uint64 const now = UnixTime();
    std::lock_guard<std::mutex> lock(s_mutex);
    auto const it = s_states.find(guid);
    if (!s_enabled || it == s_states.end())
        return 0.0f;

    float weight = 0.0f;
    for (GankRecord const& record : it->second.memory.ganks)
    {
        if (record.attackerLowGuid != attackerLowGuid)
            continue;
        double const ageDays = now > record.unixTs ? double(now - record.unixTs) / 86400.0 : 0.0;
        weight += record.scale * float(std::exp2(-ageDays / Matrix::GrudgeHalfLifeDays));
    }
    return std::clamp(weight, 0.0f, 1.0f);
}

float PvpBiographyShare(uint32 guid)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    auto const it = s_states.find(guid);
    if (!s_enabled || it == s_states.end())
        return 0.0f;

    EventMemory const& memory = it->second.memory;
    float const share = float(memory.bgEvents) / float(std::max<uint32>(10, memory.totalEvents));
    return std::clamp(share, 0.0f, 0.8f);
}

uint32 GetSpecSeed(uint32 guid, uint8 race, uint8 playerClass)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    uint64 value = Mix((uint64(s_seed) << 32) | guid);
    // A recycled guid reborn as a new race/class rolls fresh dice.
    value = Mix(value ^ 0x73706563ULL ^ (uint64(race) << 32) ^ (uint64(playerClass) << 40)); // "spec"
    return static_cast<uint32>(value ^ (value >> 32));
}

static void ApplyDials(Player* bot)
{
    PlayerbotAI* ai = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
    if (!ai || !ai->GetAiObjectContext())
        return;

    std::array<float, Matrix::Dials.size()> dials;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_states.find(bot->GetGUID().GetCounter());
        if (it == s_states.end())
            return;
        if (s_enabled)
            dials = ComputeDials(Expressed(it->second));
        else
        {
            for (std::size_t i = 0; i < dials.size(); ++i)
                dials[i] = Matrix::Dials[i].neutral;
        }
        it->second.appliedGeneration = s_generation;
        it->second.lastApplied = UnixTime();
    }

    for (std::size_t i = 0; i < dials.size(); ++i)
        ai->GetAiObjectContext()->GetValue<float>(Matrix::Dials[i].valueName)->Set(dials[i]);
}

void OnLifeEvent(Player* bot, char const* event, char const* detail)
{
    if (!bot || !event || !GET_PLAYERBOT_AI(bot))
        return;

    std::size_t eventIndex = Matrix::Drift.size();
    for (std::size_t i = 0; i < Matrix::Drift.size(); ++i)
    {
        if (std::string_view(event) == Matrix::Drift[i].event)
        {
            eventIndex = i;
            break;
        }
    }
    if (eventIndex == Matrix::Drift.size())
        return;

    uint32 guid = bot->GetGUID().GetCounter();
    std::ostringstream deltaText;
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_enabled)
            return;
        auto it = s_states.find(guid);
        if (it == s_states.end())
            return;
        State& state = it->second;
        IncrementBounded(state.memory.totalEvents);
        if (IsBattlegroundEvent(event))
            IncrementBounded(state.memory.bgEvents);

        Matrix::DriftDefinition const& definition = Matrix::Drift[eventIndex];
        if (definition.count)
        {
            Decay(state, UnixTime());
            float damping = 1.0f / std::sqrt(1.0f + float(state.novelty[eventIndex]++));
            for (uint8 i = 0; i < definition.count; ++i)
            {
                Matrix::AxisCoefficient const& impulse = definition.impulses[i];
                float delta = std::clamp(
                    impulse.weight * damping, -Matrix::EventImpulseCap, Matrix::EventImpulseCap);
                state.mood[impulse.axis] = std::clamp(
                    state.mood[impulse.axis] + delta, -Matrix::MoodCap, Matrix::MoodCap);
                if (i)
                    deltaText << ',';
                deltaText << Axes[impulse.axis].key << ':' << std::showpos << std::setprecision(4)
                          << delta << std::noshowpos;
            }
            changed = true;
        }
    }

    if (changed)
        ApplyDials(bot);
    AppendLedger(guid, event, detail ? detail : "", deltaText.str());
}

void QueueLifeEvent(uint32 guid, char const* event, std::string detail)
{
    if (!guid || !event)
        return;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_enabled)
        s_pending[guid].push_back({event, std::move(detail)});
}

void ProcessPending(Player* bot)
{
    if (!bot || !GET_PLAYERBOT_AI(bot))
        return;

    std::vector<PendingEvent> pending;
    bool refresh = false;
    bool ensureSeeded = false;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto state = s_states.find(bot->GetGUID().GetCounter());
        ensureSeeded = s_enabled && state == s_states.end();
        refresh = state != s_states.end() && state->second.appliedGeneration != s_generation;
        if (!refresh && s_enabled && state != s_states.end() &&
            UnixTime() - state->second.lastApplied >= 60)
        {
            // Mood decays between events; keep re-applying dials until it settles to baseline.
            for (float value : state->second.mood)
            {
                if (std::fabs(value) > 0.001f)
                {
                    refresh = true;
                    break;
                }
            }
        }
        auto it = s_pending.find(bot->GetGUID().GetCounter());
        if (it != s_pending.end())
        {
            if (s_enabled)
                pending = std::move(it->second);
            s_pending.erase(it);
        }
    }
    if (ensureSeeded)
        EnsureSeeded(bot);
    for (PendingEvent const& item : pending)
        OnLifeEvent(bot, item.event.c_str(), item.detail.c_str());
    if (refresh)
    {
        bool stillStale = false;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            auto const it = s_states.find(bot->GetGUID().GetCounter());
            stillStale = it != s_states.end() &&
                (it->second.appliedGeneration != s_generation ||
                 UnixTime() - it->second.lastApplied >= 60);
        }
        if (stillStale)
            ApplyDials(bot);
    }
}

bool HandleWho(ChatHandler* handler, std::string const& name)
{
    ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(name);
    if (!guid)
    {
        handler->PSendSysMessage("Playerbots: character '{}' not found", name);
        return false;
    }

    Facets facets;
    std::array<float, Matrix::Dials.size()> dials;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_enabled)
        {
            handler->SendSysMessage("Bot personality is disabled.");
            return false;
        }
        auto it = s_states.find(guid.GetCounter());
        if (it == s_states.end())
        {
            handler->PSendSysMessage("Playerbots: '{}' has no personality profile", name);
            return false;
        }
        facets = Expressed(it->second);
        dials = ComputeDials(facets);
    }

    handler->PSendSysMessage("Personality for {}:", name);
    for (std::size_t i = 0; i < AxisCount; ++i)
    {
        if (std::abs(facets.z[i]) >= .15f)
            handler->PSendSysMessage("  {}: {} ({:+.2f})", Axes[i].key, Describe(i, facets.z[i]), facets.z[i]);
    }

    std::array<std::size_t, 8> values = {{25, 26, 27, 28, 29, 30, 31, 32}};
    std::partial_sort(values.begin(), values.begin() + 3, values.end(), [&facets](std::size_t left, std::size_t right)
    {
        return facets.z[left] > facets.z[right];
    });
    handler->PSendSysMessage("Top values: {} ({:+.2f}), {} ({:+.2f}), {} ({:+.2f})",
        Axes[values[0]].key, facets.z[values[0]], Axes[values[1]].key, facets.z[values[1]],
        Axes[values[2]].key, facets.z[values[2]]);

    handler->SendSysMessage("Behavior traits:");
    for (std::size_t i = 0; i < dials.size(); ++i)
        handler->PSendSysMessage("  {} = {:.4f}", Matrix::Dials[i].valueName, dials[i]);

    handler->SendSysMessage("Last ledger entries:");
    if (QueryResult result = PlayerbotsDatabase.Query(
        "SELECT ts,event,detail,deltas FROM playerbots_personality_ledger WHERE guid={} ORDER BY id DESC LIMIT 5",
        guid.GetCounter()))
    {
        do
        {
            Field* fields = result->Fetch();
            handler->PSendSysMessage("  {} {}: {} [{}]", fields[0].Get<uint32>(), fields[1].Get<std::string>(),
                fields[2].Get<std::string>(), fields[3].Get<std::string>());
        } while (result->NextRow());
    }
    return true;
}
}
