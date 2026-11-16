#pragma once
#include <cstdint>
#include <unordered_set>

// Engine-independent rules. Identity survives disconnects for this round.
class ZRTrainingPolicy
{
public:
	enum Phase { Off, Preparing, Active, Invalid };
	Phase phase = Off;
	std::uint64_t generation = 0;
	double started = -1;
	void Begin(bool enabled)
	{
		++generation;
		phase = enabled ? Preparing : Off;
		started = -1;
		dead.clear();
		spawned.clear();
	}
	bool Enabled() const { return phase != Off; }
	bool CanEnter(std::uint64_t id, double now) const
	{
		return id && Enabled() && !dead.count(id) && !spawned.count(id) && (started < 0 || now - started < 10.0);
	}
	bool Spawn(std::uint64_t id, double now, bool explicitReset = false)
	{
		if (!id || !Enabled() || (!explicitReset && !CanEnter(id, now))) return false;
		dead.erase(id);
		spawned.insert(id);
		return true;
	}
	void Died(std::uint64_t id) { if (id) dead.insert(id); }
	void Activate() { if (phase == Preparing) phase = Active; }
	void Invalidate() { if (Enabled()) phase = Invalid; }

private:
	std::unordered_set<std::uint64_t> dead, spawned;
};
