#ifndef MOD_PLAYERBOTS_BOT_PERSONALITY_H
#define MOD_PLAYERBOTS_BOT_PERSONALITY_H

#include <array>
#include <cstdint>
#include <string>

#include "PersonalityMatrix.h"

class ChatHandler;
class Player;

namespace BotPersonality
{
void Initialize();
void ReloadConfig();
void EnsureSeeded(Player* bot);
bool GetBaselines(std::uint32_t guid, std::array<float, Matrix::AxisCount>& out);
std::uint32_t GetSpecSeed(std::uint32_t guid, std::uint8_t race, std::uint8_t playerClass);
void OnLifeEvent(Player* bot, char const* event, char const* detail = "");
void QueueLifeEvent(std::uint32_t guid, char const* event, std::string detail = {});
void NoteGank(std::uint32_t victimGuid, std::uint32_t attackerLowGuid);
void GrudgeSettled(std::uint32_t killerGuid, std::uint32_t victimLowGuid);
float GrudgeWeight(std::uint32_t guid, std::uint32_t attackerLowGuid);
float PvpBiographyShare(std::uint32_t guid);
void ProcessPending(Player* bot);
void Flush(Player* bot);
bool HandleWho(ChatHandler* handler, std::string const& name);
}

#endif
