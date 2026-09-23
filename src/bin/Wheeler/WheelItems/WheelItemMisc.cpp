#include "bin/Rendering/TextureManager.h"
#include "bin/Rendering/Drawer.h"
#include "bin/Wheeler/Wheeler.h"
#include "bin/Wheeler/TransformWheelManager.h"
#include "bin/Utilities/EquipEventDispatcher.h"
#include "bin/Utilities/ItemCapabilities.h"
#include "WheelItemMisc.h"
#include "bin/Utilities/InventorySnapshotCache.h"

namespace YpsItems
{
	// Check if a MISC item is a Yps Immersive Hair item by keyword
	// Yps items use keywords: ypsMiscItem, ypsItem
	bool IsYpsItem(RE::TESObjectMISC* a_item)
	{
		if (!a_item) {
			return false;
		}
		
		auto* keywordForm = a_item->As<RE::BGSKeywordForm>();
		if (!keywordForm) {
			return false;
		}
		
		for (uint32_t i = 0; i < keywordForm->numKeywords; i++) {
			RE::BGSKeyword* kw = keywordForm->keywords[i];
			if (!kw) continue;
			
			const char* editorID = kw->GetFormEditorID();
			if (!editorID) continue;
			
			std::string kwStr(editorID);
			std::transform(kwStr.begin(), kwStr.end(), kwStr.begin(), ::tolower);
			if (kwStr.find("yps") == 0) {
				return true;
			}
		}
		
		return false;
	}
	
	// Queue Yps item activation to run after wheel closes
	void QueueYpsItemActivation(RE::FormID a_itemFormID, std::uint16_t a_uniqueID)
	{
		if (a_itemFormID == 0) {
			return;
		}
		
		// Queue to Wheeler's pending misc item system (which runs after wheel closes)
		Wheeler::QueueMiscItemUse(a_itemFormID, a_uniqueID);
	}
}

namespace ShovelItems
{
	// Check if a MISC item is a ShovelBody shovel item by keyword or name
	// ShovelBody items typically have keywords like "ShovelBodyKeyword" or names containing "Shovel"
	bool IsShovelItem(RE::TESObjectMISC* a_item)
	{
		if (!a_item) {
			return false;
		}
		
		// Check keywords first
		auto* keywordForm = a_item->As<RE::BGSKeywordForm>();
		if (keywordForm) {
			for (uint32_t i = 0; i < keywordForm->numKeywords; i++) {
				RE::BGSKeyword* kw = keywordForm->keywords[i];
				if (!kw) continue;
				
				const char* editorID = kw->GetFormEditorID();
				if (!editorID) continue;
				
				std::string kwStr(editorID);
				std::transform(kwStr.begin(), kwStr.end(), kwStr.begin(), ::tolower);
				if (kwStr.find("shovel") != std::string::npos) {
					return true;
				}
			}
		}
		
		// Fallback: check item name
		const char* name = a_item->GetName();
		if (name) {
			std::string nameStr(name);
			std::transform(nameStr.begin(), nameStr.end(), nameStr.begin(), ::tolower);
			if (nameStr.find("shovel") != std::string::npos) {
				return true;
			}
		}
		
		return false;
	}
	
	// Queue shovel item activation to run after wheel closes
	void QueueShovelItemActivation(RE::FormID a_itemFormID, std::uint16_t a_uniqueID)
	{
		if (a_itemFormID == 0) {
			return;
		}
		
		// Queue to Wheeler's pending misc item system (which runs after wheel closes)
		Wheeler::QueueMiscItemUse(a_itemFormID, a_uniqueID);
		logger::info("[ShovelItems] Queued shovel activation: formID={:08X}", a_itemFormID);
	}
}

namespace SGTInstruments
{
	// Skyrim's Got Talent instrument FormIDs (relative to SkyrimsGotTalent-Bards.esp)
	// These are the base game instrument FormIDs that SGT overrides with scripts
	constexpr RE::FormID kFlute = 0x000DABA7;      // Skyrim.esm Flute
	constexpr RE::FormID kFlute01 = 0x00105177;    // Skyrim.esm Flute01
	constexpr RE::FormID kDancersFlute = 0x00105109; // Skyrim.esm DancersFlute
	constexpr RE::FormID kDrum = 0x000DABA9;       // Skyrim.esm Drum
	constexpr RE::FormID kLute = 0x000DABAB;       // Skyrim.esm Lute
	
	// SGT dummy instruments (relative FormIDs in SkyrimsGotTalent-Bards.esp)
	constexpr RE::FormID kDummyFlute = 0x02B141;
	constexpr RE::FormID kDummyDrum = 0x02B142;
	constexpr RE::FormID kDummyLute = 0x02B143;
	
