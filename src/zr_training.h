#pragma once
#include "entity/ccsplayercontroller.h"

bool ZR_TrainingEnabled();
void ZR_TrainingLevelInit();
void ZR_TrainingPrepare();
void ZR_TrainingStart();
void ZR_TrainingDisconnect(CPlayerSlot slot);
bool ZR_TrainingReserveSlot(CPlayerSlot slot, uint64 xuid);
bool ZR_TrainingSpawn(CCSPlayerController* controller);
void ZR_TrainingJoin(CCSPlayerController* controller);
void ZR_TrainingDeath(CCSPlayerController* controller);
bool ZR_TrainingInitialInfection();
bool ZR_TrainingCanWin();
bool ZR_TrainingSuppressDeath();
