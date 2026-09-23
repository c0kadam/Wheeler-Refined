#include "OStimBridge.h"

#include "OStimNGThreadAPI.h"
#include "OStimPreviewResolver.h"

#include <RE/F/FunctionArguments.h>
#include <RE/I/IStackCallbackFunctor.h>
#include <RE/S/SkyrimVM.h>
#include <RE/T/TESDataHandler.h>
#include <RE/V/Variable.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_set>

namespace
{
	constexpr RE::FormID kOStimQuestFormID = 0x000801;
	constexpr const char* kOStimPluginName = "OStim.esp";
	constexpr const char* kMainScriptName = "OSexIntegrationMain";
	constexpr const char* kDatabaseScriptName = "ODatabaseScript";
	constexpr const char* kLibraryScriptName = "OLibrary";
	constexpr const char* kMetadataScriptName = "OMetadata";
	constexpr float kVmPumpBudget = 0.001f;
	constexpr int kVmPumpIterations = 12;
	constexpr int kFallbackSceneRange = 3;

	std::atomic<int> s_cachedApiVersion{ 0 };

	template <class TResult>
	struct ResultState
	{
		std::atomic_bool called{ false };
		TResult result{};
	};

	struct CompletionState
	{
		std::atomic_bool called{ false };
	};

	template <class TResult>
	class ResultCallback final : public RE::BSScript::IStackCallbackFunctor
	{
	public:
		explicit ResultCallback(std::shared_ptr<ResultState<TResult>> a_state) :
			_state(std::move(a_state))
		{}

		void operator()(RE::BSScript::Variable a_result) override
		{
			TResult unpacked{};
			try {
				unpacked = a_result.Unpack<TResult>();
			} catch (...) {
				unpacked = TResult{};
			}
			_state->result = std::move(unpacked);
			_state->called.store(true, std::memory_order_release);
		}

		void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

	private:
		std::shared_ptr<ResultState<TResult>> _state;
	};

	template <>
	class ResultCallback<bool> final : public RE::BSScript::IStackCallbackFunctor
	{
	public:
		explicit ResultCallback(std::shared_ptr<ResultState<bool>> a_state) :
			_state(std::move(a_state))
		{}

		void operator()(RE::BSScript::Variable a_result) override
		{
			_state->result = a_result.IsBool() ? a_result.GetBool() : false;
			_state->called.store(true, std::memory_order_release);
		}

		void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

	private:
		std::shared_ptr<ResultState<bool>> _state;
	};

	template <>
	class ResultCallback<int> final : public RE::BSScript::IStackCallbackFunctor
	{
	public:
		explicit ResultCallback(std::shared_ptr<ResultState<int>> a_state) :
			_state(std::move(a_state))
		{}

		void operator()(RE::BSScript::Variable a_result) override
		{
			_state->result = a_result.IsInt() ? a_result.GetSInt() : 0;
			_state->called.store(true, std::memory_order_release);
		}

		void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

	private:
		std::shared_ptr<ResultState<int>> _state;
	};

	template <>
	class ResultCallback<float> final : public RE::BSScript::IStackCallbackFunctor
	{
	public:
		explicit ResultCallback(std::shared_ptr<ResultState<float>> a_state) :
			_state(std::move(a_state))
		{}

		void operator()(RE::BSScript::Variable a_result) override
		{
			_state->result = a_result.IsFloat() ? a_result.GetFloat() : 0.0f;
			_state->called.store(true, std::memory_order_release);
		}

		void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

	private:
		std::shared_ptr<ResultState<float>> _state;
	};

	class CompletionCallback final : public RE::BSScript::IStackCallbackFunctor
	{
	public:
		explicit CompletionCallback(std::shared_ptr<CompletionState> a_state) :
			_state(std::move(a_state))
		{}

		void operator()(RE::BSScript::Variable) override
		{
			_state->called.store(true, std::memory_order_release);
		}

		void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

	private:
		std::shared_ptr<CompletionState> _state;
	};

