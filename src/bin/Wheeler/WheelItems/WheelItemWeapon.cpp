#include "WheelItemWeapon.h"
#include "bin/Rendering/Drawer.h"
#include "bin/Utilities/Utils.h"
#include "bin/Utilities/ActorVirtualCompat.h"
#include "bin/Config.h"
#include "bin/Wheeler/MainWheelDebug.h"
#include "bin/Wheeler/TransformWheelManager.h"
#include "bin/Wheeler/Wheeler.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <string_view>

namespace
{
	std::string NormalizeLoose(std::string_view value)
	{
		std::string normalized;
		normalized.reserve(value.size());
		for (unsigned char ch : value) {
			if (std::isalnum(ch)) {
				normalized.push_back(static_cast<char>(std::tolower(ch)));
			}
		}
		return normalized;
	}

	bool IsPlaceholderName(const char* text)
	{
		if (!text || text[0] == '\0') {
			return true;
		}
		const std::string compact = NormalizeLoose(text);
		return compact.empty() ||
			compact == "null" ||
			compact.find("missingname") != std::string::npos ||
			compact.find("missingitem") != std::string::npos;
	}

	const char* GetWeaponKindName(RE::TESObjectWEAP* a_weapon)
	{
		if (!a_weapon) {
			return "none";
		}
		if (a_weapon->IsBow()) {
			return "bow";
		}
		if (a_weapon->IsCrossbow()) {
			return "crossbow";
		}
		switch (a_weapon->GetWeaponType()) {
		case RE::WEAPON_TYPE::kTwoHandSword:
			return "two_hand_sword";
		case RE::WEAPON_TYPE::kTwoHandAxe:
			return "two_hand_axe";
		default:
			return "weapon";
		}
	}

	RE::InventoryEntryData* FindInventoryEntryByForm(RE::TESObjectREFR::InventoryItemMap& a_inv, RE::FormID a_formID)
	{
		if (a_formID == 0) {
			return nullptr;
		}
		for (auto& [boundObj, data] : a_inv) {
			if (boundObj && boundObj->GetFormID() == a_formID) {
				return data.second.get();
			}
		}
		return nullptr;
	}

	float GetWeaponDamageSafe(RE::PlayerCharacter* a_player, RE::InventoryEntryData* a_entry, RE::TESObjectWEAP* a_weapon)
	{
		if (!a_weapon) {
			return 0.0f;
		}

		float damage = static_cast<float>(a_weapon->GetAttackDamage());
		if (!a_player || !a_entry) {
			return damage;
		}

#if defined(_MSC_VER)
		__try {
			damage = a_player->GetDamage(a_entry);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			// Corrupted or stale InventoryEntryData can crash vanilla stat computation.
			// Keep wheel stable by falling back to base weapon damage.
		}
#else
		try {
			damage = a_player->GetDamage(a_entry);
		} catch (...) {
			// Fallback to base damage on any exception.
		}
#endif

		return damage;
	}

