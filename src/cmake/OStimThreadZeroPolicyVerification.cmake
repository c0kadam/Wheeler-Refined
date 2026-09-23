if(NOT DEFINED NATIVE_API_SOURCE OR NOT DEFINED INTEGRATION_SOURCE)
	message(FATAL_ERROR "Native API and integration source paths are required")
endif()

file(READ "${NATIVE_API_SOURCE}" NATIVE_API_TEXT)
file(READ "${INTEGRATION_SOURCE}" INTEGRATION_TEXT)
string(REGEX REPLACE "[ \t\r\n]+" " " NATIVE_API_NORMALIZED "${NATIVE_API_TEXT}")
string(REGEX REPLACE "[ \t\r\n]+" " " INTEGRATION_NORMALIZED "${INTEGRATION_TEXT}")

foreach(FORBIDDEN_TEXT IN ITEMS
	"threadID == 0"
	"threadID != 0"
	"0 == threadID"
	"0 != threadID"
	"s_activeThreadID")
	string(FIND "${NATIVE_API_NORMALIZED}" "${FORBIDDEN_TEXT}" FOUND_AT)
	if(NOT FOUND_AT EQUAL -1)
		message(FATAL_ERROR "Forbidden numeric thread-validity policy remains: ${FORBIDDEN_TEXT}")
	endif()
endforeach()

foreach(REQUIRED_TEXT IN ITEMS
	"api->IsThreadValid(a_sceneInfo.threadID)"
	"api->GetNavigationCount(a_sceneInfo.threadID)"
	"api->IsThreadValid(a_threadID)"
	"api->NavigateToScene(a_threadID, sceneID.c_str())")
	string(FIND "${NATIVE_API_NORMALIZED}" "${REQUIRED_TEXT}" FOUND_AT)
	if(FOUND_AT EQUAL -1)
		message(FATAL_ERROR "Required opaque-thread-ID behavior is missing: ${REQUIRED_TEXT}")
	endif()
endforeach()

string(FIND
	"${INTEGRATION_NORMALIZED}"
	"OStimNGThreadAPI::NavigateToScene( snapshot.sceneInfo->threadID, a_payload->sceneID)"
	EXPLICIT_THREAD_DISPATCH_AT)
if(EXPLICIT_THREAD_DISPATCH_AT EQUAL -1)
	message(FATAL_ERROR "Position dispatch does not use the validated tracker snapshot thread ID")
endif()

foreach(FORBIDDEN_ACTION_READ IN ITEMS
	"GetNavigationOptions("
	"GetNavigationPositions("
	"GetCandidatePositions(")
	string(FIND "${INTEGRATION_NORMALIZED}" "${FORBIDDEN_ACTION_READ}" FOUND_AT)
	if(NOT FOUND_AT EQUAL -1)
		message(FATAL_ERROR "Action/browser source contains a live navigation read: ${FORBIDDEN_ACTION_READ}")
	endif()
endforeach()

message(STATUS "OStim thread-zero static verification PASSED")
