#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string_view>

namespace HookValidation
{
	inline bool IsSkyrim1799()
	{
		return REL::Module::get().version() == SKSE::RUNTIME_SSE_1_7_99;
	}

	inline bool MatchBytes(std::uintptr_t a_address, std::initializer_list<std::uint8_t> a_expected)
	{
		const auto* bytes = reinterpret_cast<const std::uint8_t*>(a_address);
		std::size_t index = 0;
		for (const auto expected : a_expected) {
			if (bytes[index++] != expected) {
				return false;
			}
		}

		return true;
	}

	inline std::uintptr_t DecodeRelativeCallTarget(std::uintptr_t a_callAddress)
	{
		std::int32_t displacement = 0;
		std::memcpy(
			&displacement,
			reinterpret_cast<const void*>(a_callAddress + 1),
			sizeof(displacement));
		return static_cast<std::uintptr_t>(
			static_cast<std::intptr_t>(a_callAddress + 5) + displacement);
	}

	template <class ContextValidator>
	bool ValidateCallHookSite(
		std::uintptr_t a_anchorAddress,
		std::uintptr_t a_callAddress,
		std::uint64_t a_expectedTargetID,
		std::string_view a_component,
		std::string_view a_label,
		ContextValidator&& a_contextValidator)
	{
		const auto runtime = REL::Module::get().version();
		const auto opcode = *reinterpret_cast<const std::uint8_t*>(a_callAddress);
		if (opcode != 0xE8) {
			logger::warn(
				"{}: {} hook validation failed runtime={} anchor={:X} candidate={:X}: unexpected opcode {:02X}; skipping install",
				a_component,
				a_label,
				runtime.string("."),
				a_anchorAddress,
				a_callAddress,
				static_cast<std::uint32_t>(opcode));
			return false;
		}

		// Phase B verified structural signatures only for Skyrim 1.7.99.
		// Preserve the historical opcode-only validation on older runtimes.
		if (!IsSkyrim1799()) {
			return true;
		}

		if (!a_contextValidator()) {
			logger::warn(
				"{}: {} hook validation failed runtime={} anchor={:X} candidate={:X}: instruction context mismatch; skipping install",
				a_component,
				a_label,
				runtime.string("."),
				a_anchorAddress,
				a_callAddress);
			return false;
		}

		const auto actualTarget = DecodeRelativeCallTarget(a_callAddress);
		const auto expectedTarget = REL::ID(a_expectedTargetID).address();
		if (actualTarget != expectedTarget) {
			logger::warn(
				"{}: {} hook validation failed runtime={} anchor={:X} candidate={:X}: decoded target {:X} does not match Address Library ID {} at {:X}; skipping install",
				a_component,
				a_label,
				runtime.string("."),
				a_anchorAddress,
				a_callAddress,
				actualTarget,
				a_expectedTargetID,
				expectedTarget);
			return false;
		}

		return true;
	}
}