	void PumpVm(RE::BSScript::IVirtualMachine* a_vm, const std::atomic_bool& a_called)
	{
		if (!a_vm || a_called.load(std::memory_order_acquire)) {
			return;
		}

		for (int i = 0; i < kVmPumpIterations && !a_called.load(std::memory_order_acquire); ++i) {
			a_vm->UpdateTasklets(kVmPumpBudget);
			if (!a_called.load(std::memory_order_acquire)) {
				a_vm->Update(kVmPumpBudget);
			}
		}
	}

	template <class TResult>
	bool TryConsumeResult(const std::shared_ptr<ResultState<TResult>>& a_state, TResult& a_outResult)
	{
		if (!a_state || !a_state->called.load(std::memory_order_acquire)) {
			return false;
		}

		a_outResult = a_state->result;
		return true;
	}

	std::string PrettyClassLabel(std::string_view a_code)
	{
		if (a_code == "Sx") {
			return "Sex";
		}
		if (a_code == "Ho") {
			return "Hub";
		}
		if (a_code == "Po") {
			return "Pullout";
		}
		if (a_code == "Ap") {
			return "Approach";
		}
		if (a_code == "ApU") {
			return "Approach Up";
		}
		if (a_code == "BJ") {
			return "Blowjob";
		}
		if (a_code == "HhBJ") {
			return "Handjob Blowjob";
		}
		if (a_code == "HhPo") {
			return "Handjob Pullout";
		}
		if (a_code == "AgBJ") {
			return "Aggressive Blowjob";
		}
		if (a_code == "Ro") {
			return "Rough";
		}
		return std::string(a_code);
	}

	void BlendPapyrusSceneInfo(OStimSceneInfo& a_info)
	{
		bool active = false;
		if (!OStimBridge::GetAPIBool("AnimationRunning", active) || !active) {
			return;
		}

		OStimBridge::GetAPIBool("IsPlayerInvolved", a_info.playerInvolved);
		OStimBridge::GetAPIBool("IsSceneAggressiveThemed", a_info.aggressive);
		OStimBridge::GetAPIInteger("GetCurrentAnimationSpeed", a_info.currentSpeed);
		OStimBridge::GetAPIInteger("GetCurrentAnimationMaxSpeed", a_info.maxSpeed);
		OStimBridge::GetAPIInteger("GetCurrentAnimationOID", a_info.currentOID);
		OStimBridge::GetAPIString("GetCurrentAnimation", a_info.animationID);
		OStimBridge::GetAPIString("GetCurrentAnimationClass", a_info.animationClass);
		if (a_info.sceneID.empty()) {
			OStimBridge::GetAPIString("GetCurrentAnimationSceneID", a_info.sceneID);
		}
		OStimBridge::GetAPIStringArray("GetAllSceneMetadata", a_info.metadata);

		if (a_info.currentOID > 0) {
			if (a_info.animationName.empty()) {
				OStimBridge::GetFullName(a_info.currentOID, a_info.animationName);
			}
			OStimBridge::GetPositionData(a_info.currentOID, a_info.positionData);
			OStimBridge::GetModule(a_info.currentOID, a_info.sourceModule);
			if (a_info.animationClass.empty()) {
				OStimBridge::GetAnimationClass(a_info.currentOID, a_info.animationClass);
			}
			if (a_info.sceneID.empty()) {
				OStimBridge::GetSceneID(a_info.currentOID, a_info.sceneID);
			}
		}

		if (!a_info.animationClass.empty()) {
			a_info.animationClass = PrettyClassLabel(a_info.animationClass);
		}
	}
}

RE::TESQuest* OStimBridge::ResolveQuest()
{
	auto* dataHandler = RE::TESDataHandler::GetSingleton();
	if (!dataHandler) {
		return nullptr;
	}

	auto* form = dataHandler->LookupForm(kOStimQuestFormID, kOStimPluginName);
	return form ? form->As<RE::TESQuest>() : nullptr;
}

RE::VMHandle OStimBridge::ResolveQuestHandle()
{
	auto* quest = ResolveQuest();
	if (!quest) {
		return 0;
	}

	auto* skyrimVM = RE::SkyrimVM::GetSingleton();
	auto* vm = skyrimVM ? skyrimVM->GetVMRuntimeData().impl.get() : nullptr;
	if (!vm) {
		return 0;
	}

	auto* policy = vm->GetObjectHandlePolicy();
	if (!policy) {
		return 0;
	}

	return policy->GetHandleForObject(RE::FormType::Quest, quest);
}