	template <class Fn>
	bool InvokeWithSehGuard(Fn&& a_fn)
	{
#if defined(_MSC_VER)
		__try {
			a_fn();
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
#else
		try {
			a_fn();
			return true;
		} catch (...) {
			return false;
		}
#endif
	}

	template <class TContainer>
	bool CopyExtraListsSafe(TContainer* a_extraLists, std::vector<RE::ExtraDataList*>& a_out)
	{
		if (!a_extraLists) {
			return false;
		}

		return InvokeWithSehGuard([&]() {
			for (auto* extraList : *a_extraLists) {
				a_out.push_back(extraList);
			}
		});
	}

	bool TryHasTypeSafe(RE::ExtraDataList* a_list, RE::ExtraDataType a_type, bool& a_outHasType)
	{
		a_outHasType = false;
		if (!a_list) {
			return false;
		}
		return InvokeWithSehGuard([&]() { a_outHasType = a_list->HasType(a_type); });
	}

	template <class T>
	T* GetByTypeSafe(RE::ExtraDataList* a_list)
	{
		T* result = nullptr;
		if (!a_list) {
			return nullptr;
		}
		if (!InvokeWithSehGuard([&]() { result = a_list->GetByType<T>(); })) {
			return nullptr;
		}
		return result;
	}

	bool HasTypeSafe(RE::ExtraDataList* a_list, RE::ExtraDataType a_type)
	{
		bool hasType = false;
		if (!a_list) {
			return false;
		}
		if (!InvokeWithSehGuard([&]() { hasType = a_list->HasType(a_type); })) {
			return false;
		}
		return hasType;
	}

	int GetExtraListCountSafe(RE::ExtraDataList* a_list)
	{
		int count = 1;
		if (!a_list) {
			return 0;
		}
		if (!InvokeWithSehGuard([&]() { count = a_list->GetCount(); })) {
			return 1;
		}
		return (std::max)(count, 1);
	}

	bool IsPerInstanceModifiedExtraData(RE::ExtraDataList* a_extraData)
	{
		if (!a_extraData) {
			return false;
		}
		return HasTypeSafe(a_extraData, RE::ExtraDataType::kEnchantment) ||
		       HasTypeSafe(a_extraData, RE::ExtraDataType::kPoison);
	}

	int GetSameFormInventoryCount(const RE::TESObjectREFR::InventoryItemMap& a_inv, RE::TESObjectWEAP* a_weapon);

	bool IsOneHandedWeaponForCompat(RE::TESObjectWEAP* a_weapon)
	{
		if (!a_weapon) {
			return false;
		}

		if (a_weapon->IsBow() || a_weapon->IsCrossbow()) {
			return false;
		}

		switch (a_weapon->GetWeaponType()) {
		case RE::WEAPON_TYPE::kTwoHandSword:
		case RE::WEAPON_TYPE::kTwoHandAxe:
		case RE::WEAPON_TYPE::kStaff:
			return false;
		default:
			return true;
		}
	}

	bool IsInstanceSpecificIndicatorExtraData(RE::ExtraDataList* a_extraData)
	{
		if (!a_extraData) {
			return false;
		}
		return HasTypeSafe(a_extraData, RE::ExtraDataType::kEnchantment) ||
		       HasTypeSafe(a_extraData, RE::ExtraDataType::kPoison) ||
		       HasTypeSafe(a_extraData, RE::ExtraDataType::kHealth) ||
		       HasTypeSafe(a_extraData, RE::ExtraDataType::kCharge) ||
		       GetByTypeSafe<RE::ExtraTextDisplayData>(a_extraData) != nullptr;
	}

	bool MatchesRequestedHandWorn(RE::ExtraDataList* a_extraData, bool a_leftHand)
	{
		bool hasWorn = false;
		bool hasWornLeft = false;
		const bool readWorn = TryHasTypeSafe(a_extraData, RE::ExtraDataType::kWorn, hasWorn);
		const bool readWornLeft = TryHasTypeSafe(a_extraData, RE::ExtraDataType::kWornLeft, hasWornLeft);
		if (!readWorn || !readWornLeft) {
			return false;
		}
		return a_leftHand ? hasWornLeft : (hasWorn && !hasWornLeft);
	}

	bool HasMixedSameFormIndicatorSiblings(RE::TESObjectREFR::InventoryItemMap& a_inv, RE::TESObjectWEAP* a_weapon)
	{
		if (!a_weapon || GetSameFormInventoryCount(a_inv, a_weapon) < 2) {
			return false;
		}

		RE::InventoryEntryData* entry = FindInventoryEntryByForm(a_inv, a_weapon->GetFormID());
		if (!entry || !entry->extraLists) {
			return false;
		}

		std::vector<RE::ExtraDataList*> extraListSnapshot;
		if (!CopyExtraListsSafe(entry->extraLists, extraListSnapshot)) {
			return false;
		}

		for (auto* extraList : extraListSnapshot) {
			if (IsInstanceSpecificIndicatorExtraData(extraList)) {
				return true;
			}
		}
		return false;
	}

	std::optional<bool> MatchCleanSentinelHandIndicatorFromInventory(
		RE::TESObjectREFR::InventoryItemMap& a_inv,
		RE::TESObjectWEAP* a_weapon,
		std::uint64_t a_handSignature,
		bool a_leftHand)
	{
		if (!a_weapon) {
			return std::nullopt;
		}

		RE::InventoryEntryData* entry = FindInventoryEntryByForm(a_inv, a_weapon->GetFormID());
		if (!entry || !entry->extraLists) {
			return std::nullopt;
		}

		std::vector<RE::ExtraDataList*> extraListSnapshot;
		if (!CopyExtraListsSafe(entry->extraLists, extraListSnapshot)) {
			return std::nullopt;
		}

		if (a_handSignature != 0) {
			for (auto* extraList : extraListSnapshot) {
				if (!extraList) {
					continue;
				}
				auto* uniqueData = GetByTypeSafe<RE::ExtraUniqueID>(extraList);
				if (uniqueData && uniqueData->uniqueID == a_handSignature) {
					return !IsInstanceSpecificIndicatorExtraData(extraList);
				}
			}
			return std::nullopt;
		}

		bool foundRequestedHandWorn = false;
		for (auto* extraList : extraListSnapshot) {
			if (!extraList || !MatchesRequestedHandWorn(extraList, a_leftHand)) {
				continue;
			}

			foundRequestedHandWorn = true;
			if (!IsInstanceSpecificIndicatorExtraData(extraList)) {
				return true;
			}
		}

		if (foundRequestedHandWorn) {
			return false;
		}

		return std::nullopt;
	}

	int GetSameFormInventoryCount(const RE::TESObjectREFR::InventoryItemMap& a_inv, RE::TESObjectWEAP* a_weapon)
	{
		if (!a_weapon) {
			return 0;
		}

		const RE::FormID formID = a_weapon->GetFormID();
		int totalCount = 0;
		for (const auto& [boundObj, data] : a_inv) {
			if (!boundObj || boundObj->GetFormID() != formID) {
				continue;
			}
			totalCount += data.first;
		}
		return totalCount;
	}

	bool ShouldBypassInstanceHandResolution(
		const RE::TESObjectREFR::InventoryItemMap& a_inv,
		RE::TESObjectWEAP* a_weapon,
		RE::ExtraDataList* a_extraData)
	{
		return a_weapon &&
		       IsPerInstanceModifiedExtraData(a_extraData) &&
		       GetSameFormInventoryCount(a_inv, a_weapon) > 1;
	}

	Utils::Inventory::Hand GetEquippedHandByForm(RE::PlayerCharacter* a_player, RE::TESObjectWEAP* a_weapon)
	{
		if (!a_player || !a_weapon) {
			return Utils::Inventory::Hand::None;
		}

		const RE::FormID formID = a_weapon->GetFormID();
		const bool leftMatch = a_player->GetEquippedObject(true) &&
		                       a_player->GetEquippedObject(true)->GetFormID() == formID;
		const bool rightMatch = a_player->GetEquippedObject(false) &&
		                        a_player->GetEquippedObject(false)->GetFormID() == formID;

		if (leftMatch && rightMatch) {
			return Utils::Inventory::Hand::Both;
		}
		if (leftMatch) {
			return Utils::Inventory::Hand::Left;
		}
		if (rightMatch) {
			return Utils::Inventory::Hand::Right;
		}
		return Utils::Inventory::Hand::None;
	}

	std::uint64_t ResolveEquippedHandSignature(RE::Actor* a_actor, bool a_leftHand, RE::FormID a_expectedFormID)
	{
		if (!a_actor || a_expectedFormID == 0) {
			return 0;
		}

		RE::InventoryEntryData* entry = a_actor->GetEquippedEntryData(a_leftHand);
		if (!entry || !entry->object || entry->object->GetFormID() != a_expectedFormID || !entry->extraLists) {
			return 0;
		}

		std::vector<RE::ExtraDataList*> extraListSnapshot;
		if (!CopyExtraListsSafe(entry->extraLists, extraListSnapshot)) {
			return 0;
		}

		std::uint64_t fallbackSignature = 0;
		int uniqueCandidateCount = 0;
		for (auto* extraList : extraListSnapshot) {
			if (!extraList) {
				continue;
			}

			bool hasUniqueID = false;
			if (!TryHasTypeSafe(extraList, RE::ExtraDataType::kUniqueID, hasUniqueID) || !hasUniqueID) {
				continue;
			}

			auto* uniqueData = GetByTypeSafe<RE::ExtraUniqueID>(extraList);
			if (!uniqueData || uniqueData->uniqueID == 0) {
				continue;
			}

			++uniqueCandidateCount;
			if (fallbackSignature == 0) {
				fallbackSignature = uniqueData->uniqueID;
			}

			bool hasWorn = false;
			bool hasWornLeft = false;
			const bool readWorn = TryHasTypeSafe(extraList, RE::ExtraDataType::kWorn, hasWorn);
			const bool readWornLeft = TryHasTypeSafe(extraList, RE::ExtraDataType::kWornLeft, hasWornLeft);
			if (!readWorn || !readWornLeft) {
				continue;
			}

			if (a_leftHand ? hasWornLeft : (hasWorn && !hasWornLeft)) {
				return uniqueData->uniqueID;
			}
		}

		if (uniqueCandidateCount == 1) {
			return fallbackSignature;
		}

		return 0;
	}

	Utils::Inventory::Hand GetEquippedHandByExactUniqueID(RE::PlayerCharacter* a_player, RE::TESObjectWEAP* a_weapon, std::uint16_t a_uniqueID)
	{
		if (!a_player || !a_weapon || a_uniqueID == 0) {
			return Utils::Inventory::Hand::None;
		}

		const RE::FormID formID = a_weapon->GetFormID();
		const bool leftMatch = ResolveEquippedHandSignature(a_player, true, formID) == a_uniqueID;
		const bool rightMatch = ResolveEquippedHandSignature(a_player, false, formID) == a_uniqueID;

		if (leftMatch && rightMatch) {
			return Utils::Inventory::Hand::Both;
		}
		if (leftMatch) {
			return Utils::Inventory::Hand::Left;
		}
		if (rightMatch) {
			return Utils::Inventory::Hand::Right;
		}
		return Utils::Inventory::Hand::None;
	}

	const char* GetHandName(Utils::Inventory::Hand a_hand)
	{
		switch (a_hand) {
		case Utils::Inventory::Hand::Left:
			return "left";
		case Utils::Inventory::Hand::Right:
			return "right";
		case Utils::Inventory::Hand::Both:
			return "both";
		case Utils::Inventory::Hand::None:
		default:
			return "none";
		}
	}

	Utils::Inventory::Hand GetTargetHand(bool a_toRight)
	{
		return a_toRight ? Utils::Inventory::Hand::Right : Utils::Inventory::Hand::Left;
	}

	RE::BGSEquipSlot* GetSlotForHand(Utils::Inventory::Hand a_hand)
	{
		switch (a_hand) {
		case Utils::Inventory::Hand::Left:
			return Utils::Slot::GetLeftHandSlot();
		case Utils::Inventory::Hand::Right:
			return Utils::Slot::GetRightHandSlot();
		default:
			return nullptr;
		}
	}

	bool IsWeaponDrawnSafe(RE::PlayerCharacter* a_player)
	{
		if (!a_player) {
			return false;
		}
		auto* actorState = a_player->AsActorState();
		return actorState && actorState->IsWeaponDrawn();
	}

	bool IsImmersiveWeaponSwitchLoaded()
	{
		const bool loaded = ::GetModuleHandleW(L"ImmersiveWeaponSwitch.dll") != nullptr;
		static bool loggedLoaded = false;
		if (loaded && !loggedLoaded) {
			logger::info("IWSCompat: ImmersiveWeaponSwitch.dll detected; Wheeler exact single-copy weapon transfer safe mode enabled");
			loggedLoaded = true;
		}
		return loaded;
	}

	struct ExactWeaponInventoryState
	{
		int sameFormCount = 0;
		int representedCount = 0;
		int exactCount = 0;
		int uniqueCandidateCount = 0;
		RE::ExtraDataList* exactExtraData = nullptr;
		bool readFailed = false;
	};

	ExactWeaponInventoryState ResolveExactWeaponInventoryState(
		RE::TESObjectREFR::InventoryItemMap& a_inv,
		RE::FormID a_formID,
		std::uint16_t a_uniqueID)
	{
		ExactWeaponInventoryState state;
		if (a_formID == 0 || a_uniqueID == 0) {
			return state;
		}

		std::vector<std::uint16_t> uniqueIDs;
		for (auto& [boundObj, data] : a_inv) {
			if (!boundObj || boundObj->GetFormID() != a_formID) {
				continue;
			}

			state.sameFormCount += data.first;
			auto* entry = data.second.get();
			if (!entry || !entry->extraLists) {
				continue;
			}

			std::vector<RE::ExtraDataList*> extraListSnapshot;
			if (!CopyExtraListsSafe(entry->extraLists, extraListSnapshot)) {
				state.readFailed = true;
				continue;
			}

			for (auto* extraList : extraListSnapshot) {
				if (!extraList) {
					continue;
				}
				state.representedCount += GetExtraListCountSafe(extraList);
				auto* uniqueData = GetByTypeSafe<RE::ExtraUniqueID>(extraList);
				if (!uniqueData || uniqueData->uniqueID == 0) {
					continue;
				}
				if (std::find(uniqueIDs.begin(), uniqueIDs.end(), uniqueData->uniqueID) == uniqueIDs.end()) {
					uniqueIDs.push_back(uniqueData->uniqueID);
				}
				if (uniqueData->uniqueID != a_uniqueID) {
					continue;
				}
				state.exactExtraData = extraList;
				state.exactCount += GetExtraListCountSafe(extraList);
			}
		}

		state.uniqueCandidateCount = static_cast<int>(uniqueIDs.size());
		return state;
	}

	bool IsStrictSingleExactInventoryState(const ExactWeaponInventoryState& a_state)
	{
		return !a_state.readFailed &&
		       a_state.sameFormCount == 1 &&
		       a_state.representedCount <= 1 &&
		       a_state.exactCount == 1 &&
		       a_state.uniqueCandidateCount == 1 &&
		       a_state.exactExtraData != nullptr;
	}

	using IWSClock = std::chrono::steady_clock;
	constexpr auto kIWSCompatSheatheTimeout = std::chrono::milliseconds(1800);
	constexpr auto kIWSCompatSettleDelay = std::chrono::milliseconds(120);
	constexpr auto kIWSCompatSheathePulseDelay = std::chrono::milliseconds(250);

	struct PendingIWSExactWeaponTransfer
	{
		bool active = false;
		RE::FormID formID = 0;
		std::uint16_t uniqueID = 0;
		bool toRight = true;
		bool restoreDrawn = false;
		IWSClock::time_point queuedAt{};
		IWSClock::time_point notDrawnSince{};
		IWSClock::time_point lastSheathePulse{};
	};

	PendingIWSExactWeaponTransfer g_pendingIWSExactWeaponTransfer;

	void RestoreDrawForPendingIWSExactWeaponTransfer(const char* a_reason)
	{
		if (!g_pendingIWSExactWeaponTransfer.active || !g_pendingIWSExactWeaponTransfer.restoreDrawn) {
			return;
		}
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc || !pc->Is3DLoaded()) {
			return;
		}
		InvokeWithSehGuard([&]() { pc->DrawWeaponMagicHands(true); });
		logger::info(
			"IWSCompat: requested draw restore after {} formId={:08X} uniqueID={} targetHand={}",
			a_reason ? a_reason : "abort",
			g_pendingIWSExactWeaponTransfer.formID,
			g_pendingIWSExactWeaponTransfer.uniqueID,
			g_pendingIWSExactWeaponTransfer.toRight ? "right" : "left");
	}

