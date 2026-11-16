#include "zr_training.h"
#include "zr_training_policy.h"
#include "zombiereborn.h"
#include "commands.h"
#include "entity/cgamerules.h"
#include "engine2/ivengineserver2.h"
#include <map>
#include <string>
#include <chrono>

void ZR_Infect(CCSPlayerController*, CCSPlayerController*, bool);
void ZR_Cure(CCSPlayerController*);

static CConVar<bool> s_enable("zr_training_enable", FCVAR_NONE, "Request ZE training ZR; applied next round (requires zr_enable)", false);
static CConVar<int> s_state("zr_training_state", FCVAR_NONE, "Output: 0 ordinary ZR, 1 preparing, 2 active, 3 invalid; valid through round_end", 0);
static CConVar<int> s_round("zr_training_round", FCVAR_NONE, "Output: native round generation; changes on map init and round prestart", 0);
static CConVar<int> s_version("zr_training_protocol", FCVAR_NONE, "Output: training observer protocol version", 1);
static CConVar<CUtlString> s_session("zr_training_session", FCVAR_NONE, "Output: process-unique native training round identity", "");
static const auto s_boot = std::chrono::system_clock::now().time_since_epoch().count();
static ZRTrainingPolicy s_policy;
static CHandle<CCSPlayerController> s_bot;
static int s_botSlot = -1;
static bool s_suppressDeath = false, s_hadHumans = false, s_restartQueued = false;
static bool s_mapPending = false;
static double s_nextBot = 0;
static CHandle<CCSPlayerController> s_explicitSpawn;
static std::map<std::string, std::string> s_savedCvars;

static void Publish()
{
	s_state.Set(static_cast<int>(s_policy.phase));
	s_round.Set(static_cast<int>(s_policy.generation));
	s_session.Set(CUtlString((std::to_string(s_boot) + "-" + std::to_string(s_policy.generation)).c_str()));
}

static void Invalidate(const char* reason)
{
	if (s_policy.phase != ZRTrainingPolicy::Invalid)
		Message("[ZR-Training] round=%llu invalid=%s\n", static_cast<unsigned long long>(s_policy.generation), reason);
	s_policy.Invalidate();
	Publish();
}

bool ZR_TrainingEnabled() { return s_policy.Enabled(); }
bool ZR_TrainingCanWin() { return !s_policy.Enabled() || (s_policy.phase == ZRTrainingPolicy::Active && !s_suppressDeath); }
bool ZR_TrainingSuppressDeath() { return s_suppressDeath; }

static uint64 Identity(CCSPlayerController* p)
{
	if (!p || p->IsBot() || p->m_bIsHLTV) return 0;
	auto player = p->GetZEPlayer();
	if (!player) return 0;
	return player->IsAuthenticated() ? player->GetSteamId64() :
		(player->GetUnauthenticatedSteamId() ? player->GetUnauthenticatedSteamId64() : 0);
}

static int Humans()
{
	int count = 0;
	if (GetGlobals())
		for (int i = 0; i < GetGlobals()->maxClients; ++i)
		{
			auto p = CCSPlayerController::FromSlot(i);
			if (p && p->IsConnected() && !p->IsBot() && !p->m_bIsHLTV) ++count;
		}
	return count;
}

static int Bots()
{
	int count = 0;
	if (GetGlobals())
		for (int i = 0; i < GetGlobals()->maxClients; ++i)
		{
			auto p = CCSPlayerController::FromSlot(i);
			if (p && p->IsConnected() && p->IsBot() && !p->m_bIsHLTV) ++count;
		}
	return count;
}

static void Restart(const char* reason)
{
	if (s_restartQueued) return;
	Invalidate(reason);
	s_restartQueued = true;
	Message("[ZR-Training] restarting for %s\n", reason);
	g_pEngineServer2->ServerCommand("mp_restartgame 1");
}

static void ConfigureBots(int humans)
{
	// fill is required by the upstream ZR team rules. Never use quota=1 with fill.
	const std::pair<const char*, std::string> values[] = {
		{"bot_quota_mode", "fill"}, {"bot_quota", std::to_string(humans + 1)},
		{"bot_auto_vacate", "0"}, {"bot_join_after_player", "0"}, {"bot_stop", "1"}, {"bot_zombie", "1"}
	};
	for (const auto& [name, value] : values)
	{
		ConVarRefAbstract ref(name);
		if (!ref.IsValidRef()) continue;
		if (!s_savedCvars.count(name)) s_savedCvars[name] = ref.GetString().Get();
		if (value != ref.GetString().Get())
			g_pEngineServer2->ServerCommand((std::string(name) + " " + value).c_str());
	}
}