bool OStimBridge::HasBoundScript(std::string_view a_className)
{
	auto* skyrimVM = RE::SkyrimVM::GetSingleton();
	auto* vm = skyrimVM ? skyrimVM->GetVMRuntimeData().impl.get() : nullptr;
	if (!vm) {
		return false;
	}

	const RE::VMHandle handle = ResolveQuestHandle();
	if (handle == 0) {
		return false;
	}

	RE::BSTSmartPointer<RE::BSScript::Object> object;
	return vm->FindBoundObject(handle, a_className.data(), object) && object != nullptr;
}

bool OStimBridge::IsQuestAvailable()
{
	return ResolveQuest() != nullptr;
}

OStimAvailabilityInfo OStimBridge::GetAvailability()
{
	OStimAvailabilityInfo result{};
	result.hasNativeThreadAPI = OStimNGThreadAPI::IsAvailable();
	if (result.hasNativeThreadAPI) {
		result.available = true;
		result.apiVersion = s_cachedApiVersion.load(std::memory_order_acquire);
		result.reason = "NativeThreadAPI";
		return result;
	}

	if (!ResolveQuest()) {
		result.reason = "QuestMissing";
		return result;
	}
	if (!HasBoundScript(kMainScriptName)) {
		result.reason = "MainScriptMissing";
		return result;
	}

	result.hasDatabase = HasBoundScript(kDatabaseScriptName);
	int apiVersion = 0;
	if (!GetAPIInteger("GetAPIVersion", apiVersion)) {
		apiVersion = s_cachedApiVersion.load(std::memory_order_acquire);
		if (apiVersion <= 0) {
			result.reason = "APICallFailed";
			return result;
		}

		result.available = true;
		result.apiVersion = apiVersion;
		result.reason = result.hasDatabase ? "LegacyPapyrus" : "LegacyPapyrus.DatabaseScriptMissing";
		return result;
	}

	result.available = true;
	result.apiVersion = apiVersion;
	result.reason = result.hasDatabase ? "LegacyPapyrus" : "LegacyPapyrus.DatabaseScriptMissing";
	s_cachedApiVersion.store(apiVersion, std::memory_order_release);
	return result;
}

template <class TResult, class... TArgs>
bool OStimBridge::DispatchQuestMethod(std::string_view a_className, std::string_view a_method, TResult& a_outResult, TArgs&&... a_args)
{
	auto* skyrimVM = RE::SkyrimVM::GetSingleton();
	auto* vm = skyrimVM ? skyrimVM->GetVMRuntimeData().impl.get() : nullptr;
	if (!vm) {
		return false;
	}

	const RE::VMHandle handle = ResolveQuestHandle();
	if (handle == 0) {
		return false;
	}

	std::unique_ptr<RE::BSScript::IFunctionArguments> args(
		RE::MakeFunctionArguments(std::decay_t<TArgs>(std::forward<TArgs>(a_args))...));
	if (!args) {
		return false;
	}

	auto state = std::make_shared<ResultState<TResult>>();
	RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback =
		RE::make_smart<ResultCallback<TResult>>(state);

	const bool dispatched = vm->DispatchMethodCall(
		handle,
		RE::BSFixedString(a_className.data()),
		RE::BSFixedString(a_method.data()),
		args.get(),
		callback);
	if (!dispatched) {
		return false;
	}

	PumpVm(vm, state->called);
	return TryConsumeResult(state, a_outResult);
}