	void ClearPendingIWSExactWeaponTransfer(const char* a_reason)
	{
		if (g_pendingIWSExactWeaponTransfer.active) {
			logger::info(
				"IWSCompat: exact transfer cleared reason={} formId={:08X} uniqueID={} targetHand={}",
				a_reason ? a_reason : "unknown",
				g_pendingIWSExactWeaponTransfer.formID,
				g_pendingIWSExactWeaponTransfer.uniqueID,
			g_pendingIWSExactWeaponTransfer.toRight ? "right" : "left");
		}
		g_pendingIWSExactWeaponTransfer = {};
	}

	void AbortPendingIWSExactWeaponTransfer(const char* a_reason, bool a_restoreDraw)
	{
		if (a_restoreDraw) {
			RestoreDrawForPendingIWSExactWeaponTransfer(a_reason);
		}
		ClearPendingIWSExactWeaponTransfer(a_reason);
	}

	bool QueueIWSExactSingleWeaponTransfer(
		RE::PlayerCharacter* a_player,
		RE::TESObjectWEAP* a_weapon,
		std::uint16_t a_uniqueID,
		bool a_toRight,
		int a_count,
		int a_sameFormCount,
		RE::ExtraDataList* a_extraData,
		bool a_bypassInstanceHandResolution)
	{
		if (!IsImmersiveWeaponSwitchLoaded() || !a_player || !a_weapon || !IsOneHandedWeaponForCompat(a_weapon)) {
			return false;
		}
		if (a_bypassInstanceHandResolution || a_uniqueID == 0 || a_count != 1 || a_sameFormCount != 1 || !a_extraData) {
			return false;
		}

		const Utils::Inventory::Hand currentHand =
			GetEquippedHandByExactUniqueID(a_player, a_weapon, a_uniqueID);
		const Utils::Inventory::Hand targetHand = GetTargetHand(a_toRight);
		const bool oppositeHandTransfer =
			(currentHand == Utils::Inventory::Hand::Right && targetHand == Utils::Inventory::Hand::Left) ||
			(currentHand == Utils::Inventory::Hand::Left && targetHand == Utils::Inventory::Hand::Right);
		if (!oppositeHandTransfer) {
			return false;
		}

		const auto now = IWSClock::now();
		const bool replacingPending = g_pendingIWSExactWeaponTransfer.active;
		const bool samePending =
			replacingPending &&
			g_pendingIWSExactWeaponTransfer.formID == a_weapon->GetFormID() &&
			g_pendingIWSExactWeaponTransfer.uniqueID == a_uniqueID &&
			g_pendingIWSExactWeaponTransfer.toRight == a_toRight;
		if (samePending) {
			g_pendingIWSExactWeaponTransfer.restoreDrawn =
				g_pendingIWSExactWeaponTransfer.restoreDrawn ||
				Config::WheelBehavior::AutoDrawOnUse ||
				IsWeaponDrawnSafe(a_player);
			logger::info(
				"IWSCompat: coalesced exact single-copy transfer formId={:08X} uniqueID={} targetHand={} restoreDrawn={}",
				a_weapon->GetFormID(),
				a_uniqueID,
				a_toRight ? "right" : "left",
				g_pendingIWSExactWeaponTransfer.restoreDrawn ? 1 : 0);
			return true;
		}

		RE::TESObjectREFR::InventoryItemMap queueInv = a_player->GetInventory();
		ExactWeaponInventoryState invState = ResolveExactWeaponInventoryState(
			queueInv,
			a_weapon->GetFormID(),
			a_uniqueID);
		if (!IsStrictSingleExactInventoryState(invState)) {
			logger::warn(
				"IWSCompat: suppressed exact transfer due malformed single-copy inventory formId={:08X} uniqueID={} sameFormCount={} representedCount={} exactCount={} uniqueCandidates={} hasExtraData={} readFailed={}",
				a_weapon->GetFormID(),
				a_uniqueID,
				invState.sameFormCount,
				invState.representedCount,
				invState.exactCount,
				invState.uniqueCandidateCount,
				invState.exactExtraData ? 1 : 0,
				invState.readFailed ? 1 : 0);
			return true;
		}

		g_pendingIWSExactWeaponTransfer.active = true;
		g_pendingIWSExactWeaponTransfer.formID = a_weapon->GetFormID();
		g_pendingIWSExactWeaponTransfer.uniqueID = a_uniqueID;
		g_pendingIWSExactWeaponTransfer.toRight = a_toRight;
		g_pendingIWSExactWeaponTransfer.restoreDrawn =
			Config::WheelBehavior::AutoDrawOnUse || IsWeaponDrawnSafe(a_player);
		g_pendingIWSExactWeaponTransfer.queuedAt = now;
		g_pendingIWSExactWeaponTransfer.notDrawnSince = {};
		g_pendingIWSExactWeaponTransfer.lastSheathePulse = {};

		logger::info(
			"IWSCompat: queued exact single-copy transfer formId={:08X} uniqueID={} sourceHand={} targetHand={} restoreDrawn={} replacingPending={}",
			a_weapon->GetFormID(),
			a_uniqueID,
			GetHandName(currentHand),
			GetHandName(targetHand),
			g_pendingIWSExactWeaponTransfer.restoreDrawn ? 1 : 0,
			replacingPending ? 1 : 0);

		if (IsWeaponDrawnSafe(a_player)) {
			InvokeWithSehGuard([&]() { a_player->DrawWeaponMagicHands(false); });
			g_pendingIWSExactWeaponTransfer.lastSheathePulse = now;
		}
		return true;
	}