static float Tick()
{
	if (!GetGlobals()) return 0.25f;
	// Map cfgs run after OnLevelInit. Bootstrap empty servers too; no round_prestart
	// is guaranteed before the first human/bot causes the engine to start a game.
	if (s_mapPending && g_pGameRules)
	{
		s_mapPending = false;
		ZR_TrainingPrepare();
	}
	if (!s_policy.Enabled() || !g_cvarEnableZR.Get()) return 0.25f;
	int humans = Humans();
	ConfigureBots(humans);
	if (!humans)
	{
		if (s_hadHumans) Invalidate("server-empty");
		s_hadHumans = false;
	}
	else if (!s_hadHumans)
	{
		s_hadHumans = true;
		Restart("first-human");
	}

	if (s_botSlot >= 0 && (!s_bot.Get() || !s_bot->IsConnected()))
	{
		Invalidate("bot-missing");
		s_botSlot = -1;
	}
	if (!s_bot.Get() || !s_bot->IsConnected())
	{
		for (int i = 0; i < GetGlobals()->maxClients; ++i)
		{
			auto p = CCSPlayerController::FromSlot(i);
			if (p && p->IsConnected() && p->IsBot() && !p->m_bIsHLTV)
			{
				s_bot = p->GetHandle(); s_botSlot = i; break;
			}
		}
	}
	// Quota creation and the no-nav fallback can race. Keep one owned bot only;
	// invalidate before removing extras so their departure cannot award a CT win.
	if (s_bot.Get() && s_bot->IsConnected())
		for (int i = 0; i < GetGlobals()->maxClients; ++i)
		{
			auto p = CCSPlayerController::FromSlot(i);
			if (p && p->IsConnected() && p->IsBot() && !p->m_bIsHLTV && i != s_botSlot)
			{
				Invalidate("extra-bot");
				g_pEngineServer2->DisconnectClient(CPlayerSlot(i), NETWORK_DISCONNECT_KICKED, "Only one ZE training bot is allowed");
			}
		}
	if (!s_bot.Get() || !s_bot->IsConnected())
	{
		if (g_flUniversalTime >= s_nextBot && humans < GetGlobals()->maxClients)
		{
			s_nextBot = g_flUniversalTime + 5;
			g_pEngineServer2->ServerCommand("bot_add_ct");
		}
	}
	else if (s_policy.phase == ZRTrainingPolicy::Invalid && humans)
		Restart("bot-restored");
	return 0.25f;
}

void ZR_TrainingLevelInit()
{
	s_policy.Begin(false); s_bot = {}; s_botSlot = -1; s_hadHumans = false;
	s_restartQueued = false; s_explicitSpawn = {}; s_nextBot = g_flUniversalTime + 5;
	s_mapPending = true;
	Publish();
	CTimer::Create(0.25f, TIMERFLAG_MAP, Tick);
}

void ZR_TrainingPrepare()
{
	s_mapPending = false;
	const bool enabled = g_cvarEnableZR.Get() && s_enable.Get();
	s_policy.Begin(enabled); s_explicitSpawn = {}; s_suppressDeath = false; s_restartQueued = false;
	s_hadHumans = Humans() > 0;
	if (!enabled)
	{
		s_bot = {}; s_botSlot = -1;
		for (const auto& [name, value] : s_savedCvars)
			g_pEngineServer2->ServerCommand((name + " \"" + value + "\"").c_str());
		s_savedCvars.clear();
	}
	Publish();
}

void ZR_TrainingStart()
{
	s_policy.started = g_flUniversalTime;
	if (s_policy.Enabled()) Message("[ZR-Training] round=%llu admission=10s infection=3s-after-freeze\n", static_cast<unsigned long long>(s_policy.generation));
}

void ZR_TrainingDisconnect(CPlayerSlot slot)
{
	if (!s_policy.Enabled()) return;
	if (slot.Get() == s_botSlot)
	{
		Invalidate("bot-disconnect"); s_bot = {}; s_botSlot = -1;
	}
	if (!Humans()) { Invalidate("server-empty"); s_hadHumans = false; }
}

bool ZR_TrainingReserveSlot(CPlayerSlot slot, uint64 xuid)
{
	if (!s_policy.Enabled() || !xuid || !GetGlobals()) return true;
	int humans = 0;
	for (int i = 0; i < GetGlobals()->maxClients; ++i)
	{
		if (i == slot.Get()) continue;
		auto p = g_playerManager->GetPlayer(i);
		if (p && p->IsConnected() && !p->IsFakeClient()) ++humans;
	}
	return humans < GetGlobals()->maxClients - 1;
}

