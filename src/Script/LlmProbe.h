#ifndef MOD_PLAYERBOTS_LLM_PROBE_H
#define MOD_PLAYERBOTS_LLM_PROBE_H

// EXPLORATORY: instrumentation for the llmind control-loop investigation.
// Not a shipping surface; delete freely.

#include <iosfwd>
#include <string>

class ChatHandler;
class Player;
class PlayerbotAI;

namespace LlmProbe
{
// One bot's full decision-relevant state as a JSON object (mind wake packets).
void AppendBotJson(std::ostringstream& os, Player* bot, PlayerbotAI* botAI);

// Command feedback tap: playerbots "tell master" lines (cast results, errors)
// otherwise evaporate for masterless bots. mod-bot-minds registers a sink and
// turns them into percepts.
using TellSink = void (*)(Player* bot, std::string const& text);
void SetTellSink(TellSink sink);
void NoteTell(Player* bot, std::string const& text);

bool HandleTap(ChatHandler* handler, char const* args);
bool HandleSteer(ChatHandler* handler, char const* args);
bool HandleSnap(ChatHandler* handler, char const* args);
}

#endif