	void ProcessPendingIWSExactWeaponTransfer()
	{
		if (!g_pendingIWSExactWeaponTransfer.active) {
			return;
		}

		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc || !pc->Is3DLoaded()) {
			AbortPendingIWSExactWeaponTransfer("player_unavailable", false);
			return;
		}

		auto* weapon = RE::TESForm::LookupByID<RE::TESObjectWEAP>(g_pendingIWSExactWeaponTransfer.formID);
		if (!weapon || !IsOneHandedWeaponForCompat(weapon)) {
			AbortPendingIWSExactWeaponTransfer("weapon_unavailable", true);
			return;
		}

		const auto now = IWSClock::now();
		if (IsWeaponDrawnSafe(pc)) {
			if (now - g_pendingIWSExactWeaponTransfer.queuedAt >= kIWSCompatSheatheTimeout) {
				AbortPendingIWSExactWeaponTransfer("sheathe_timeout", true);
				return;
			}

			if (g_pendingIWSExactWeaponTransfer.lastSheathePulse == IWSClock::time_point{} ||
				now - g_pendingIWSExactWeaponTransfer.lastSheathePulse >= kIWSCompatSheathePulseDelay) {
				InvokeWithSehGuard([&]() { pc->DrawWeaponMagicHands(false); });
				g_pendingIWSExactWeaponTransfer.lastSheathePulse = now;
			}
			g_pendingIWSExactWeaponTransfer.notDrawnSince = {};
			return;
		}

		if (g_pendingIWSExactWeaponTransfer.notDrawnSince == IWSClock::time_point{}) {
			g_pendingIWSExactWeaponTransfer.notDrawnSince = now;
			return;
		}
		if (now - g_pendingIWSExactWeaponTransfer.notDrawnSince < kIWSCompatSettleDelay) {
			return;
		}

