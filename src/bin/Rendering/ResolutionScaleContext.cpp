#include "ResolutionScaleContext.h"

#include <algorithm>
#include <cmath>

#include <Windows.h>
#include <dxgi.h>

#include "bin/Config.h"

namespace ResolutionScale
{
	Context& Context::GetSingleton()
	{
		static Context instance;
		return instance;
	}

	void Context::Initialize(IDXGISwapChain* swapchain, HWND hwnd)
	{
		_swapchain = swapchain;
		_hwnd = hwnd;
		_initialized = true;
		_dirty = true;
		Update();
	}

	void Context::MarkDirty()
	{
		_dirty = true;
	}

	bool Context::TryGetDisplaySize(float& outW, float& outH) const
	{
		if (!_hwnd) {
			return false;
		}

		RECT rect{};
		if (!GetClientRect(_hwnd, &rect)) {
			return false;
		}

		outW = static_cast<float>(rect.right - rect.left);
		outH = static_cast<float>(rect.bottom - rect.top);
		return outW > 0.0f && outH > 0.0f;
	}

	bool Context::TryGetGameSize(float& outW, float& outH) const
	{
		if (!_swapchain) {
			return false;
		}

		DXGI_SWAP_CHAIN_DESC desc{};
		if (FAILED(_swapchain->GetDesc(&desc))) {
			return false;
		}

		outW = static_cast<float>(desc.BufferDesc.Width);
		outH = static_cast<float>(desc.BufferDesc.Height);
		return outW > 0.0f && outH > 0.0f;
	}

	void Context::Recompute(float displayW, float displayH, float gameW, float gameH)
	{
		_state.displayW = displayW;
		_state.displayH = displayH;
		_state.gameW = gameW;
		_state.gameH = gameH;

		const bool validDisplay = displayW > 0.0f && displayH > 0.0f;
		const bool validGame = gameW > 0.0f && gameH > 0.0f;

		if (!validDisplay || !validGame) {
			_state.scaleX = 1.0f;
			_state.scaleY = 1.0f;
			_state.uniformScale = 1.0f;
			_state.mismatch = false;
			return;
		}

		_state.scaleX = gameW / displayW;
		_state.scaleY = gameH / displayH;
		_state.uniformScale = (std::min)(_state.scaleX, _state.scaleY);

		const float epsilon = (std::max)(Config::ResolutionFix::Epsilon, 0.0f);
		_state.mismatch = std::fabs(_state.scaleX - 1.0f) > epsilon || std::fabs(_state.scaleY - 1.0f) > epsilon;
	}

	void Context::Update()
	{
		if (!_initialized) {
			return;
		}

		const int modeValue = static_cast<int>(Config::ResolutionFix::ModeSetting);
		if (_lastModeValue != modeValue || _lastEnabled != Config::ResolutionFix::Enabled ||
			_lastEpsilon != Config::ResolutionFix::Epsilon) {
			_dirty = true;
			_lastModeValue = modeValue;
			_lastEnabled = Config::ResolutionFix::Enabled;
			_lastEpsilon = Config::ResolutionFix::Epsilon;
		}

		float displayW = _state.displayW;
		float displayH = _state.displayH;
		float gameW = _state.gameW;
		float gameH = _state.gameH;

		const bool gotDisplay = TryGetDisplaySize(displayW, displayH);
		const bool gotGame = TryGetGameSize(gameW, gameH);

		if (!gotDisplay && !_loggedMissingDisplay) {
			_loggedMissingDisplay = true;
			logger::warn("[ResolutionFix] Failed to read display size (HWND invalid or GetClientRect failed).");
		}
		if (!gotGame && !_loggedMissingGame) {
			_loggedMissingGame = true;
			logger::warn("[ResolutionFix] Failed to read game size (swapchain not ready).");
		}

		if (_dirty || gotDisplay || gotGame) {
			Recompute(displayW, displayH, gameW, gameH);
			_dirty = false;
		}

		if ((_state.displayW <= 0.0f || _state.displayH <= 0.0f ||
				_state.gameW <= 0.0f || _state.gameH <= 0.0f) && !_loggedInvalidDims) {
			_loggedInvalidDims = true;
			logger::warn("[ResolutionFix] Invalid dimensions (display {}x{}, game {}x{}). Mapping disabled.",
				_state.displayW, _state.displayH, _state.gameW, _state.gameH);
		}

		if (!Config::ResolutionFix::Enabled) {
			_state.active = false;
			return;
		}

		switch (Config::ResolutionFix::ModeSetting) {
		case Config::ResolutionFix::Mode::ForceDisplayToGame:
			_state.active = true;
			break;
		case Config::ResolutionFix::Mode::ForceNone:
			_state.active = false;
			break;
		case Config::ResolutionFix::Mode::Auto:
		default:
			_state.active = _state.mismatch;
			break;
		}
	}

	ImVec2 Context::GetRenderSize() const
	{
		if (_state.active && _state.gameW > 0.0f && _state.gameH > 0.0f) {
			return ImVec2(_state.gameW, _state.gameH);
		}
		return ImVec2(_state.displayW, _state.displayH);
	}

	float Context::ToGameX(float displayPx) const
	{
		return _state.active ? displayPx * _state.scaleX : displayPx;
	}

	float Context::ToGameY(float displayPx) const
	{
		return _state.active ? displayPx * _state.scaleY : displayPx;
	}

	ImVec2 Context::ToGame(ImVec2 displayPx) const
	{
		return ImVec2(ToGameX(displayPx.x), ToGameY(displayPx.y));
	}

	float Context::ToGameUniform(float displayPx) const
	{
		return _state.active ? displayPx * _state.uniformScale : displayPx;
	}

	float Context::ToDisplayX(float gamePx) const
	{
		return _state.active && _state.scaleX > 0.0f ? (gamePx / _state.scaleX) : gamePx;
	}

	float Context::ToDisplayY(float gamePx) const
	{
		return _state.active && _state.scaleY > 0.0f ? (gamePx / _state.scaleY) : gamePx;
	}

	ImVec2 Context::ToDisplay(ImVec2 gamePx) const
	{
		return ImVec2(ToDisplayX(gamePx.x), ToDisplayY(gamePx.y));
	}
}
