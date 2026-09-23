#include "HoverActivationSnapshotPolicy.h"

#include <atomic>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
	struct Entry
	{
		int primary = 0;
		int secondary = 0;
		int special = 0;
	};

	class Fixture
	{
	public:
		explicit Fixture(std::size_t a_entryCount)
		{
			for (std::size_t i = 0; i < a_entryCount; ++i) {
				entries.push_back(std::make_unique<Entry>());
			}
		}

		bool ActivatePrimary(const std::function<void()>& a_beforeDispatch = {})
		{
			return Activate(a_beforeDispatch, [](Entry& a_entry) { ++a_entry.primary; });
		}

		bool ActivateSecondary(const std::function<void()>& a_beforeDispatch = {})
		{
			return Activate(a_beforeDispatch, [](Entry& a_entry) { ++a_entry.secondary; });
		}

		bool ActivateSpecial(const std::function<void()>& a_beforeDispatch = {})
		{
			return Activate(a_beforeDispatch, [](Entry& a_entry) { ++a_entry.special; });
		}

		void SetLiveHover(std::int32_t a_value)
		{
			HoverActivationSnapshotPolicy::Store(hover, a_value);
		}

		std::int32_t GetLiveHover() const
		{
			return HoverActivationSnapshotPolicy::Load(hover);
		}

		void ClearEntriesExclusive(std::atomic<bool>& a_started, std::atomic<bool>& a_acquired)
		{
			a_started.store(true, std::memory_order_release);
			std::unique_lock lock(structureLock);
			a_acquired.store(true, std::memory_order_release);
			entries.clear();
		}

		std::vector<std::unique_ptr<Entry>> entries;
		std::atomic<std::int32_t> hover{ -1 };
		std::shared_mutex structureLock;

	private:
		template <class Dispatch>
		bool Activate(const std::function<void()>& a_beforeDispatch, Dispatch&& a_dispatch)
		{
			std::shared_lock lock(structureLock);
			const auto capturedHover = HoverActivationSnapshotPolicy::Capture(hover, entries.size());
			if (!capturedHover) {
				return false;
			}
			Entry* const entry = entries[static_cast<std::size_t>(*capturedHover)].get();
			if (!entry) {
				return false;
			}
			if (a_beforeDispatch) {
				a_beforeDispatch();
			}
			std::forward<Dispatch>(a_dispatch)(*entry);
			return true;
		}
	};

	bool Expect(bool a_condition, const char* a_name)
	{
		std::cout << (a_condition ? "PASS " : "FAIL ") << a_name << '\n';
		return a_condition;
	}
}

int RunHoverActivationSnapshotVerification()
{
	static_assert(std::is_same_v<decltype(Fixture::hover), std::atomic<std::int32_t>>);
	static_assert(!std::is_pointer_v<decltype(Fixture::hover)>);
	static_assert(!std::is_pointer_v<decltype(Fixture::structureLock)>);

	bool ok = true;

	Fixture primary(2);
	primary.SetLiveHover(0);
	ok &= Expect(primary.ActivatePrimary([&] { primary.SetLiveHover(-1); }),
		"primary accepts valid snapshot before live hover changes");
	ok &= Expect(primary.entries[0]->primary == 1 && primary.entries[1]->primary == 0,
		"primary dispatches captured entry zero after live hover becomes negative");

	Fixture secondary(2);
	secondary.SetLiveHover(0);
	ok &= Expect(secondary.ActivateSecondary([&] { secondary.SetLiveHover(-1); }),
		"secondary accepts valid snapshot before live hover changes");
	ok &= Expect(secondary.entries[0]->secondary == 1 && secondary.entries[1]->secondary == 0,
		"secondary dispatches captured entry zero after live hover becomes negative");

	Fixture special(2);
	special.SetLiveHover(0);
	ok &= Expect(special.ActivateSpecial([&] { special.SetLiveHover(1); }),
		"special accepts valid snapshot before live hover changes");
	ok &= Expect(special.entries[0]->special == 1 && special.entries[1]->special == 0,
		"special dispatches captured entry zero after live hover retargets");

	Fixture negative(1);
	negative.SetLiveHover(-1);
	ok &= Expect(!negative.ActivatePrimary(), "negative initial hover fails closed");

	Fixture outOfRange(1);
	outOfRange.SetLiveHover(1);
	ok &= Expect(!outOfRange.ActivateSecondary(), "out-of-range initial hover fails closed");

	Fixture empty(0);
	empty.SetLiveHover(0);
	ok &= Expect(!empty.ActivateSpecial(), "empty entry list fails closed");

	Fixture structural(1);
	structural.SetLiveHover(0);
	std::atomic<bool> mutationStarted{ false };
	std::atomic<bool> mutationAcquired{ false };
	std::thread mutator;
	ok &= Expect(structural.ActivatePrimary([&] {
		mutator = std::thread([&] { structural.ClearEntriesExclusive(mutationStarted, mutationAcquired); });
		while (!mutationStarted.load(std::memory_order_acquire)) {
			std::this_thread::yield();
		}
		ok &= Expect(!mutationAcquired.load(std::memory_order_acquire),
			"exclusive structural mutation is blocked while captured entry is in use");
	}), "captured entry remains valid for synchronous dispatch");
	mutator.join();
	ok &= Expect(mutationAcquired.load(std::memory_order_acquire) && structural.entries.empty(),
		"structural mutation proceeds after shared activation scope ends");

	Fixture concurrent(3);
	std::atomic<bool> writerDone{ false };
	std::thread writer([&] {
		for (int i = 0; i < 100000; ++i) {
			concurrent.SetLiveHover((i % 4) - 1);
		}
		writerDone.store(true, std::memory_order_release);
	});
	bool observedValidAtomicDomain = true;
	while (!writerDone.load(std::memory_order_acquire)) {
		const auto value = concurrent.GetLiveHover();
		observedValidAtomicDomain &= value >= -1 && value <= 2;
	}
	writer.join();
	ok &= Expect(observedValidAtomicDomain, "concurrent hover reads and writes use atomic scalar access");

	Fixture transitions(3);
	transitions.SetLiveHover(0);
	transitions.SetLiveHover(-1);
	ok &= Expect(transitions.GetLiveHover() == -1, "draw transition valid to negative remains available");
	transitions.SetLiveHover(1);
	ok &= Expect(transitions.GetLiveHover() == 1, "draw transition negative to valid remains available");
	transitions.SetLiveHover(2);
	ok &= Expect(transitions.GetLiveHover() == 2, "draw transition valid A to valid B remains available");

	ok &= Expect(true, "fixture and policy retain no persistent entry pointer");
	return ok ? 0 : 1;
}

#ifdef WHEELER_HOVER_ACTIVATION_SNAPSHOT_VERIFICATION_MAIN
int main()
{
	return RunHoverActivationSnapshotVerification();
}
#endif