		RE::TESObjectREFR::InventoryItemMap inv = pc->GetInventory();
		ExactWeaponInventoryState invState = ResolveExactWeaponInventoryState(
			inv,
			g_pendingIWSExactWeaponTransfer.formID,
			g_pendingIWSExactWeaponTransfer.uniqueID);
		if (!IsStrictSingleExactInventoryState(invState)) {
			logger::warn(
				"IWSCompat: exact transfer aborted by inventory guard formId={:08X} uniqueID={} sameFormCount={} representedCount={} exactCount={} uniqueCandidates={} hasExtraData={} readFailed={}",
				g_pendingIWSExactWeaponTransfer.formID,
				g_pendingIWSExactWeaponTransfer.uniqueID,
				invState.sameFormCount,
				invState.representedCount,
				invState.exactCount,
				invState.uniqueCandidateCount,
				invState.exactExtraData ? 1 : 0,
				invState.readFailed ? 1 : 0);
			AbortPendingIWSExactWeaponTransfer("inventory_guard", true);
			return;
		}

		const Utils::Inventory::Hand sourceHand =
			GetEquippedHandByExactUniqueID(pc, weapon, g_pendingIWSExactWeaponTransfer.uniqueID);
		const Utils::Inventory::Hand targetHand = GetTargetHand(g_pendingIWSExactWeaponTransfer.toRight);
		if (sourceHand == targetHand) {
			if (g_pendingIWSExactWeaponTransfer.restoreDrawn) {
				InvokeWithSehGuard([&]() { pc->DrawWeaponMagicHands(true); });
			}
			ClearPendingIWSExactWeaponTransfer("already_target_hand");
			return;
		}

		const bool sourceIsOpposite =
			(sourceHand == Utils::Inventory::Hand::Right && targetHand == Utils::Inventory::Hand::Left) ||
			(sourceHand == Utils::Inventory::Hand::Left && targetHand == Utils::Inventory::Hand::Right);
		if (!sourceIsOpposite) {
			logger::warn(
				"IWSCompat: exact transfer aborted by equipped-hand guard formId={:08X} uniqueID={} sourceHand={} targetHand={}",
				g_pendingIWSExactWeaponTransfer.formID,
				g_pendingIWSExactWeaponTransfer.uniqueID,
				GetHandName(sourceHand),
				GetHandName(targetHand));
			AbortPendingIWSExactWeaponTransfer("equipped_hand_guard", true);
			return;
		}

		auto* aeMan = RE::ActorEquipManager::GetSingleton();
		auto* sourceSlot = GetSlotForHand(sourceHand);
		auto* targetSlot = GetSlotForHand(targetHand);
		if (!aeMan || !sourceSlot || !targetSlot) {
			AbortPendingIWSExactWeaponTransfer("equip_manager_or_slot_unavailable", true);
			return;
		}

		logger::info(
			"IWSCompat: executing exact transfer while sheathed formId={:08X} uniqueID={} sourceHand={} targetHand={}",
			g_pendingIWSExactWeaponTransfer.formID,
			g_pendingIWSExactWeaponTransfer.uniqueID,
			GetHandName(sourceHand),
			GetHandName(targetHand));

		aeMan->UnequipObject(pc, weapon, nullptr, 1, sourceSlot, false, true, true);

		RE::TESObjectREFR::InventoryItemMap postUnequipInv = pc->GetInventory();
		ExactWeaponInventoryState postUnequipState = ResolveExactWeaponInventoryState(
			postUnequipInv,
			g_pendingIWSExactWeaponTransfer.formID,
			g_pendingIWSExactWeaponTransfer.uniqueID);
		if (!IsStrictSingleExactInventoryState(postUnequipState)) {
			logger::warn(
				"IWSCompat: exact transfer aborted after unequip formId={:08X} uniqueID={} sameFormCount={} representedCount={} exactCount={} uniqueCandidates={} hasExtraData={} readFailed={}",
				g_pendingIWSExactWeaponTransfer.formID,
				g_pendingIWSExactWeaponTransfer.uniqueID,
				postUnequipState.sameFormCount,
				postUnequipState.representedCount,
				postUnequipState.exactCount,
				postUnequipState.uniqueCandidateCount,
				postUnequipState.exactExtraData ? 1 : 0,
				postUnequipState.readFailed ? 1 : 0);
			AbortPendingIWSExactWeaponTransfer("post_unequip_inventory_guard", true);
			return;
		}

		aeMan->EquipObject(pc, weapon, postUnequipState.exactExtraData, 1, targetSlot);

		if (g_pendingIWSExactWeaponTransfer.restoreDrawn) {
			InvokeWithSehGuard([&]() { pc->DrawWeaponMagicHands(true); });
		}

		ClearPendingIWSExactWeaponTransfer("completed");
	}
}

void WheelItemWeapon::ProcessIWSCompatTransfer()
{
	ProcessPendingIWSExactWeaponTransfer();
}

void WheelItemWeapon::DrawSlot(ImVec2 a_center, bool a_hovered, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs)
{
	auto* weapon = this->_obj ? this->_obj->As<RE::TESObjectWEAP>() : nullptr;
	if (TransformWheelManager::ShouldDimWeaponActivation(weapon)) {
		a_drawArgs.alphaMult *= 0.35f;
	}
	std::string text = this->GetDisplayName(a_imap);
	if (weapon && weapon->GetWeaponType() == RE::WEAPON_TYPE::kHandToHandMelee && IsPlaceholderName(text.c_str())) {
		text = "Unarmed";
	}
	int itemCount = this->GetItemExtraDataAndCount(a_imap).first;
	if (itemCount > 1) {
		text += " (" + std::to_string(itemCount) + ")";
	}
	this->drawSlotText(a_center, text.c_str(), a_drawArgs);
	this->drawSlotTexture(a_center, a_drawArgs);
}

