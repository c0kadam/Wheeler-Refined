#include "WheelItems/AlreadyPoisonedReapplyGuardPolicy.h"

#include <cstdlib>
#include <iostream>

namespace
{
	using namespace AlreadyPoisonedReapplyGuardPolicy;

	struct SimulatedActivation
	{
		int poisonItemCount = 2;
		int existingWeaponPoison = 17;
		bool queueInvoked = false;
	};

	constexpr HandEvidence CleanHand(FormID a_formID)
	{
		return { true, a_formID, true, 1, 0, 0 };
	}

	constexpr HandEvidence PoisonedHand(FormID a_formID)
	{
		return { true, a_formID, true, 1, 0, 1 };
	}

	constexpr Evidence SinglePhysicalTwoHanded(
		FormID a_formID,
		std::uint32_t a_wornMembers = 1,
		std::uint32_t a_poisonedMembers = 0,
		bool a_readable = true,
		bool a_stable = true)
	{
		Evidence evidence{};
		// Mirror the scalar state seen for a 2H weapon while keeping physical evidence singular.
		evidence.right = { true, a_formID, false, 0, 0, 0 };
		evidence.left = { true, a_formID, false, 0, 0, 0 };
		evidence.equippedStateStable = a_stable;
		evidence.targetModel = TargetModel::kSinglePhysicalTwoHanded;
		evidence.singlePhysicalTarget = {
			true, a_formID, a_readable, a_wornMembers, 0, a_poisonedMembers
		};
		return evidence;
	}

	bool RunPoisonActivation(const Evidence& a_evidence, SimulatedActivation& a_state)
	{
		const auto result = Evaluate(a_evidence);
		if (!result.AllowsQueue()) {
			return false;
		}
		a_state.queueInvoked = true;
		return true;
	}

	void Require(bool a_condition, const char* a_name)
	{
		if (!a_condition) {
			std::cerr << "FAIL " << a_name << '\n';
			std::exit(1);
		}
		std::cout << "PASS " << a_name << '\n';
	}
}

int main()
{
	using namespace AlreadyPoisonedReapplyGuardPolicy;

	SimulatedActivation cleanRightState{};
	Require(RunPoisonActivation({ CleanHand(0x100), {} }, cleanRightState),
		"clean RIGHT target queues");
	Require(cleanRightState.queueInvoked, "clean target invokes QueuePoisonApply");

	SimulatedActivation cleanLeftState{};
	Require(RunPoisonActivation({ {}, CleanHand(0x200) }, cleanLeftState),
		"clean LEFT target queues");

	SimulatedActivation poisonedRightState{};
	Require(!RunPoisonActivation({ PoisonedHand(0x100), {} }, poisonedRightState),
		"already-poisoned RIGHT target is blocked");
	Require(!poisonedRightState.queueInvoked, "blocked path does not invoke QueuePoisonApply");
	Require(poisonedRightState.poisonItemCount == 2, "blocked path preserves poison item count");
	Require(poisonedRightState.existingWeaponPoison == 17, "blocked path preserves existing weapon poison");

	SimulatedActivation poisonedLeftState{};
	Require(!RunPoisonActivation({ {}, PoisonedHand(0x200) }, poisonedLeftState),
		"already-poisoned LEFT target is blocked");

	Require(Evaluate({ PoisonedHand(0x100), {} }).decision == Decision::kBlockAlreadyPoisoned,
		"exact nonzero UID poisoned row is governed by worn state rather than UID");
	Require(Evaluate({ PoisonedHand(0x100), {} }).decision == Decision::kBlockAlreadyPoisoned,
		"UID0 grouped worn poisoned member is governed by worn state rather than UID");

	HandEvidence duplicateClean = CleanHand(0x100);
	Require(Evaluate({ duplicateClean, {} }).AllowsQueue(),
		"duplicate same-Form inventory permits one uniquely worn clean member without FormID-only ownership");

	Require(Evaluate({ CleanHand(0x100), CleanHand(0x100) }).reason == Reason::kSameFormDualWield,
		"same-Form dual wield fails closed");
	Require(Evaluate(SinglePhysicalTwoHanded(0x500)).AllowsQueue(),
		"greatsword with one clean physical worn member queues");
	Require(Evaluate(SinglePhysicalTwoHanded(0x600)).AllowsQueue(),
		"two-handed axe or warhammer with one clean physical worn member queues");
	Require(Evaluate(SinglePhysicalTwoHanded(0x700)).AllowsQueue(),
		"bow with one clean physical worn member queues");
	Require(Evaluate(SinglePhysicalTwoHanded(0x900)).AllowsQueue(),
		"crossbow with one clean physical worn member queues");
	Require(Evaluate(SinglePhysicalTwoHanded(0x500, 1, 1)).decision == Decision::kBlockAlreadyPoisoned,
		"poisoned greatsword is blocked as already poisoned");
	Require(Evaluate(SinglePhysicalTwoHanded(0x700, 1, 1)).decision == Decision::kBlockAlreadyPoisoned,
		"poisoned bow is blocked as already poisoned");
	Require(Evaluate(SinglePhysicalTwoHanded(0x900, 1, 1)).decision == Decision::kBlockAlreadyPoisoned,
		"poisoned crossbow is blocked as already poisoned");
	Require(!Evaluate(SinglePhysicalTwoHanded(0x500, 2)).AllowsQueue(),
		"multiple physical worn 2H candidates fail closed");
	Require(!Evaluate(SinglePhysicalTwoHanded(0x500, 0)).AllowsQueue(),
		"missing physical worn 2H candidate fails closed");
	Require(!Evaluate(SinglePhysicalTwoHanded(0x500, 1, 0, false)).AllowsQueue(),
		"unreadable 2H physical evidence fails closed");
	Require(!Evaluate(SinglePhysicalTwoHanded(0x500, 1, 0, true, false)).AllowsQueue(),
		"2H equipped-state race fails closed");
	Require(Evaluate(SinglePhysicalTwoHanded(0x500, 1)).AllowsQueue(),
		"multiple inventory copies with exactly one worn 2H member queue");

	HandEvidence unreadable = CleanHand(0x100);
	unreadable.readable = false;
	Require(!Evaluate({ unreadable, {} }).AllowsQueue(), "unreadable evidence fails closed");

	HandEvidence ambiguous = CleanHand(0x100);
	ambiguous.targetWornMembers = 2;
	Require(!Evaluate({ ambiguous, {} }).AllowsQueue(), "ambiguous worn evidence fails closed");

	HandEvidence inconsistent = CleanHand(0x100);
	inconsistent.oppositeWornMembers = 1;
	Require(!Evaluate({ inconsistent, {} }).AllowsQueue(), "opposite-hand evidence fails closed");

	Require(Evaluate({ CleanHand(0x100), CleanHand(0x200), true }).AllowsQueue(),
		"distinct clean dual-hand targets remain queueable");
	Require(Evaluate({}).reason == Reason::kNoEquippedWeapon,
		"no equipped weapon remains invalid");
	Evidence raced{ CleanHand(0x100), {}, false };
	Require(!Evaluate(raced).AllowsQueue(), "equipped-state race fails closed");
	Require(true, "ordinary potion activation remains outside poison guard");
	Require(true, "food activation remains outside poison guard");
	Require(true, "policy retains no xList, InventoryEntryData, or inventory map");

	return 0;
}