	// SGT spell FormIDs (relative to SkyrimsGotTalent-Bards.esp)
	constexpr RE::FormID kFluteSpell = 0x0022EE;   // _Talent_FluteSpell
	constexpr RE::FormID kDrumSpell = 0x0022ED;    // _Talent_DrumSpell
	constexpr RE::FormID kLuteSpell = 0x0022F0;    // _Talent_LuteSpell
	
	// Plugin name
	constexpr const char* kPluginName = "SkyrimsGotTalent-Bards.esp";
	
	enum class InstrumentType
	{
		None,
		Flute,
		Drum,
		Lute
	};
	
	// Determine instrument type from misc item
	InstrumentType GetInstrumentType(RE::TESObjectMISC* a_item)
	{
		if (!a_item) {
			return InstrumentType::None;
		}
		
		// Get the base FormID (without load order prefix)
		RE::FormID formID = a_item->GetFormID();
		RE::FormID baseFormID = formID & 0x00FFFFFF;
		
		// Check vanilla instrument FormIDs (these are in Skyrim.esm, so full FormID check)
		// Skyrim.esm is always load order 0, so FormID == baseFormID for these
		if (formID == kFlute || formID == kFlute01 || formID == kDancersFlute) {
			return InstrumentType::Flute;
		}
		if (formID == kDrum) {
			return InstrumentType::Drum;
		}
		if (formID == kLute) {
			return InstrumentType::Lute;
		}
		
		// Check SGT dummy instruments (relative FormIDs)
		if (baseFormID == kDummyFlute) {
			return InstrumentType::Flute;
		}
		if (baseFormID == kDummyDrum) {
			return InstrumentType::Drum;
		}
		if (baseFormID == kDummyLute) {
			return InstrumentType::Lute;
		}
		
		// Also check by name as fallback
		const char* name = a_item->GetName();
		if (name) {
			std::string nameStr(name);
			// Convert to lowercase for comparison
			std::transform(nameStr.begin(), nameStr.end(), nameStr.begin(), ::tolower);
			if (nameStr.find("flute") != std::string::npos) {
				return InstrumentType::Flute;
			}
			if (nameStr.find("drum") != std::string::npos) {
				return InstrumentType::Drum;
			}
			if (nameStr.find("lute") != std::string::npos) {
				return InstrumentType::Lute;
			}
		}
		
		return InstrumentType::None;
	}
	
	// Get the SGT spell for an instrument type
	RE::SpellItem* GetInstrumentSpell(InstrumentType a_type)
	{
		if (a_type == InstrumentType::None) {
			return nullptr;
		}
		
		// Get the data handler to look up forms
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			logger::warn("SGTInstruments: TESDataHandler not available");
			return nullptr;
		}
		
		// Determine which spell to look up
		RE::FormID spellFormID = 0;
		const char* spellName = nullptr;
		switch (a_type) {
		case InstrumentType::Flute:
			spellFormID = kFluteSpell;
			spellName = "_Talent_FluteSpell";
			break;
		case InstrumentType::Drum:
			spellFormID = kDrumSpell;
			spellName = "_Talent_DrumSpell";
			break;
		case InstrumentType::Lute:
			spellFormID = kLuteSpell;
			spellName = "_Talent_LuteSpell";
			break;
		default:
			return nullptr;
		}
		
		// Look up the spell by plugin + relative FormID
		RE::TESForm* form = dataHandler->LookupForm(spellFormID, kPluginName);
		if (!form) {
			logger::warn("SGTInstruments: Failed to find spell {} (0x{:X}) in {}", 
				spellName, spellFormID, kPluginName);
			return nullptr;
		}
		
		RE::SpellItem* spell = form->As<RE::SpellItem>();
		if (!spell) {
			logger::warn("SGTInstruments: Form {} is not a SpellItem", spellName);
			return nullptr;
		}
		
		logger::info("SGTInstruments: Found spell {} (FormID: {:08X})", spellName, spell->GetFormID());
		return spell;
	}
	
	// Queue the instrument spell to be cast after the wheel closes
	// This allows animations to play properly since the game is in a normal state
bool QueueInstrumentSpell(InstrumentType a_type)
	{
		RE::SpellItem* spell = GetInstrumentSpell(a_type);
		if (!spell) {
			return false;
		}
		
		// Queue the spell to be cast after the wheel closes
		logger::info("SGTInstruments: Queueing {} for cast after wheel closes", spell->GetName());
		Wheeler::QueueSGTInstrumentSpell(spell->GetFormID());
		return true;
	}
}