void WheelItemWeapon::DrawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs)
{
	std::string displayName = this->GetDisplayName(a_imap);
	auto* weapon = this->_obj ? this->_obj->As<RE::TESObjectWEAP>() : nullptr;
	if (TransformWheelManager::ShouldDimWeaponActivation(weapon)) {
		a_drawArgs.alphaMult *= 0.35f;
	}
	if (weapon && weapon->GetWeaponType() == RE::WEAPON_TYPE::kHandToHandMelee && IsPlaceholderName(displayName.c_str())) {
		displayName = "Unarmed";
	}

	RE::InventoryEntryData* invData = FindInventoryEntryByForm(a_imap, this->GetFormID());

	std::string descriptionBuf = "";
	if (!this->_description.empty()) {
		descriptionBuf = this->_description;
	}

	std::vector<RE::EnchantmentItem*> enchants;
	this->GetItemEnchantment(a_imap, enchants);
	if (!enchants.empty()) {
		std::string enchantDescription;
		for (auto* enchant : enchants) {
			if (!enchant) {
				continue;
			}
			Utils::Magic::GetMagicItemDescription(enchant, enchantDescription);
			if (!enchantDescription.empty()) {
				break;
			}
		}
		if (!enchantDescription.empty()) {
			if (descriptionBuf.empty()) {
				descriptionBuf = enchantDescription;
			} else if (descriptionBuf.find(enchantDescription) == std::string::npos) {
				descriptionBuf += "\n";
				descriptionBuf += enchantDescription;
			}
		}
	}
	const float textShiftY = calculateHighlightTextShiftY(descriptionBuf.c_str());
	this->drawHighlightText(a_center, displayName.c_str(), a_drawArgs, textShiftY);
	this->drawHighlightTexture(a_center, a_drawArgs);
	
	if (!descriptionBuf.empty()) {
		this->drawHighlightDescription(a_center, descriptionBuf.data(), a_drawArgs, textShiftY);
	}

	auto* player = RE::PlayerCharacter::GetSingleton();
	const float weaponDamage = GetWeaponDamageSafe(player, invData, weapon);
	drawItemHighlightStatIconAndValue(a_center, this->_stat_texture, weaponDamage, a_drawArgs);
}

std::optional<bool> WheelItemWeapon::MatchesEquippedHandIndicator(
	RE::TESObjectREFR::InventoryItemMap& a_inv,
	RE::FormID a_handFormID,
	std::uint64_t a_handSignature,
	bool a_leftHand) const
{
	auto* weapon = this->_obj ? this->_obj->As<RE::TESObjectWEAP>() : nullptr;
	if (!weapon || this->GetUniqueID() != 0) {
		return std::nullopt;
	}
	if (a_handFormID == 0 || a_handFormID != weapon->GetFormID()) {
		return std::nullopt;
	}
	if (!HasMixedSameFormIndicatorSiblings(a_inv, weapon)) {
		return std::nullopt;
	}
	return MatchCleanSentinelHandIndicatorFromInventory(a_inv, weapon, a_handSignature, a_leftHand);
}

WheelItemWeapon::WheelItemWeapon(RE::TESBoundObject* a_weapon, uint16_t a_uniqueID)
{
	this->_obj = a_weapon;
	this->SetUniqueID(a_uniqueID);
	// get weapon's texture
	// TODO: add support for animated armory/2h mace
	Texture::icon_image_type iconType = Texture::icon_image_type::sword_one_handed;
	switch (a_weapon->As<RE::TESObjectWEAP>()->GetWeaponType()) {
	case RE::WEAPON_TYPE::kBow:
		iconType = Texture::icon_image_type::bow;
		break;
	case RE::WEAPON_TYPE::kCrossbow:
		iconType = Texture::icon_image_type::crossbow;
		break;
	case RE::WEAPON_TYPE::kStaff:
		iconType = Texture::icon_image_type::staff;
		break;
	case RE::WEAPON_TYPE::kHandToHandMelee:
		iconType = Texture::icon_image_type::hand_to_hand;
		break;
	case RE::WEAPON_TYPE::kOneHandSword:
		iconType = Texture::icon_image_type::sword_one_handed;
		break;
	case RE::WEAPON_TYPE::kOneHandDagger:
		iconType = Texture::icon_image_type::dagger;
		break;
	case RE::WEAPON_TYPE::kOneHandAxe:
		iconType = Texture::icon_image_type::axe_one_handed;
		break;
	case RE::WEAPON_TYPE::kOneHandMace:
		iconType = Texture::icon_image_type::mace;
		break;
	case RE::WEAPON_TYPE::kTwoHandSword:
		iconType = Texture::icon_image_type::sword_two_handed;
		break;
	case RE::WEAPON_TYPE::kTwoHandAxe:
	{
		if (a_weapon->As<RE::TESObjectWEAP>()->HasKeywordString("WeapTypeWarhammer")) {
			iconType = Texture::icon_image_type::warhammer_two_handed;
		} else {
			iconType = Texture::icon_image_type::axe_two_handed;
		}
	}
		break;
	}
	_texture = Texture::GetIconImage(iconType, a_weapon);
	_stat_texture = Texture::GetIconImage(Texture::icon_image_type::weapon_damage, nullptr);

	RE::BSString descriptionBuf = "";
	a_weapon->As<RE::TESObjectWEAP>()->GetDescription(descriptionBuf, nullptr);
	this->_description = descriptionBuf.c_str();
}

