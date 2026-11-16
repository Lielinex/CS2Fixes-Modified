#include "../src/zr_training_policy.h"
#include <cstdlib>
#include <iostream>

static int checks = 0;
static void Check(bool pass, const char* label)
{
	++checks;
	if (!pass) { std::cerr << "FAIL: " << label << '\n'; std::exit(1); }
}
int main()
{
	ZRTrainingPolicy p;
	Check(!p.Enabled(), "ordinary ZR starts off");
	p.Begin(true);
	const auto first = p.generation;
	Check(p.CanEnter(11, 50), "engine round-reset spawn before round_start is admitted");
	p.started = 100;
	Check(!p.CanEnter(0, 100), "unknown identity cannot bypass death bookkeeping");
	Check(p.Spawn(11, 100), "first CT spawn admitted");
	p.Died(11);
	Check(!p.CanEnter(11, 101) && !p.Spawn(11, 101), "dead player cannot reconnect or switch teams even before cutoff");
	Check(p.Spawn(22, 109.999), "first admission just before 10 seconds");
	Check(!p.Spawn(33, 110), "exactly 10 seconds is closed");
	Check(!p.Spawn(44, 999), "late first join cannot spawn");
	Check(!p.Spawn(22, 109.999), "second pawn/reconnect does not reset admission");
	Check(p.Spawn(11, 200, true), "authorized reset may respawn dead participant");
	Check(p.Spawn(44, 200, true), "authorized reset may admit waiting participant");
	p.Died(44);
	Check(!p.Spawn(44, 201), "explicit reset is not a permanent exemption");
	p.Activate(); Check(p.phase == ZRTrainingPolicy::Active, "normal infection activates training");
	p.Invalidate(); p.Activate(); Check(p.phase == ZRTrainingPolicy::Invalid, "bot restoration cannot reactivate invalid current round");
	p.Begin(true); p.started = 1000;
	Check(p.generation != first && p.Spawn(11, 1000), "new round clears old death restriction");
	Check(p.phase == ZRTrainingPolicy::Preparing, "new round requires initial infection again");
	p.Begin(false);
	Check(!p.Enabled() && !p.CanEnter(55, 1001), "ordinary mode releases training policy");
	std::cout << "Training policy: " << checks << " checks passed\n";
}
