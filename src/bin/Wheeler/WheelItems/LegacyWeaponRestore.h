#pragma once

#include "LegacyWeaponRestorePolicy.h"

#include <RE/Skyrim.h>

#include <cstdint>
#include <string>

struct LegacyWeaponRestoreToken
{
	RE::FormID formID = 0;
	std::uint16_t uniqueID = 0;
	std::string logicalRowSignature;
	LegacyWeaponRestorePolicy::RowKind rowKind = LegacyWeaponRestorePolicy::RowKind::Invalid;

	[[nodiscard]] bool IsValid() const noexcept
	{
		return formID != 0 && rowKind != LegacyWeaponRestorePolicy::RowKind::Invalid;
	}

	void Clear() noexcept
	{
		*this = {};
	}
};

namespace LegacyWeaponRestore
{
	enum class CaptureRejectionReason : std::uint8_t
	{
		None,
		InvalidWeapon,
		NotInInventory,
		UnreadablePopulation,
		ConflictingLogicalRows,
		WornWeaponNotAttributable,
		AmbiguousPhysicalMembers
	};

	struct CaptureDiagnostic
	{
		CaptureRejectionReason reason = CaptureRejectionReason::None;
		LegacyWeaponRestorePolicy::Topology topology = LegacyWeaponRestorePolicy::Topology::Unknown;
		int sameFormCount = 0;
	};

	struct LiveSelection
	{
		RE::ExtraDataList* extraData = nullptr;
		LegacyWeaponRestorePolicy::Resolution resolution = LegacyWeaponRestorePolicy::Resolution::None;
	};

	enum class DualHandCaptureReconciliation : std::uint8_t
	{
		Unchanged,
		PromotedToGroupEquivalent,
		RejectedUnsafeCollision
	};

	bool CaptureWornToken(
		RE::TESObjectREFR::InventoryItemMap& a_inventory,
		RE::TESObjectWEAP* a_weapon,
		bool a_leftHand,
		LegacyWeaponRestoreToken& a_outToken,
		CaptureDiagnostic* a_outDiagnostic = nullptr);

	DualHandCaptureReconciliation ReconcileDualHandSameFormCapture(
		RE::TESObjectREFR::InventoryItemMap& a_inventory,
		LegacyWeaponRestoreToken& a_leftToken,
		LegacyWeaponRestoreToken& a_rightToken);

	bool ResolveLiveMember(
		RE::TESObjectREFR::InventoryItemMap& a_inventory,
		const LegacyWeaponRestoreToken& a_token,
		bool a_leftHand,
		LiveSelection& a_outSelection);

	bool MatchesWornMember(
		RE::TESObjectREFR::InventoryItemMap& a_inventory,
		const LegacyWeaponRestoreToken& a_token,
		bool a_leftHand);
}