void WheelItemWeapon::ActivateItemSecondary()
{
	auto* weapon = this->_obj ? this->_obj->As<RE::TESObjectWEAP>() : nullptr;
	if (TransformWheelManager::ShouldBlockWeaponActivation(weapon)) {
		logger::info("TransformWheels: blocked weapon activation source=EquipSecondary formId={:08X} name='{}'",
			this->_obj ? this->_obj->GetFormID() : 0,
			this->_obj ? this->_obj->GetName() : "");
		return;
	}
	auto pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return;
	}
	if (!weapon) {
		return;
	}
	if (TransformWheelManager::ShouldBlockStaffActivation(weapon, "EquipSecondary")) {
		return;
	}
	auto weaponType = weapon->GetWeaponType();
	bool isTwoHanded = weapon->IsCrossbow() ||
		weapon->IsBow() ||
		weaponType == RE::WEAPON_TYPE::kTwoHandSword ||
		weaponType == RE::WEAPON_TYPE::kTwoHandAxe;

	const auto inv = pc->GetInventory();
	const auto itemData = this->GetItemExtraDataAndCount(const_cast<RE::TESObjectREFR::InventoryItemMap&>(inv));
	const int count = itemData.first;
	RE::ExtraDataList* extraData = itemData.second;
	const int sameFormCount = GetSameFormInventoryCount(inv, weapon);
	const bool bypassInstanceHandResolution = ShouldBypassInstanceHandResolution(inv, weapon, extraData);
	const Utils::Inventory::Hand equippedFormHand =
		bypassInstanceHandResolution ? GetEquippedHandByForm(pc, weapon) : Utils::Inventory::Hand::None;
	if (MainWheelDebug::IsCategoryEnabled(MainWheelDebug::Category::Input)) {
		MainWheelDebug::Log(
			MainWheelDebug::Category::Input,
			"WeaponActivateSecondary: formId={:08X} uniqueID={} count={} sameFormCount={} hasExtraData={} useHandFallback={} formHand={}",
			this->_obj ? this->_obj->GetFormID() : 0,
			this->GetUniqueID(),
			count,
			sameFormCount,
			extraData ? 1 : 0,
			bypassInstanceHandResolution ? 1 : 0,
			static_cast<int>(equippedFormHand));
	}
	if (bypassInstanceHandResolution) {
		if (equippedFormHand == Utils::Inventory::Hand::Left ||
			(isTwoHanded && equippedFormHand == Utils::Inventory::Hand::Right) ||
			equippedFormHand == Utils::Inventory::Hand::Both) {
			if (isTwoHanded) {
				unequipItem(Utils::Slot::GetRightHandSlot());
			} else {
				unequipItem(Utils::Slot::GetLeftHandSlot());
			}
		} else {
			bool wasAlreadyDrawn = pc->AsActorState()->IsWeaponDrawn();
			if (!equipItem(false)) {
				return;
			}
			if (Config::WheelBehavior::AutoDrawOnUse) {
				pc->DrawWeaponMagicHands(true);
			} else if (!wasAlreadyDrawn) {
				pc->DrawWeaponMagicHands(false);
			} else {
				pc->DrawWeaponMagicHands(true);
			}
		}
		return;
	}
	Utils::Inventory::Hand equippedHand = Utils::Inventory::GetWeaponEquippedHand(pc, weapon, this->GetUniqueID(), true);
	if (equippedHand == Utils::Inventory::Hand::Left || (isTwoHanded && equippedHand == Utils::Inventory::Hand::Right) || equippedHand == Utils::Inventory::Hand::Both) {
		if (isTwoHanded) {
			unequipItem(Utils::Slot::GetRightHandSlot());  // note: 2 handed weapons need to be unequipped from the right hand slot to be truly unequipped.
		} else {
			unequipItem(Utils::Slot::GetLeftHandSlot());
		}
	} else {
		// Check if player was already in combat (weapon/magic drawn) BEFORE equipping
		bool wasAlreadyDrawn = pc->AsActorState()->IsWeaponDrawn();
		if (!equipItem(false)) {
			return;
		}
		if (Config::WheelBehavior::AutoDrawOnUse) {
			ActorVirtualCompat::DrawWeaponMagicHands(pc, true);  // draw whatever is in hands
		} else {
			// Only sheathe if the player was NOT already in combat.
			// If they were already drawn, maintain combat stance for smooth weapon swapping.
			if (!wasAlreadyDrawn) {
				pc->DrawWeaponMagicHands(false);
			} else {
				// Force re-draw to maintain combat flow during weapon swap
				pc->DrawWeaponMagicHands(true);
			}
		}
	}
}

void WheelItemWeapon::ActivateItemPrimary()
{
	auto* weapon = this->_obj ? this->_obj->As<RE::TESObjectWEAP>() : nullptr;
	if (TransformWheelManager::ShouldBlockWeaponActivation(weapon)) {
		logger::info("TransformWheels: blocked weapon activation source=EquipPrimary formId={:08X} name='{}'",
			this->_obj ? this->_obj->GetFormID() : 0,
			this->_obj ? this->_obj->GetName() : "");
		return;
	}
	auto pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return;
	}
	if (!weapon) {
		return;
	}
	if (TransformWheelManager::ShouldBlockStaffActivation(weapon, "EquipPrimary")) {
		return;
	}
	const auto inv = pc->GetInventory();
	const auto itemData = this->GetItemExtraDataAndCount(const_cast<RE::TESObjectREFR::InventoryItemMap&>(inv));
	const int count = itemData.first;
	RE::ExtraDataList* extraData = itemData.second;
	const int sameFormCount = GetSameFormInventoryCount(inv, weapon);
	const bool bypassInstanceHandResolution = ShouldBypassInstanceHandResolution(inv, weapon, extraData);
	const Utils::Inventory::Hand equippedFormHand =
		bypassInstanceHandResolution ? GetEquippedHandByForm(pc, weapon) : Utils::Inventory::Hand::None;
	if (MainWheelDebug::IsCategoryEnabled(MainWheelDebug::Category::Input)) {
		MainWheelDebug::Log(
			MainWheelDebug::Category::Input,
			"WeaponActivatePrimary: formId={:08X} uniqueID={} count={} sameFormCount={} hasExtraData={} useHandFallback={} formHand={}",
			this->_obj ? this->_obj->GetFormID() : 0,
			this->GetUniqueID(),
			count,
			sameFormCount,
			extraData ? 1 : 0,
			bypassInstanceHandResolution ? 1 : 0,
			static_cast<int>(equippedFormHand));
	}
	if (bypassInstanceHandResolution) {
		if (equippedFormHand == Utils::Inventory::Hand::Right ||
			equippedFormHand == Utils::Inventory::Hand::Both) {
			unequipItem(Utils::Slot::GetRightHandSlot());
		} else {
			bool wasAlreadyDrawn = pc->AsActorState()->IsWeaponDrawn();
			if (!equipItem(true)) {
				return;
			}
			if (Config::WheelBehavior::AutoDrawOnUse) {
				pc->DrawWeaponMagicHands(true);
			} else if (!wasAlreadyDrawn) {
				ActorVirtualCompat::DrawWeaponMagicHands(pc, false);
			} else {
				ActorVirtualCompat::DrawWeaponMagicHands(pc, true);
			}
		}
		return;
	}
	Utils::Inventory::Hand equippedHand = Utils::Inventory::GetWeaponEquippedHand(pc, weapon, this->GetUniqueID(), true);
	if (equippedHand == Utils::Inventory::Hand::Right || equippedHand == Utils::Inventory::Hand::Both) {
		unequipItem(Utils::Slot::GetRightHandSlot());
	} else {
		// Check if player was already in combat (weapon/magic drawn) BEFORE equipping
		bool wasAlreadyDrawn = pc->AsActorState()->IsWeaponDrawn();
		if (!equipItem(true)) {
			return;
		}
		if (Config::WheelBehavior::AutoDrawOnUse) {
			ActorVirtualCompat::DrawWeaponMagicHands(pc, true);
		} else {
			// Only sheathe if the player was NOT already in combat.
			// If they were already drawn, maintain combat stance for smooth weapon swapping.
			if (!wasAlreadyDrawn) {
				ActorVirtualCompat::DrawWeaponMagicHands(pc, false);
			} else {
				// Force re-draw to maintain combat flow during weapon swap
				ActorVirtualCompat::DrawWeaponMagicHands(pc, true);
			}
		}
	}
}

void WheelItemWeapon::SerializeIntoJsonObj(nlohmann::json& a_json)
{
	a_json["type"] = WheelItemWeapon::ITEM_TYPE_STR;
	a_json["formID"] = this->_obj->GetFormID();
	a_json["uniqueID"] = this->GetUniqueID();
	if (this->GetUniqueID() == 0) {
		a_json["formLevelStack"] = true;
	}
}



