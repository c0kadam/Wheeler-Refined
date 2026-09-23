#include "bin/Rendering/TextureManager.h"
#include "bin/Rendering/Drawer.h"
#include "bin/Wheeler/Wheeler.h"
#include "WheelItemBook.h"

WheelItemBook::WheelItemBook(RE::TESObjectBOOK* a_book)
{
	if (!a_book) {
		logger::warn("WheelItemBook: constructed with null book");
		return;
	}
	this->_book = a_book;
	this->_formID = a_book->GetFormID();
	// Use default icon type or try to find one. Books usually use generic icon if no specific texture found.
	this->_texture = Texture::GetIconImage(Texture::icon_image_type::icon_default, a_book);
}

RE::TESObjectBOOK* WheelItemBook::ResolveBook()
{
	if (_formID == 0) {
		return nullptr;
	}
	
	// Always resolve from the form table to avoid holding on to stale pointers
	RE::TESObjectBOOK* book = RE::TESForm::LookupByID<RE::TESObjectBOOK>(_formID);
	if (!book) {
		_book = nullptr;
		return nullptr;
	}
	
	_book = book;
	return _book;
}

bool WheelItemBook::IsInPlayerInventory() const
{
	if (_formID == 0) {
		return false;
	}
	
	RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return false;
	}
	
	// Lookup by FormID to avoid stale pointer
	RE::TESObjectBOOK* book = RE::TESForm::LookupByID<RE::TESObjectBOOK>(_formID);
	if (!book) {
		return false;
	}
	
	auto counts = pc->GetInventoryCounts();
	auto it = counts.find(book);
	return it != counts.end() && it->second > 0;
}

void WheelItemBook::DrawSlot(ImVec2 a_center, bool a_hovered, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs)
{
	RE::TESObjectBOOK* book = ResolveBook();
	if (!book) {
		return;
	}
	this->drawSlotText(a_center, book->GetName(), a_drawArgs);
	this->drawSlotTexture(a_center, a_drawArgs);
}

void WheelItemBook::DrawHighlight(ImVec2 a_center, RE::TESObjectREFR::InventoryItemMap& a_imap, DrawArgs a_drawArgs)
{
	RE::TESObjectBOOK* book = ResolveBook();
	if (!book) {
		return;
	}
	this->drawHighlightText(a_center, book->GetName(), a_drawArgs);
	this->drawHighlightTexture(a_center, a_drawArgs);
}

bool WheelItemBook::IsActive(RE::TESObjectREFR::InventoryItemMap& a_inv)
{
	return false;
}

bool WheelItemBook::IsAvailable(RE::TESObjectREFR::InventoryItemMap& a_inv)
{
	RE::TESObjectBOOK* book = ResolveBook();
	if (!book) {
		return false;
	}
	return a_inv.contains(book) && a_inv[book].first > 0;
}

void WheelItemBook::ActivateItemSecondary()
{
	this->readBook();
}

void WheelItemBook::ActivateItemPrimary()
{
	this->readBook();
}

void WheelItemBook::SerializeIntoJsonObj(nlohmann::json& a_json)
{
	a_json["type"] = WheelItemBook::ITEM_TYPE_STR;
	a_json["formID"] = this->_formID;
}

void WheelItemBook::readBook()
{
	if (_formID == 0) {
		return;
	}
	// Queue the book read action to be performed after the wheel closes
	// The guard for inventory presence is checked in Wheeler::ProcessPendingActions
	Wheeler::QueueBookRead(_formID);
}

const char* WheelItemBook::GetItemName() const
{
	RE::TESObjectBOOK* book = RE::TESForm::LookupByID<RE::TESObjectBOOK>(_formID);
	return book ? book->GetName() : "(deleted)";
}


