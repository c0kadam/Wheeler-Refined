#pragma once

#include "OStimTypes.h"

#include <optional>
#include <string_view>
#include <vector>

class OStimBridge
{
public:
	static bool IsQuestAvailable();
	static OStimAvailabilityInfo GetAvailability();

	static bool GetAPIInteger(std::string_view a_method, int& a_outValue);
	static bool GetAPIBool(std::string_view a_method, bool& a_outValue);
	static bool GetAPIString(std::string_view a_method, std::string& a_outValue);
	static bool GetAPIStringArray(std::string_view a_method, std::vector<std::string>& a_outValue);
	static bool GetAPIActor(std::string_view a_method, RE::Actor*& a_outActor);
	static bool GetAPIActors(std::string_view a_method, std::vector<RE::Actor*>& a_outActors);

	static bool CallAPIMethod(std::string_view a_method);
	static bool CallAPIMethod(std::string_view a_method, std::string_view a_stringArg);
	static bool CallAPIMethod(std::string_view a_method, int a_intArg);
	static bool CallAPIMethod(std::string_view a_method, float a_floatArg);
	static bool CallAPIMethod(std::string_view a_method, bool a_boolArg);

	static bool GetDatabaseOArray(int& a_outHandle);
	static bool GetAnimationsWithActorCount(int a_databaseHandle, int a_actorCount, int& a_outHandle);
	static bool GetAnimationsWithAnimationClass(int a_databaseHandle, std::string_view a_animationClass, int& a_outHandle);
	static bool GetHubAnimations(int a_databaseHandle, bool a_isHub, int& a_outHandle);
	static bool GetTransitoryAnimations(int a_databaseHandle, bool a_isTransitory, int& a_outHandle);
	static bool GetLengthOArray(int a_arrayHandle, int& a_outLength);
	static bool GetObjectOArray(int a_arrayHandle, int a_index, int& a_outValue);
	static bool GetFullName(int a_animationOID, std::string& a_outValue);
	static bool GetAnimationClass(int a_animationOID, std::string& a_outValue);
	static bool GetPositionData(int a_animationOID, std::string& a_outValue);
	static bool GetSceneID(int a_animationOID, std::string& a_outValue);
	static bool GetModule(int a_animationOID, std::string& a_outValue);
	static bool GetNumActors(int a_animationOID, int& a_outValue);
	static bool GetMaxSpeed(int a_animationOID, int& a_outValue);
	static bool GetMinSpeed(int a_animationOID, int& a_outValue);
	static bool IsAggressive(int a_animationOID, bool& a_outValue);
	static bool IsHubAnimation(int a_animationOID, bool& a_outValue);
	static bool IsTransitoryAnimation(int a_animationOID, bool& a_outValue);
	static bool GetAllScenes(std::vector<std::string>& a_outScenes);
	static bool GetScenesInRange(
		std::string_view a_sceneID,
		const std::vector<RE::Actor*>& a_actors,
		int a_distance,
		std::vector<std::string>& a_outScenes);
	static bool GetSceneNames(const std::vector<std::string>& a_sceneIDs, std::vector<std::string>& a_outNames);

	static std::optional<OStimSceneInfo> GetCurrentSceneInfo();
	static std::vector<OStimPositionInfo> GetCandidatePositions(bool a_preferCurrentClassOnly);

private:
	static RE::TESQuest* ResolveQuest();
	static RE::VMHandle ResolveQuestHandle();
	static bool HasBoundScript(std::string_view a_className);

	template <class TResult, class... TArgs>
	static bool DispatchQuestMethod(std::string_view a_className, std::string_view a_method, TResult& a_outResult, TArgs&&... a_args);

	template <class... TArgs>
	static bool DispatchQuestMethodVoid(std::string_view a_className, std::string_view a_method, TArgs&&... a_args);

	template <class TResult, class... TArgs>
	static bool DispatchStaticMethod(std::string_view a_className, std::string_view a_method, TResult& a_outResult, TArgs&&... a_args);

	template <class... TArgs>
	static bool DispatchStaticMethodVoid(std::string_view a_className, std::string_view a_method, TArgs&&... a_args);
};