template <class... TArgs>
bool OStimBridge::DispatchQuestMethodVoid(std::string_view a_className, std::string_view a_method, TArgs&&... a_args)
{
	auto* skyrimVM = RE::SkyrimVM::GetSingleton();
	auto* vm = skyrimVM ? skyrimVM->GetVMRuntimeData().impl.get() : nullptr;
	if (!vm) {
		return false;
	}

	const RE::VMHandle handle = ResolveQuestHandle();
	if (handle == 0) {
		return false;
	}

	std::unique_ptr<RE::BSScript::IFunctionArguments> args(
		RE::MakeFunctionArguments(std::decay_t<TArgs>(std::forward<TArgs>(a_args))...));
	if (!args) {
		return false;
	}

	auto state = std::make_shared<CompletionState>();
	RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback =
		RE::make_smart<CompletionCallback>(state);
	const bool dispatched = vm->DispatchMethodCall(
		handle,
		RE::BSFixedString(a_className.data()),
		RE::BSFixedString(a_method.data()),
		args.get(),
		callback);
	if (!dispatched) {
		return false;
	}

	PumpVm(vm, state->called);
	return true;
}

template <class TResult, class... TArgs>
bool OStimBridge::DispatchStaticMethod(std::string_view a_className, std::string_view a_method, TResult& a_outResult, TArgs&&... a_args)
{
	auto* skyrimVM = RE::SkyrimVM::GetSingleton();
	auto* vm = skyrimVM ? skyrimVM->GetVMRuntimeData().impl.get() : nullptr;
	if (!vm) {
		return false;
	}

	std::unique_ptr<RE::BSScript::IFunctionArguments> args(
		RE::MakeFunctionArguments(std::decay_t<TArgs>(std::forward<TArgs>(a_args))...));
	if (!args) {
		return false;
	}

	auto state = std::make_shared<ResultState<TResult>>();
	RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback =
		RE::make_smart<ResultCallback<TResult>>(state);

	const bool dispatched = vm->DispatchStaticCall(
		RE::BSFixedString(a_className.data()),
		RE::BSFixedString(a_method.data()),
		args.get(),
		callback);
	if (!dispatched) {
		return false;
	}

	PumpVm(vm, state->called);
	return TryConsumeResult(state, a_outResult);
}

template <class... TArgs>
bool OStimBridge::DispatchStaticMethodVoid(std::string_view a_className, std::string_view a_method, TArgs&&... a_args)
{
	auto* skyrimVM = RE::SkyrimVM::GetSingleton();
	auto* vm = skyrimVM ? skyrimVM->GetVMRuntimeData().impl.get() : nullptr;
	if (!vm) {
		return false;
	}

	std::unique_ptr<RE::BSScript::IFunctionArguments> args(
		RE::MakeFunctionArguments(std::decay_t<TArgs>(std::forward<TArgs>(a_args))...));
	if (!args) {
		return false;
	}

	auto state = std::make_shared<CompletionState>();
	RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback =
		RE::make_smart<CompletionCallback>(state);
	const bool dispatched = vm->DispatchStaticCall(
		RE::BSFixedString(a_className.data()),
		RE::BSFixedString(a_method.data()),
		args.get(),
		callback);
	if (!dispatched) {
		return false;
	}

	PumpVm(vm, state->called);
	return true;
}

bool OStimBridge::GetAPIInteger(std::string_view a_method, int& a_outValue)
{
	return DispatchQuestMethod(kMainScriptName, a_method, a_outValue);
}

bool OStimBridge::GetAPIBool(std::string_view a_method, bool& a_outValue)
{
	return DispatchQuestMethod(kMainScriptName, a_method, a_outValue);
}

bool OStimBridge::GetAPIString(std::string_view a_method, std::string& a_outValue)
{
	return DispatchQuestMethod(kMainScriptName, a_method, a_outValue);
}

bool OStimBridge::GetAPIStringArray(std::string_view a_method, std::vector<std::string>& a_outValue)
{
	return DispatchQuestMethod(kMainScriptName, a_method, a_outValue);
}

bool OStimBridge::GetAPIActor(std::string_view a_method, RE::Actor*& a_outActor)
{
	return DispatchQuestMethod(kMainScriptName, a_method, a_outActor);
}

bool OStimBridge::GetAPIActors(std::string_view a_method, std::vector<RE::Actor*>& a_outActors)
{
	return DispatchQuestMethod(kMainScriptName, a_method, a_outActors);
}

bool OStimBridge::CallAPIMethod(std::string_view a_method)
{
	return DispatchQuestMethodVoid(kMainScriptName, a_method);
}

bool OStimBridge::CallAPIMethod(std::string_view a_method, std::string_view a_stringArg)
{
	return DispatchQuestMethodVoid(kMainScriptName, a_method, std::string(a_stringArg));
}