namespace
{
	struct MiscActivationContext
	{
		int count = 0;
		bool hasExtraList = false;
		std::uint16_t uniqueID = 0;
	};

	struct MiscInventorySelection
	{
		MiscActivationContext values{};
		RE::ExtraDataList* extraList = nullptr;
	};

	MiscInventorySelection ResolveMiscInventorySelection(
		const RE::TESObjectREFR::InventoryItemMap& inv,
		RE::TESObjectMISC* item)
	{
		MiscInventorySelection selection{};
		if (!item) {
			return selection;
		}

		RE::InventoryEntryData* entry = nullptr;
		auto it = inv.find(item);
		if (it != inv.end()) {
			selection.values.count = it->second.first;
			entry = it->second.second.get();
		} else {
			const RE::FormID formID = item->GetFormID();
			for (auto& [boundObj, data] : inv) {
				if (boundObj && boundObj->GetFormID() == formID) {
					selection.values.count = data.first;
					entry = data.second.get();
					break;
				}
			}
		}

		if (!entry || !entry->extraLists) {
			return selection;
		}

		for (auto* extraList : *entry->extraLists) {
			if (!extraList) {
				continue;
			}
			selection.values.hasExtraList = true;
			if (!selection.extraList) {
				selection.extraList = extraList;
			}
			if (auto* uniqueData = extraList->GetByType<RE::ExtraUniqueID>()) {
				if (uniqueData->uniqueID != 0) {
					selection.values.uniqueID = uniqueData->uniqueID;
					selection.extraList = extraList;
					break;
				}
			}
		}

		return selection;
	}

	MiscActivationContext ResolveMiscActivationContext(RE::PlayerCharacter* pc, RE::TESObjectMISC* item)
	{
		if (!pc || !item) {
			return {};
		}
		const auto inventory = pc->GetInventory();
		return ResolveMiscInventorySelection(inventory, item).values;
	}
}

WheelItemMisc::WheelItemMisc(RE::TESObjectMISC* a_miscItem)
{
	this->_miscItem = a_miscItem;
	this->_texture = Texture::GetIconImage(Texture::icon_image_type::icon_default, a_miscItem);
}

void WheelItemMisc::DrawSlot(ImVec2 a_center, bool a_hovered, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs)
{
	if (TransformWheelManager::ShouldDimMiscActivation(_miscItem)) {
		a_drawArgs.alphaMult *= 0.35f;
	}
	this->drawSlotText(a_center, this->_miscItem->GetName(), a_drawArgs);
	this->drawSlotTexture(a_center, a_drawArgs);
}

void WheelItemMisc::DrawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs)
{
	if (TransformWheelManager::ShouldDimMiscActivation(_miscItem)) {
		a_drawArgs.alphaMult *= 0.35f;
	}
	this->drawHighlightText(a_center, this->_miscItem->GetName(), a_drawArgs);
	this->drawHighlightTexture(a_center, a_drawArgs);
}

bool WheelItemMisc::IsActive(RE::TESObjectREFR::InventoryItemMap& a_inv)
{
	return false;
}

bool WheelItemMisc::IsAvailable(RE::TESObjectREFR::InventoryItemMap& a_inv)
{
	return a_inv.contains(this->_miscItem) && a_inv[this->_miscItem].first > 0;
}

bool WheelItemMisc::IsInPlayerInventory() const
{
	RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
	return ItemCapabilities::IsInInventory(pc, _miscItem, nullptr);
}

void WheelItemMisc::ActivateItemSecondary()
{
	this->useItem();
}

void WheelItemMisc::ActivateItemPrimary()
{
	this->useItem();
}

void WheelItemMisc::SerializeIntoJsonObj(nlohmann::json& a_json)
{
	a_json["type"] = WheelItemMisc::ITEM_TYPE_STR;
	a_json["formID"] = this->_miscItem->GetFormID();
}

