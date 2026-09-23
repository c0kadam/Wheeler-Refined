from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
INPUT = (ROOT / "src/bin/UserInput/Input.cpp").read_text(encoding="utf-8")
BROKER = (ROOT / "src/bin/InputBroker.cpp").read_text(encoding="utf-8")
ADAPTER = (ROOT / "src/bin/Integrations/DMenuInputOwnership.cpp").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"FAIL: {message}")


require(
    "const bool wheelerInputSuspendedByOwner" in INPUT,
    "missing dispatch-scoped effective-owner decision",
)
require(
    "Wheeler::IsWheelerOpen() && !brokerOwnerBlocked && !passthroughThisEvent && !consumeEvent" in INPUT,
    "per-event MainWheel hard lock is not ownership-aware",
)
require(
    "!wheelerInputSuspendedByOwner;" in INPUT,
    "dispatch-wide exclusivity does not use the shared owner decision",
)
require(
    "bool brokerAllowsWheelProcessing = !brokerOwnerBlocked;" in INPUT,
    "ordinary Wheeler control dispatch is not suspended by the owner decision",
)
require(
    "InputBroker::IsBlockedByActiveOwner" in INPUT,
    "Input.cpp does not consume the generic InputBroker answer",
)
require(
    all(symbol not in INPUT for symbol in ("dMenu_IsMenuOpen", "dMenu_IsReady", "GetModuleHandleW")),
    "dMenu-specific calls leaked into Input.cpp",
)
require(
    "GetModuleHandleW(L\"dmenu.dll\")" in ADAPTER
    and "GetProcAddress(module, \"dMenu_IsMenuOpen\")" in ADAPTER,
    "optional dMenu state adapter is incomplete",
)
require(
    all(symbol not in ADAPTER for symbol in ("LoadLibrary", "GetAsyncKeyState", "Present")),
    "adapter introduced a hard load, physical polling, or Present polling",
)
require(
    "effectiveActiveOwner == kDMenuInputOwnerId" in BROKER,
    "InputBroker does not represent dMenu as the effective higher owner",
)

print("PASS: dMenu/Wheeler input ownership static contract")
