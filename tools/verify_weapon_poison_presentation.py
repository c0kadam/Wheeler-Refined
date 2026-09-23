from pathlib import Path
from static_cpp_checks import extract_braced_body

root = Path(__file__).resolve().parents[1]
source = (root / 'src/bin/Wheeler/WheelItems/WheelItemWeapon.cpp').read_text(encoding='utf-8')
policy = (root / 'src/bin/Wheeler/WheelItems/WeaponPoisonPresentationPolicy.h').read_text(encoding='utf-8')
cmake = (root / 'src/CMakeLists.txt').read_text(encoding='utf-8')
reader = extract_braced_body(source, 'bool TryReadPoisonPresentationMember(')
resolver = extract_braced_body(source, 'WeaponPoisonPresentationPolicy::WeaponPoisonPresentation ResolveWeaponPoisonPresentation(')
formatter = extract_braced_body(source, 'std::string AppendWeaponPoisonHighlightText(')
highlight = extract_braced_body(source, 'void WheelItemWeapon::DrawHighlight(')
new_code = reader + resolver + formatter
checks = []
def check(ok, name):
    checks.append(ok)
    print(('PASS ' if ok else 'FAIL ') + name)

for forbidden in ('GetItemExtraDataAndCount', 'SetUniqueID', '_logicalRowSignature', 'EquipObject', 'UnequipObject',
                  'GetEquippedObject', 'ResolveGroupedPoisonAliasPresentation', 'QueuePoisonApply', 'HandMemory',
                  'GetPlayerItemCount', 'BuildLogicalRowSignature', 'GetItemEnchantment'):
    check(forbidden not in new_code, 'new path excludes ' + forbidden)
check(source.count('ResolveWeaponPoisonPresentation(') == 2 and 'ResolveWeaponPoisonPresentation(' in highlight,
      'resolver has only highlight consumer')
check(source.count('AppendWeaponPoisonHighlightText(') == 2, 'formatter has only highlight consumer')
check('const RE::TESObjectREFR::InventoryItemMap& a_inv' in source, 'inventory parameter is const')
check('InvokeWithSehGuard' in reader and 'TryHasTypeSafe' in reader, 'poison fields and probes guarded')
check('CopyExtraListsSafe' in resolver and 'guarded && complete' in resolver, 'enumeration failure rejects evidence')
check('Resolve(sameFormCount, a_uid, guarded && complete, members)' in resolver, 'production uses tested ownership policy')
check('LookupByID<RE::AlchemyItem>' in resolver and 'ValidateForm(' in resolver, 'typed live form validation')
check('InvokeWithSehGuard' in formatter and 'LookupByID<RE::AlchemyItem>' in formatter, 'formatter reacquires guarded form')
check('Utils::Magic::GetMagicItemDescription(poison, effects)' in formatter, 'existing engine formatter reused')
check('name = rawName' in formatter and 'IsPlaceholderName' in formatter, 'owned non-placeholder name')
check(highlight.index('AppendWeaponPoisonHighlightText(') < highlight.index('calculateHighlightTextShiftY('), 'layout uses combined text')
check('GetItemEnchantment(a_imap, enchants)' in highlight, 'existing enchantment path retained')
check(all(t not in policy for t in ('ExtraDataList', 'InventoryEntryData', 'AlchemyItem*', 'InventoryItemMap')), 'policy has no engine pointers')
check('WeaponPoisonPresentationVerification\\\\.cpp$' in cmake, 'new test excluded from DLL glob')
check('add_executable(wheeler_weapon_poison_presentation_verification EXCLUDE_FROM_ALL' in cmake, 'standalone test target')
print(f'{sum(checks)}/{len(checks)} checks passed')
raise SystemExit(0 if all(checks) else 1)