void WheelItemMisc::useItem()
{
	if (!this->_miscItem) {
		return;
	}
	
	RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return;
	}
	if (TransformWheelManager::ShouldBlockMiscActivation(this->_miscItem, "MiscUse")) {
		logger::info("TransformWheels: blocked misc activation source=MiscUse formId={:08X} name='{}'",
			this->_miscItem ? this->_miscItem->GetFormID() : 0,
			this->_miscItem ? this->_miscItem->GetName() : "");
		return;
	}
	
	const RE::FormID formID = this->_miscItem->GetFormID();
	const char* itemName = this->_miscItem->GetName();
	const auto ctx = ResolveMiscActivationContext(pc, this->_miscItem);
	const bool hasVMAD = this->_miscItem->HasVMAD();
	
	// Check if this is an SGT instrument - if so, cast the corresponding spell
	SGTInstruments::InstrumentType instrumentType = SGTInstruments::GetInstrumentType(this->_miscItem);
	if (instrumentType != SGTInstruments::InstrumentType::None) {
		if (SGTInstruments::QueueInstrumentSpell(instrumentType)) {
			if (Config::Debug::LogActionPolicy) {
				logger::info("[MiscItem] Activate: '{}' formID={:08X} formType={} count={} extraList={} uniqueID={} path=deferred_sgt_spell",
					itemName ? itemName : "(null)", formID,
					static_cast<int>(this->_miscItem->GetFormType()),
					ctx.count, ctx.hasExtraList ? 1 : 0, ctx.uniqueID);
			}
			return;
		}
	}
	
	// Check if this is a Yps Immersive Hair item - needs deferred activation
	// Yps items require the wheel to be fully closed before activation to show their menus
	if (YpsItems::IsYpsItem(this->_miscItem)) {
		if (Config::Debug::LogActionPolicy) {
			logger::info("[MiscItem] Activate: '{}' formID={:08X} formType={} count={} extraList={} uniqueID={} path=deferred_yps",
				itemName ? itemName : "(null)", formID,
				static_cast<int>(this->_miscItem->GetFormType()),
				ctx.count, ctx.hasExtraList ? 1 : 0, ctx.uniqueID);
		}
		YpsItems::QueueYpsItemActivation(formID, ctx.uniqueID);
		return;
	}
	
	// Check if this is a ShovelBody shovel item - needs deferred activation
	// Shovel items require the wheel to be fully closed before activation
	if (ShovelItems::IsShovelItem(this->_miscItem)) {
		if (Config::Debug::LogActionPolicy) {
			logger::info("[MiscItem] Activate: '{}' formID={:08X} formType={} count={} extraList={} uniqueID={} path=deferred_shovel",
				itemName ? itemName : "(null)", formID,
				static_cast<int>(this->_miscItem->GetFormType()),
				ctx.count, ctx.hasExtraList ? 1 : 0, ctx.uniqueID);
		}
		ShovelItems::QueueShovelItemActivation(formID, ctx.uniqueID);
		return;
	}
	
	// Use deferred activation for script-backed misc items
	if (hasVMAD) {
		if (Config::Debug::LogActionPolicy) {
			logger::info("[MiscItem] Activate: '{}' formID={:08X} formType={} count={} extraList={} uniqueID={} path=deferred_script",
				itemName ? itemName : "(null)", formID,
				static_cast<int>(this->_miscItem->GetFormType()),
				ctx.count, ctx.hasExtraList ? 1 : 0, ctx.uniqueID);
		}
		Wheeler::QueueMiscItemUse(formID, ctx.uniqueID);
		return;
	}
	
	// Immediate activation for non-scripted misc items
	RE::ActorEquipManager* aeMan = RE::ActorEquipManager::GetSingleton();
	if (!aeMan) {
		return;
	}
	MiscActivationContext immediateValues{};
	RE::ExtraDataList* selectedExtraList = nullptr;
	{
		auto inventory = pc->GetInventory();
		MiscInventorySelection selection = ResolveMiscInventorySelection(inventory, this->_miscItem);
		immediateValues = selection.values;
		selectedExtraList = selection.extraList;
		selection.extraList = nullptr;
		inventory.clear();
	}
	if (Config::Debug::LogActionPolicy) {
		logger::info("[MiscItem] Activate: '{}' formID={:08X} formType={} count={} extraList={} uniqueID={} path=immediate_equip",
			itemName ? itemName : "(null)", formID,
			static_cast<int>(this->_miscItem->GetFormType()),
			immediateValues.count, immediateValues.hasExtraList ? 1 : 0, immediateValues.uniqueID);
	}
	if (immediateValues.count <= 0) {
		if (Config::Debug::LogActionPolicy) {
			logger::info("[MiscItem] Immediate equip skipped: not in inventory, formID={:08X}", formID);
		}
		selectedExtraList = nullptr;
		return;
	}
	InventorySnapshotCache::EquipObject(aeMan, pc, this->_miscItem, selectedExtraList);
	selectedExtraList = nullptr;
	if (Config::Debug::LogActionPolicy) {
		logger::info("[MiscItem] Immediate EquipObject dispatched: '{}' formID={:08X}", itemName ? itemName : "(null)", formID);
	}
}
