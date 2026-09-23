#pragma once
/**
 * Wraps around a float value and linearly interpolates it over time when applicable.
 * The float stays at a fixed value when not interpolating and can be read using GetValue().
 * 
 * Interpolator puts itself into a TimeFloatInterpolatorManager on construction and removes 
 * itself on destruction; TimeFloatInterpolatorManager is a static class that handles updating 
 * all instances of TimeFloatInterpolator.
 */
class TimeFloatInterpolator
{
private:
	std::atomic<double> value;
	std::atomic<double> target;
	std::atomic<double> duration;
	std::atomic<double> elapsedTime;

public:
	TimeFloatInterpolator(double initialValue);
	TimeFloatInterpolator(double initialValue, std::function<void()> callback);
	TimeFloatInterpolator();
	~TimeFloatInterpolator();

	void InterpolateTo(double targetValue, double interpolDuration);

	/// <summary>
	/// Push a callback function to the interpolator, which will be invoked one the interpolate finishes interpolating.
	/// Note that the callback function does not run on the imgui thread.
	/// </summary>
	/// <param name="callback"></param>
	void PushCallback(std::function<void()> callback);

	// Update the interpolator's value based on a delta. Only TimeFloatInterpolatorManager may call it.
	// returns whether the interpolator reaches its target value, which signals that it should be removed from the manager
	bool Update(double dt);
	void InvokeCallbacks();

	double GetValue() const;

	void ForceFinish(bool wantCallback = true);

	void SetValue(double value);

	void ForceValue(double value);

	std::vector<std::function<void()>> _callbacks;
};