bool ZR_TrainingSpawn(CCSPlayerController* p)
{
	if (!s_policy.Enabled() || p->IsBot() || p->m_bIsHLTV) return true;
	bool explicitReset = s_explicitSpawn.Get() == p;
	if (explicitReset) s_explicitSpawn = {};
	if (!s_policy.Spawn(Identity(p), g_flUniversalTime, explicitReset))
	{
		// Covers map/plugin Respawn(), not only our timers or the jointeam command.
		s_suppressDeath = true;
		if (auto pawn = p->GetPlayerPawn(); pawn && pawn->IsAlive()) pawn->CommitSuicide(false, true);
		s_suppressDeath = false;
		p->SwitchTeam(CS_TEAM_CT);
		ClientPrint(p, HUD_PRINTTALK, ZR_PREFIX "Wait for the next round to join training.");
		return false;
	}
	p->SwitchTeam(CS_TEAM_CT);
	return true;
}

void ZR_TrainingJoin(CCSPlayerController* p)
{
	if (p->IsBot()) return;
	p->ChangeTeam(CS_TEAM_CT);
	CHandle<CCSPlayerController> handle = p->GetHandle();
	CTimer::Create(0.1f, TIMERFLAG_MAP | TIMERFLAG_ROUND, [handle]() {
		auto p = handle.Get();
        if (p && s_policy.Enabled() && g_ZRRoundState != EZRRoundState::ROUND_END && p->m_iTeamNum() == CS_TEAM_CT && s_policy.CanEnter(Identity(p), g_flUniversalTime)) p->Respawn();
		return -1.0f;
	});
}

void ZR_TrainingDeath(CCSPlayerController* p)
{
	if (s_policy.Enabled() && !p->IsBot() && !s_suppressDeath) s_policy.Died(Identity(p));
}

bool ZR_TrainingInitialInfection()
{
	if (!s_policy.Enabled()) return false;
	auto bot = s_bot.Get();
	if (s_policy.phase != ZRTrainingPolicy::Preparing) return true;
	if (!Humans() || Bots() != 1 || !bot || !bot->IsConnected() || !bot->GetPlayerPawn() || !bot->GetPlayerPawn()->IsAlive())
	{
		Invalidate("initial-bot-or-human-missing"); return true;
	}
	// Use the normal zombie class and its default health; never pick a human MZ.
	g_ZRRoundState = EZRRoundState::POST_INFECTION;
	ZR_Infect(bot, bot, true);
	s_policy.Activate(); Publish();
	Message("[ZR-Training] round=%llu active bot-slot=%d\n", static_cast<unsigned long long>(s_policy.generation), s_botSlot);
	ZR_CheckTeamWinConditions(CS_TEAM_T);
	return true;
}

CON_COMMAND_F(zr_training_respawn, "<#userid> - Explicit CT respawn for server/admin controlled training reset", FCVAR_LINKED_CONCOMMAND)
{
	if (context.GetPlayerSlot().Get() != -1 || !s_policy.Enabled() || g_ZRRoundState == EZRRoundState::ROUND_END ||
		(args.ArgC() != 2 && args.ArgC() != 3) || args[1][0] != '#') return;
	if (args.ArgC() == 3 && V_strcmp(args[2], s_session.Get().String())) return;
	int id = atoi(args[1] + 1);
	if (!GetGlobals()) return;
	for (int i = 0; i < GetGlobals()->maxClients; ++i)
	{
		auto p = CCSPlayerController::FromSlot(i);
		if (!p || !p->IsConnected() || p->IsBot() || p->m_bIsHLTV || !Identity(p) || g_pEngineServer2->GetPlayerUserId(p->GetPlayerSlot()).Get() != id) continue;
		p->SwitchTeam(CS_TEAM_CT);
		s_explicitSpawn = p->GetHandle(); p->Respawn(); s_explicitSpawn = {};
		return;
	}
}

CON_COMMAND_F(zr_training_status, "Print native training round state", FCVAR_LINKED_CONCOMMAND)
{
	if (context.GetPlayerSlot().Get() != -1) return;
	Message("[ZR-Training] protocol=1 requested=%d effective=%d round=%d state=%d humans=%d bots=%d bot-slot=%d\n", s_enable.Get(), s_policy.Enabled(), s_round.Get(), s_state.Get(), Humans(), Bots(), s_botSlot);
}