bool OStimBridge::CallAPIMethod(std::string_view a_method, int a_intArg)
{
	return DispatchQuestMethodVoid(kMainScriptName, a_method, a_intArg);
}

bool OStimBridge::CallAPIMethod(std::string_view a_method, float a_floatArg)
{
	return DispatchQuestMethodVoid(kMainScriptName, a_method, a_floatArg);
}

bool OStimBridge::CallAPIMethod(std::string_view a_method, bool a_boolArg)
{
	return DispatchQuestMethodVoid(kMainScriptName, a_method, a_boolArg);
}

bool OStimBridge::GetDatabaseOArray(int& a_outHandle)
{
	return DispatchQuestMethod(kDatabaseScriptName, "GetDatabaseOArray", a_outHandle);
}

bool OStimBridge::GetAnimationsWithActorCount(int a_databaseHandle, int a_actorCount, int& a_outHandle)
{
	return DispatchQuestMethod(kDatabaseScriptName, "GetAnimationsWithActorCount", a_outHandle, a_databaseHandle, a_actorCount);
}

bool OStimBridge::GetAnimationsWithAnimationClass(int a_databaseHandle, std::string_view a_animationClass, int& a_outHandle)
{
	return DispatchQuestMethod(
		kDatabaseScriptName,
		"GetAnimationsWithAnimationClass",
		a_outHandle,
		a_databaseHandle,
		std::string(a_animationClass));
}

bool OStimBridge::GetHubAnimations(int a_databaseHandle, bool a_isHub, int& a_outHandle)
{
	return DispatchQuestMethod(kDatabaseScriptName, "GetHubAnimations", a_outHandle, a_databaseHandle, a_isHub);
}

bool OStimBridge::GetTransitoryAnimations(int a_databaseHandle, bool a_isTransitory, int& a_outHandle)
{
	return DispatchQuestMethod(kDatabaseScriptName, "GetTransitoryAnimations", a_outHandle, a_databaseHandle, a_isTransitory);
}

bool OStimBridge::GetLengthOArray(int a_arrayHandle, int& a_outLength)
{
	return DispatchQuestMethod(kDatabaseScriptName, "GetLengthOArray", a_outLength, a_arrayHandle);
}

bool OStimBridge::GetObjectOArray(int a_arrayHandle, int a_index, int& a_outValue)
{
	return DispatchQuestMethod(kDatabaseScriptName, "GetObjectOArray", a_outValue, a_arrayHandle, a_index);
}

bool OStimBridge::GetFullName(int a_animationOID, std::string& a_outValue)
{
	return DispatchQuestMethod(kDatabaseScriptName, "GetFullName", a_outValue, a_animationOID);
}

bool OStimBridge::GetAnimationClass(int a_animationOID, std::string& a_outValue)
{
	return DispatchQuestMethod(kDatabaseScriptName, "GetAnimationClass", a_outValue, a_animationOID);
}

bool OStimBridge::GetPositionData(int a_animationOID, std::string& a_outValue)
{
	return DispatchQuestMethod(kDatabaseScriptName, "GetPositionData", a_outValue, a_animationOID);
}

bool OStimBridge::GetSceneID(int a_animationOID, std::string& a_outValue)
{
	return DispatchQuestMethod(kDatabaseScriptName, "GetSceneID", a_outValue, a_animationOID);
}

bool OStimBridge::GetModule(int a_animationOID, std::string& a_outValue)
{
	return DispatchQuestMethod(kDatabaseScriptName, "GetModule", a_outValue, a_animationOID);
}

bool OStimBridge::GetNumActors(int a_animationOID, int& a_outValue)
{
	return DispatchQuestMethod(kDatabaseScriptName, "GetNumActors", a_outValue, a_animationOID);
}

bool OStimBridge::GetMaxSpeed(int a_animationOID, int& a_outValue)
{
	return DispatchQuestMethod(kDatabaseScriptName, "GetMaxSpeed", a_outValue, a_animationOID);
}

bool OStimBridge::GetMinSpeed(int a_animationOID, int& a_outValue)
{
	return DispatchQuestMethod(kDatabaseScriptName, "GetMinSpeed", a_outValue, a_animationOID);
}

