#pragma once

#include <cstdint>

#include "imgui.h"

struct IDXGISwapChain;
struct HWND__;
using HWND = HWND__*;

namespace ResolutionScale
{
	struct State
	{
		float displayW = 0.0f;
		float displayH = 0.0f;
		float gameW = 0.0f;
		float gameH = 0.0f;
		float scaleX = 1.0f;
		float scaleY = 1.0f;
		float uniformScale = 1.0f;
		bool mismatch = false;
		bool active = false;
	};

	class Context
	{
	public:
		static Context& GetSingleton();

		void Initialize(IDXGISwapChain* swapchain, HWND hwnd);
		void MarkDirty();
		void Update();

		const State& GetState() const { return _state; }
		bool IsActive() const { return _state.active; }

		ImVec2 GetRenderSize() const;

		float ToGameX(float displayPx) const;
		float ToGameY(float displayPx) const;
		ImVec2 ToGame(ImVec2 displayPx) const;
		float ToGameUniform(float displayPx) const;

		float ToDisplayX(float gamePx) const;
		float ToDisplayY(float gamePx) const;
		ImVec2 ToDisplay(ImVec2 gamePx) const;

	private:
		bool TryGetDisplaySize(float& outW, float& outH) const;
		bool TryGetGameSize(float& outW, float& outH) const;
		void Recompute(float displayW, float displayH, float gameW, float gameH);

		IDXGISwapChain* _swapchain = nullptr;
		HWND _hwnd = nullptr;
		State _state{};
		bool _initialized = false;
		bool _dirty = true;
		bool _loggedMissingDisplay = false;
		bool _loggedMissingGame = false;
		bool _loggedInvalidDims = false;
		int _lastModeValue = -1;
		bool _lastEnabled = true;
		float _lastEpsilon = -1.0f;
	};
}
