#include "TimeFloatInterpolator.h"
#include "TimeInterpolatorManager.h"
TimeFloatInterpolator::TimeFloatInterpolator(double initialValue) :
	value(initialValue), target(initialValue), duration(0.0f), elapsedTime(0.0f)
{
}

TimeFloatInterpolator::TimeFloatInterpolator(double initialValue, std::function<void()> callback) :
	value(initialValue), target(initialValue), duration(0.0f), elapsedTime(0.0f)
{
	_callbacks.push_back(callback);
}

TimeFloatInterpolator::TimeFloatInterpolator() :
	value(0),
	target(0), duration(0.0f), elapsedTime(0.0f)
{
}

TimeFloatInterpolator::~TimeFloatInterpolator()
{
	TimeFloatInterpolatorManager::UnregisterInterpolator(this);
}

void TimeFloatInterpolator::InterpolateTo(double targetValue, double interpolDuration)
{
	target = targetValue;
	duration = interpolDuration;
	elapsedTime = 0.0f;
	TimeFloatInterpolatorManager::RegisterInterpolator(this);
}

void TimeFloatInterpolator::PushCallback(std::function<void()> callback)
{
	this->_callbacks.push_back(callback);
}

bool TimeFloatInterpolator::Update(double dt)
{
	if (elapsedTime < duration) {
		elapsedTime += dt;
		float t = min(elapsedTime / duration, 1.0f);
		value = value + (target - value) * t;
		if (elapsedTime >= duration) { // we're done
			return true;
		}
	}
	return false;
}

void TimeFloatInterpolator::InvokeCallbacks()
{
	for (auto& callback : _callbacks) {
		callback();
	}
}

double TimeFloatInterpolator::GetValue() const
{
	return value;
}

void TimeFloatInterpolator::ForceFinish(bool wantCallback)
{
	this->elapsedTime.store(this->duration);
	this->value.store(this->target);
	if (wantCallback) {
		InvokeCallbacks();
	}
	TimeFloatInterpolatorManager::UnregisterInterpolator(this);
}

void TimeFloatInterpolator::SetValue(double value)
{
	this->value = value;
}

void TimeFloatInterpolator::ForceValue(double value)
{
	this->target = value;
	this->value = value;
	this->duration = 0;
	this->elapsedTime = 0;
}