bool OStimBridge::IsAggressive(int a_animationOID, bool& a_outValue)
{
	return DispatchQuestMethod(kDatabaseScriptName, "IsAggressive", a_outValue, a_animationOID);
}

bool OStimBridge::IsHubAnimation(int a_animationOID, bool& a_outValue)
{
	return DispatchQuestMethod(kDatabaseScriptName, "IsHubAnimation", a_outValue, a_animationOID);
}

bool OStimBridge::IsTransitoryAnimation(int a_animationOID, bool& a_outValue)
{
	return DispatchQuestMethod(kDatabaseScriptName, "IsTransitoryAnimation", a_outValue, a_animationOID);
}

bool OStimBridge::GetAllScenes(std::vector<std::string>& a_outScenes)
{
	return DispatchStaticMethod(kLibraryScriptName, "GetAllScenes", a_outScenes);
}

bool OStimBridge::GetScenesInRange(
	std::string_view a_sceneID,
	const std::vector<RE::Actor*>& a_actors,
	int a_distance,
	std::vector<std::string>& a_outScenes)
{
	return DispatchStaticMethod(
		kLibraryScriptName,
		"GetScenesInRange",
		a_outScenes,
		std::string(a_sceneID),
		a_actors,
		a_distance);
}

bool OStimBridge::GetSceneNames(const std::vector<std::string>& a_sceneIDs, std::vector<std::string>& a_outNames)
{
	return DispatchStaticMethod(kMetadataScriptName, "ScenesToNames", a_outNames, a_sceneIDs);
}

std::optional<OStimSceneInfo> OStimBridge::GetCurrentSceneInfo()
{
	OStimAvailabilityInfo availability = GetAvailability();
	if (!availability.available) {
		return std::nullopt;
	}

	if (availability.hasNativeThreadAPI) {
		return std::nullopt;
	}

	auto* quest = ResolveQuest();
	if (!quest || !quest->IsRunning()) {
		return std::nullopt;
	}

	bool active = false;
	if (!GetAPIBool("AnimationRunning", active) || !active) {
		return std::nullopt;
	}

	OStimSceneInfo info{};
	info.active = true;
	info.apiVersion = availability.apiVersion;
	BlendPapyrusSceneInfo(info);

	std::vector<RE::Actor*> actors;
	if (GetAPIActors("GetActors", actors)) {
		info.participants.reserve(actors.size());
		for (auto* actor : actors) {
			if (!actor) {
				continue;
			}

			OStimParticipantInfo participant{};
			participant.formID = actor->GetFormID();
			participant.name = actor->GetName() ? actor->GetName() : "";
			participant.isPlayer = actor->IsPlayerRef();
			info.participants.push_back(std::move(participant));
		}
	}

	info.participantCount = static_cast<int>(info.participants.size());
	if (info.participantCount <= 0) {
		bool solo = false;
		bool threesome = false;
		GetAPIBool("IsSoloScene", solo);
		GetAPIBool("IsThreesome", threesome);
		info.participantCount = threesome ? 3 : (solo ? 1 : 2);
	}

	return info;
}