bool WheelItemWeapon::equipItem(bool a_toRight)
{
	try {
		auto pc = RE::PlayerCharacter::GetSingleton();
		if (!pc || !pc->Is3DLoaded()) {
			return true;
		}
		RE::TESObjectREFR::InventoryItemMap inv = pc->GetInventory();
		auto itemData = this->GetItemExtraDataAndCount(inv);
		int count = itemData.first;
		RE::ExtraDataList* extraData = itemData.second;
		auto* weapon = this->_obj ? this->_obj->As<RE::TESObjectWEAP>() : nullptr;
		if (!weapon) {
			return true;
		}
		const int sameFormCount = GetSameFormInventoryCount(inv, weapon);
		const bool bypassInstanceHandResolution = ShouldBypassInstanceHandResolution(inv, weapon, extraData);
		if (count <= 0) {  // nothing to equip
			return true;
		}

		const bool allowVanillaSentinelGroupedFallback =
			Wheeler::GetMutableInventoryCompatProfile() == Wheeler::MutableInventoryCompatProfile::Vanilla &&
			this->GetUniqueID() == 0 &&
			!bypassInstanceHandResolution &&
			IsOneHandedWeaponForCompat(weapon) &&
			sameFormCount >= 2 &&
			this->CanUseGroupedEquipFallback(inv, extraData, sameFormCount);
		if (QueueIWSExactSingleWeaponTransfer(
				pc,
				weapon,
				this->GetUniqueID(),
				a_toRight,
				count,
				sameFormCount,
				extraData,
				bypassInstanceHandResolution)) {
			return false;
		}

		bool usedGroupedFallback = false;
		if (!bypassInstanceHandResolution && count < 2 && !allowVanillaSentinelGroupedFallback) {  // we have less than 2, meaning we can't dual-wield
			Utils::Inventory::Hand hand = Utils::Inventory::GetWeaponEquippedHand(pc, this->_obj->As<RE::TESObjectWEAP>(), this->GetUniqueID());
			if ((hand == Utils::Inventory::Hand::Right && !a_toRight) || (hand == Utils::Inventory::Hand::Left && a_toRight)) {  // in opposite hands, simply swap l/r
			auto oppositeSlot = a_toRight ? Utils::Slot::GetLeftHandSlot() : Utils::Slot::GetRightHandSlot();                    // first, clean the slot with item
			RE::ActorEquipManager::GetSingleton()->UnequipObject(pc, this->_obj, nullptr, 1, oppositeSlot, false, true, true);
			}
		} else if (allowVanillaSentinelGroupedFallback || this->CanUseGroupedEquipFallback(inv, extraData, count)) {
			// Safe only when every same-form instance in inventory belongs to the same logical stack group.
			extraData = nullptr;
			usedGroupedFallback = true;
		}
		if (MainWheelDebug::IsCategoryEnabled(MainWheelDebug::Category::Input)) {
			MainWheelDebug::Log(
				MainWheelDebug::Category::Input,
				"WeaponEquip: formId={:08X} uniqueID={} count={} sameFormCount={} hasExtraData={} groupedFallback={} vanillaSentinelFallback={} targetHand={} bypassInstanceHandResolution={}",
				this->_obj ? this->_obj->GetFormID() : 0,
				this->GetUniqueID(),
				count,
				sameFormCount,
				extraData ? 1 : 0,
				usedGroupedFallback ? 1 : 0,
				allowVanillaSentinelGroupedFallback ? 1 : 0,
				a_toRight ? "right" : "left",
				bypassInstanceHandResolution ? 1 : 0);
		}
		if (this->_obj->As<RE::TESObjectWEAP>()->IsCrossbow() || this->_obj->As<RE::TESObjectWEAP>()->IsBow()) {  // clean up both slots
			logger::info("[HandMemoryDiag] BowLikeEquipCleanup formId={:08X} kind={} targetHand={} leftBefore={:08X} rightBefore={:08X} uniqueID={} groupedFallback={} vanillaSentinelFallback={} sameFormCount={}",
				this->_obj ? this->_obj->GetFormID() : 0,
				GetWeaponKindName(this->_obj->As<RE::TESObjectWEAP>()),
				a_toRight ? "RIGHT" : "LEFT",
				pc->GetEquippedObject(true) ? pc->GetEquippedObject(true)->GetFormID() : 0,
				pc->GetEquippedObject(false) ? pc->GetEquippedObject(false)->GetFormID() : 0,
				this->GetUniqueID(),
				usedGroupedFallback ? 1 : 0,
				allowVanillaSentinelGroupedFallback ? 1 : 0,
				sameFormCount);
			Utils::Slot::CleanSlot(pc, Utils::Slot::GetLeftHandSlot());
			Utils::Slot::CleanSlot(pc, Utils::Slot::GetRightHandSlot());
		}
		auto slot = a_toRight ? Utils::Slot::GetRightHandSlot() : Utils::Slot::GetLeftHandSlot();
		RE::ActorEquipManager::GetSingleton()->EquipObject(pc, _obj, extraData, 1, slot);
	} catch (const std::exception& e) {
		logger::error("Error while equipping weapon: {}", e.what());
	}
	return true;
}

void WheelItemWeapon::unequipItem(const RE::BGSEquipSlot* a_slot)
{
	try {
		auto pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			return;
		}
		auto aeMan = RE::ActorEquipManager::GetSingleton();
		if (!aeMan) {
			return;
		}
		aeMan->UnequipObject(pc, this->_obj, nullptr, 1, a_slot);
	} catch (const std::exception& e) {
		logger::error("Error while unequip item: {}", e.what());
	}
}

bool WheelItemWeapon::IsActive(RE::TESObjectREFR::InventoryItemMap& a_inv)
{
	try {
		auto pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			return false;
		}
		auto itemData = this->GetItemExtraDataAndCount(a_inv);
		if (ShouldBypassInstanceHandResolution(a_inv, this->_obj->As<RE::TESObjectWEAP>(), itemData.second)) {
			return GetEquippedHandByExactUniqueID(pc, this->_obj->As<RE::TESObjectWEAP>(), this->GetUniqueID()) != Utils::Inventory::Hand::None;
		}
		if (itemData.first >= 2) {
			return Utils::Inventory::GetWeaponEquippedHand(pc, this->_obj->As<RE::TESObjectWEAP>(), this->GetUniqueID(), true) != Utils::Inventory::Hand::None;
		} else {
			return Utils::Inventory::GetWeaponEquippedHand(pc, this->_obj->As<RE::TESObjectWEAP>(), this->GetUniqueID()) != Utils::Inventory::Hand::None;
		}
	} catch (const std::exception& e) {
		logger::error("Error while checking if item is active: {}", e.what());
		return false;
	}

}
bool WheelItemWeapon::IsAvailable(RE::TESObjectREFR::InventoryItemMap& a_inv)
{
	auto pc = RE::PlayerCharacter::GetSingleton();

	auto itemData = this->GetItemExtraDataAndCount(a_inv);

	return itemData.first > 0;
}
