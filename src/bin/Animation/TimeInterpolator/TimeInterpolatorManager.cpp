#include "TimeInterpolatorManager.h"

#include <vector>

void TimeFloatInterpolatorManager::RegisterInterpolator(TimeFloatInterpolator* interpolator)
{
	std::lock_guard<std::mutex> lock(mutex);
	interpolators.insert(interpolator);
}

void TimeFloatInterpolatorManager::UnregisterInterpolator(TimeFloatInterpolator* interpolator)
{
	std::lock_guard<std::mutex> lock(mutex);
	interpolators.erase(interpolator);
}

void TimeFloatInterpolatorManager::Update(float dt)
{
	std::vector<TimeFloatInterpolator*> completed;
	{
		std::lock_guard<std::mutex> lock(mutex);
		for (auto it = interpolators.begin(); it != interpolators.end();) {
			if ((*it)->Update(dt)) {
				completed.push_back(*it);
				it = interpolators.erase(it);
			} else {
				++it;
			}
		}
	}
	// A bounce callback may re-register its interpolator. Run it only after the
	// completed entry is erased and the manager lock is released.
	for (auto* interpolator : completed) {
		if (interpolator) {
			interpolator->InvokeCallbacks();
		}
	}
	/*ImGui::Begin("INTERPOLATOR DEBUGGING");
	ImGui::Text("%i interpolators present.", interpolators.size());
	ImGui::End();*/
}