std::vector<OStimPositionInfo> OStimBridge::GetCandidatePositions(bool a_preferCurrentClassOnly)
{
	std::vector<OStimPositionInfo> positions;

	const auto availability = GetAvailability();
	if (!availability.available) {
		return positions;
	}
	if (availability.hasNativeThreadAPI) {
		return positions;
	}

	const auto scene = GetCurrentSceneInfo();
	if (!scene || scene->participantCount <= 0) {
		return positions;
	}

	if (availability.hasDatabase) {
		int databaseHandle = 0;
		if (!GetDatabaseOArray(databaseHandle) || databaseHandle == 0) {
			return positions;
		}

		int filteredHandle = 0;
		if (!GetAnimationsWithActorCount(databaseHandle, scene->participantCount, filteredHandle) || filteredHandle == 0) {
			return positions;
		}

		int tempHandle = 0;
		if (!GetHubAnimations(filteredHandle, false, tempHandle) || tempHandle == 0) {
			return positions;
		}
		filteredHandle = tempHandle;

		if (!GetTransitoryAnimations(filteredHandle, false, tempHandle) || tempHandle == 0) {
			return positions;
		}
		filteredHandle = tempHandle;

		if (a_preferCurrentClassOnly && !scene->animationClass.empty()) {
			int classHandle = 0;
			if (GetAnimationsWithAnimationClass(filteredHandle, scene->animationClass, classHandle) && classHandle != 0) {
				int classLength = 0;
				if (GetLengthOArray(classHandle, classLength) && classLength > 0) {
					filteredHandle = classHandle;
				}
			}
		}

		int length = 0;
		if (!GetLengthOArray(filteredHandle, length) || length <= 0) {
			return positions;
		}

		std::unordered_set<std::string> seenSceneIDs;
		positions.reserve(static_cast<std::size_t>(length));

		for (int i = 0; i < length; ++i) {
			int animationOID = 0;
			if (!GetObjectOArray(filteredHandle, i, animationOID) || animationOID == 0) {
				continue;
			}

			std::string sceneID;
			if (!GetSceneID(animationOID, sceneID) || sceneID.empty()) {
				continue;
			}
			if (!seenSceneIDs.insert(sceneID).second) {
				continue;
			}

			OStimPositionInfo info{};
			info.id = sceneID;
			info.sourceSceneID = scene->sceneID;
			GetFullName(animationOID, info.displayName);
			if (info.displayName.empty()) {
				info.displayName = sceneID;
			}

			std::string animationClass;
			GetAnimationClass(animationOID, animationClass);
			info.category = PrettyClassLabel(animationClass);
			GetModule(animationOID, info.subcategory);
			info.destinationID = info.id;
			info.requiresActiveScene = true;
			info.isValidNow = true;
			OStimPreviewResolver::Apply(info);

			positions.push_back(std::move(info));
		}

		std::stable_sort(positions.begin(), positions.end(), [&](const auto& lhs, const auto& rhs) {
			if (lhs.id == scene->sceneID) {
				return true;
			}
			if (rhs.id == scene->sceneID) {
				return false;
			}
			return lhs.displayName < rhs.displayName;
		});

		return positions;
	}

	std::vector<RE::Actor*> actors;
	if (!GetAPIActors("GetActors", actors) || actors.empty()) {
		return positions;
	}

	std::vector<std::string> sceneIDs;
	if (!GetScenesInRange(scene->sceneID, actors, kFallbackSceneRange, sceneIDs) || sceneIDs.empty()) {
		return positions;
	}

	std::unordered_set<std::string> seenSceneIDs;
	std::vector<std::string> uniqueSceneIDs;
	uniqueSceneIDs.reserve(sceneIDs.size() + 1);

	if (!scene->sceneID.empty()) {
		seenSceneIDs.insert(scene->sceneID);
		uniqueSceneIDs.push_back(scene->sceneID);
	}

	for (auto& sceneID : sceneIDs) {
		if (sceneID.empty() || !seenSceneIDs.insert(sceneID).second) {
			continue;
		}
		uniqueSceneIDs.push_back(std::move(sceneID));
	}

	if (uniqueSceneIDs.empty()) {
		return positions;
	}

	std::vector<std::string> sceneNames;
	GetSceneNames(uniqueSceneIDs, sceneNames);

	positions.reserve(uniqueSceneIDs.size());
	for (std::size_t i = 0; i < uniqueSceneIDs.size(); ++i) {
		OStimPositionInfo info{};
		info.id = uniqueSceneIDs[i];
		info.sourceSceneID = scene->sceneID;
		if (i < sceneNames.size() && !sceneNames[i].empty()) {
			info.displayName = sceneNames[i];
		}
		if (info.displayName.empty()) {
			info.displayName = info.id;
		}
		info.category = !scene->animationClass.empty() ?
			PrettyClassLabel(scene->animationClass) :
			"Scene";
		info.subcategory = scene->sourceModule;
		info.destinationID = info.id;
		info.requiresActiveScene = true;
		info.isValidNow = true;
		OStimPreviewResolver::Apply(info);
		positions.push_back(std::move(info));
	}

	return positions;
}
