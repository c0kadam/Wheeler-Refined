#include "nlohmann/json.hpp"

#include "SerializationEntry.h"
#include "bin/Wheeler/Wheeler.h"
#include "bin/API/WheelerAPI.h"

namespace Serial
{
	template <typename T>
	bool Write(SKSE::SerializationInterface* a_interface, const T& data)
	{
		return a_interface->WriteRecordData(&data, sizeof(T));
	}

	template <>
	bool Write(SKSE::SerializationInterface* a_interface, const std::string& data)
	{
		const std::size_t size = data.length();
		return a_interface->WriteRecordData(size) && a_interface->WriteRecordData(data.data(), static_cast<std::uint32_t>(size));
	}

	template <typename T>
	bool Read(SKSE::SerializationInterface* a_interface, T& result)
	{
		return a_interface->ReadRecordData(&result, sizeof(T));
	}

	template <>
	bool Read(SKSE::SerializationInterface* a_interface, std::string& result)
	{
		std::size_t size = 0;
		if (!a_interface->ReadRecordData(size)) {
			return false;
		}
		// Bounds check: reject absurdly large sizes that indicate corrupted data
		// Max reasonable size for Wheeler JSON is ~1MB (generous upper bound)
		constexpr std::size_t MAX_REASONABLE_SIZE = 1024 * 1024;
		if (size > MAX_REASONABLE_SIZE) {
			INFO("Read: string size {} exceeds max {}, likely corrupted data", size, MAX_REASONABLE_SIZE);
			return false;
		}
		if (size > 0) {
			result.resize(size);
			if (!a_interface->ReadRecordData(result.data(), static_cast<std::uint32_t>(size))) {
				return false;
			}
		} else {
			result = "";
		}
		return true;
	}
}
void SerializationEntry::BindSerializationCallbacks(const SKSE::SerializationInterface* a_in)
{
	a_in->SetLoadCallback(SerializationEntry::Load);
	a_in->SetSaveCallback(SerializationEntry::Save);
	a_in->SetRevertCallback(SerializationEntry::Revert);
}
void SerializationEntry::Save(SKSE::SerializationInterface* a_intfc)
{
	INFO("Serializing wheel into save...");
	if (!a_intfc->OpenRecord(WHEELER_JSON_STRING_TYPE, SERIALIZER_VERSION)) {
		INFO("Failed to open record");
		return;
	}
	nlohmann::json j_wheeler;
	Wheeler::SerializeIntoJsonObj(j_wheeler);
	const auto wheelCount = j_wheeler.contains("wheels") ? j_wheeler["wheels"].size() : 0;
	const auto activeIdx = j_wheeler.value("activewheel", -1);
	std::string writeBuffer = j_wheeler.dump();
	INFO("Serializing record: wheels={}, activeWheel={}", wheelCount, activeIdx);
	
	Serial::Write(a_intfc, writeBuffer);

}

void SerializationEntry::Load(SKSE::SerializationInterface* a_intfc)
{
	std::uint32_t type, version, length;
	if (!a_intfc->GetNextRecordInfo(type, version, length)) {
		INFO("Failed to obtain record, abort loading");
		return;
	}
	if (type != WHEELER_JSON_STRING_TYPE) {
		INFO("Load: wrong type, abort loading");
		return;
	}
	if (version != SERIALIZER_VERSION) {
		INFO("Load: wrong version (got {}, expected {}), clearing wheel state", version, SERIALIZER_VERSION);
		Wheeler::Clear();
		return;
	}
	std::string readBuffer;

	if (!Serial::Read(a_intfc, readBuffer)) {
		INFO("Load: failed to read record data, clearing wheel state");
		Wheeler::Clear();
		return;
	}
	
	INFO("Deserializing record (length={})", readBuffer.size());
	try {
		nlohmann::json j_wheeler = nlohmann::json::parse(readBuffer);
		const auto wheelCount = j_wheeler.contains("wheels") ? j_wheeler["wheels"].size() : 0;
		const auto activeIdx = j_wheeler.value("activewheel", -1);
		INFO("Deserializing parsed data: wheels={}, activeWheel={}", wheelCount, activeIdx);
		WheelerAPI::ClearManagedWheels();  // Clear managed wheel tracking before replacing wheel list
		Wheeler::Clear();
		Wheeler::SerializeFromJsonObj(j_wheeler, a_intfc);
		INFO("Deserialization complete");
	} catch (const nlohmann::json::exception& e) {
		INFO("Deserialize failed (JSON error): {}, clearing wheel state", e.what());
		Wheeler::Clear();
	} catch (const std::exception& e) {
		INFO("Deserialize failed: {}, clearing wheel state", e.what());
		Wheeler::Clear();
	} catch (...) {
		INFO("Deserialize failed (unknown error), clearing wheel state");
		Wheeler::Clear();
	}
}

void SerializationEntry::Revert(SKSE::SerializationInterface* a_intfc)
{
	Wheeler::Clear();
}

// serialization example:
/**

"activeWheel" : 0,
"wheels" :
[
	{
		"entries":
		[
			{
				"selectedItem" ： 3
				"items:
				[
					{
						"type": "WheelItemWeapon",
						"formID": 1234(uint),
						"uniqueID": 5(uint)							
					},
					{
						"type": "WheelItemArmor",
						"formID": 1324(uint),
						"uniqueID": 6(uint)
					},
					{
						"type": "WheelItemSpell",
						"formID": 1234(uint),
					}
				]
			},
			{
				"items:
				[
				]
			},
		]
	},
	{
		"entries":
		[
			{
				"items:
				[
				]
			},
		]
	},
]
	

*/
