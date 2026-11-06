#!/usr/bin/env python3
"""Generate typed IGI 1 bindings from recovered IGI-RE declarations."""
import json
import re
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LIMIT = 300
DEMO_MANIFEST = ROOT / "tests" / "demo-natives.tsv"
RUNTIME_MANIFEST = ROOT / "tests" / "menu-runtime-natives.tsv"
PINNED_MANIFESTS = (DEMO_MANIFEST, RUNTIME_MANIFEST)
SCALARS = {'void', 'char', 'unsigned char', 'signed char', 'short', 'unsigned short',
           'int', 'unsigned', 'unsigned int', 'long', 'unsigned long', 'float', 'double',
           'long double', 'uint8_t', 'uint16_t', 'uint32_t', 'int8_t', 'int16_t',
           'int32_t', 'size_t', 'bool'}
GROUPS = {
    'MISC': ['FramesSet', 'GameMaterial_LoadParameters'],
    'GAME': ['Game_'], 'PLAYER': ['HumanPlayer_'], 'HUMAN': ['Human_'],
    'SOLDIER': ['Soldier_'], 'WEAPON': ['Weapon_', 'Gun_', 'Ammo_'],
    'CAMERA': ['HumanCamera_', 'HumanView_', 'Camera_', 'Viewport_'],
    'VEHICLE': ['Vehicle_', 'Car_', 'Heli_'], 'GRAPH': ['Graph_', 'AIGraph_'],
    'AI': ['HumanAI_', 'AIFunction_'], 'AUDIO': ['Sound_', 'Music_'],
    'WEATHER': ['RainObj_', 'RainDynCubeObj_'], 'LEVEL': ['Level_', 'LevelQuit'],
    'MENU': ['MenuManager_', 'ListBox_', 'ToggleBox_'], 'SCRIPT': ['Script_'],
    'TASK': ['Task_', 'TaskType_'], 'CONFIG': ['Config_'],
    'OTHER': [],
}

def is_memory_api(name):
    """Exclude memory utilities even when future source tables add aliases."""
    return name.startswith(('Mem_', 'Memory_', 'MEM_', 'MEMORY_', 'crt_',
                            'Virtual', 'Heap', 'ReadProcessMemory',
                            'WriteProcessMemory')) or bool(re.search(
        r'(^|_)(Alloc|Realloc|Dealloc|Malloc|Calloc|Memcpy|Memset|Memmove|'
        r'Patch|ScanMemory|ReadMemory|WriteMemory)(_|$)', name))

def caps(name):
    if name == 'FramesSet':
        return 'FRAMES_SET'
    name = re.sub(r'([A-Z]+)([A-Z][a-z])', r'\1_\2', name)
    return re.sub(r'([a-z0-9])([A-Z])', r'\1_\2', name).upper()

def parse(row):
    address, name, provenance, kind, signature = row
    if kind != 'entry' or not signature or '...' in signature:
        return None
    if is_memory_api(name):
        return None
    match = re.fullmatch(r'(.+?)\s*\b' + re.escape(name) + r'\((.*)\)', signature)
    if not match:
        return None
    result, params = match.groups()
    if '[' in signature or '(' in params:
        return None
    arguments = []
    for param in ([] if params in ('void', '') else params.split(',')):
        arg = re.fullmatch(r'(.+?[\s*])([A-Za-z_]\w*)', param.strip())
        if not arg:
            return None
        argument_name = arg[2] + '_' if arg[2] in {'new', 'delete', 'class', 'template', 'operator', 'this', 'namespace', 'private', 'public', 'protected', 'virtual', 'throw', 'catch', 'try', 'bool'} else arg[2]
        arguments.append((arg[1].strip(), argument_name))
    opaque = set()
    for value_type in [result] + [arg[0] for arg in arguments]:
        base = re.sub(r'\bconst\b|\bstruct\b|\*', '', value_type).strip()
        base = ' '.join(base.split())
        if base not in SCALARS:
            if '*' not in value_type or not re.fullmatch(r'[A-Za-z_]\w*', base):
                return None
            opaque.add(base)
    domain = next((group for group, prefixes in GROUPS.items()
                   if any(name.startswith(prefix) for prefix in prefixes)), None)
    if not domain:
        domain = 'OTHER'
    # ABI callbacks, allocators and initialization/teardown handlers need
    # specialized ownership contracts; they are outside this callable set.
    if any(word in name for word in ['Handler', '_Init', '_Open', '_Close', '_Free',
                                     '_Delete', '_Alloc', '_Register', '_Shutdown']) and name != 'Task_Delete':
        return None
    return dict(address=address, name=name, signature=signature, result=result,
                args=arguments, opaque=opaque, domain=domain, hash=caps(name))

def select_mvp(rows):
    """Select exactly 300 callable entries and pin every native used by the demo."""
    pools = {group: [] for group in GROUPS}
    parsed_by_address = {}
    for row in rows:
        item = parse(row)
        if item:
            pools[item['domain']].append(item)
            parsed_by_address[item['address'].lower()] = item

    pinned = []
    pinned_addresses = set()
    for manifest in PINNED_MANIFESTS:
        for line in manifest.read_text(encoding='utf-8').splitlines():
            if not line or line.startswith('#'):
                continue
            api, domain, name, address, signature = line.split('\t', 4)
            item = parsed_by_address.get(address.lower())
            if not item or (item['name'], item['signature'], item['domain']) != (name, signature, domain):
                raise ValueError(f'native does not exactly match IGI-RE source: {name} @ {address}')
            if address.lower() in pinned_addresses:
                raise ValueError(f'pinned manifest repeats native address: {address}')
            pinned.append(item)
            pinned_addresses.add(address.lower())
    if len(pinned) > LIMIT:
        raise ValueError(f'demo uses {len(pinned)} natives, exceeding the {LIMIT}-native MVP')

    def usefulness(item):
        name = item['name']
        # Prefer usable accessors and controls over engine state-machine steps.
        state_step = any(word in name for word in ('Enter', 'Run', 'Update', 'Draw', 'Create'))
        accessor = any(word in name for word in ('_Get', '_Set', '_Find', '_Is', '_Has'))
        return (state_step, not accessor, int(item['address'], 16))
    for pool in pools.values():
        pool[:] = [item for item in pool if item['address'].lower() not in pinned_addresses]
        pool.sort(key=usefulness)
    selected = list(pinned)
    while len(selected) < LIMIT:
        before = len(selected)
        for pool in pools.values():
            if pool and len(selected) < LIMIT:
                selected.append(pool.pop(0))
        if len(selected) == before:
            raise ValueError(f'IGI-RE source has only {len(selected)} callable entries for MVP')
    assert len({item['address'] for item in selected}) == len(selected)
    return selected


def main():
    rows = [line.split('\t', 4) for line in
            (ROOT/'data/IGI1/neonix-igi-re-signatures.tsv').read_text().splitlines()
            if line and not line.startswith('#')]
    selected = select_mvp(rows)
    hash_counts = Counter(item['hash'] for item in selected)
    for item in selected:
        if hash_counts[item['hash']] > 1:
            item['hash'] += '_' + item['address'].upper()
    opaque = sorted(set().union(*(item['opaque'] for item in selected)))
    header = ['// Generated by tools/generate_mvp_natives.py; IGI-RE declaration metadata.',
              '#pragma once', '#include <cstddef>', '#include <cstdint>',
              '#include "igi_sdk/natives/native_api.hpp"', 'namespace IGI {',
              'using Void = void;', 'std::size_t NativeBindingCount() noexcept;',
              *[f'struct {name};' for name in opaque],
              'namespace HASH {']
    header += [f'inline constexpr std::uint32_t {item["hash"]} = 0x{item["address"]};'
               for item in selected]
    header += ['}']
    cpp = ['// Generated typed calls from IGI-RE; implementations stay in .cpp.',
           '#include "igi_sdk/natives/mvp_natives.hpp"',
           '#include "igi_sdk/natives/igi1_native_registry.hpp"',
           '#include "igi_sdk/natives/native_caller.hpp"',
           '#include "igi_sdk/logging/logging.hpp"',
           '#include <utility>', '#define NATIVE_INVOKE InvokeNative',
           'namespace IGI { namespace {',
           'template<class R, class... A> R InvokeNative(std::uint32_t hash, const char* signature, A... args) {',
           '    auto* caller = igi::igi1NativeCaller();',
           '    if (!caller || !caller->scheduler().isGameThread()) {',
           '        igi::log(igi::LogLevel::Warning, "native refused: runtime/game thread unavailable");',
           '        if constexpr (!std::is_void_v<R>) return R{}; else return;', '    }',
           '    std::uintptr_t address = 0;',
           '    const auto name = caller->nameForAddress(hash);',
           '    if (caller->resolveByAddress(hash, signature, address) != igi::NativeCallStatus::Success) {',
           '        if constexpr (!std::is_void_v<R>) return R{}; else return;', '    }',
           '#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))',
           '    using Function = R(__cdecl *)(A...);',
           '    const auto function = reinterpret_cast<Function>(address);',
           '    try {', '        if constexpr (std::is_void_v<R>) {',
           '            function(args...); igi::NativeCaller::reportSuccess(name); return;',
           '        } else {', '            R result = function(args...);',
           '            igi::NativeCaller::reportSuccess(name); return result;', '        }',
           '    } catch (...) { igi::NativeCaller::reportException(name); }', '#endif',
           '    if constexpr (!std::is_void_v<R>) return R{};', '}', '}']
    cpp.insert(5, '#include <type_traits>')
    records = ['// Generated IGI-RE source bindings; in-game verification is tracked separately.']
    manifest = ['# domain\tapi\taddress\tname\tsignature\tvalidation_status']
    pinned_rows = [line.split('\t', 4) for manifest in PINNED_MANIFESTS
                   for line in manifest.read_text(encoding='utf-8').splitlines()
                   if line and not line.startswith('#')]
    pinned_addresses = {row[3].lower() for row in pinned_rows}
    pinned_apis = {row[3].lower(): row[0] for row in pinned_rows}
    for domain in GROUPS:
        items = [item for item in selected if item['domain'] == domain]
        header.append(f'namespace {domain} {{')
        cpp.append(f'namespace {domain} {{')
        api_counts = Counter(caps(item['name'].split('_', 1)[-1]) for item in items)
        for item in items:
            name = item['name']
            suffix = name.split('_', 1)[1] if '_' in name else name
            api = 'FRAMES_SET' if name == 'FramesSet' else caps(name if domain == 'OTHER' else suffix)
            if api_counts[caps(suffix)] > 1:
                api = caps(name)
            api = pinned_apis.get(item['address'].lower(), api)
            params = ', '.join(f'{kind} {arg}' for kind, arg in item['args'])
            args = ''.join(', '+arg for _, arg in item['args'])
            result = item['result']
            header.append(f'NATIVE_DECL {result} {api}({params});')
            invoke_result = 'Void' if result == 'void' else result
            cpp += [f'NATIVE_DECL {result} {api}({params}) {{',
                    f'    return NATIVE_INVOKE<{invoke_result}>(HASH::{item["hash"]}, {json.dumps(item["signature"])}{args});', '}']
            records.append('if (!catalogue.add({0x'+item['address']+', '+json.dumps(name)+', "IGI-RE", "entry", '+json.dumps(item['signature'])+', NativeVerification::Exposed})) return false;')
            status = 'unicorn_abi' if item['address'].lower() in pinned_addresses else 'source_only'
            manifest.append('\t'.join([domain, api, item['address'], name, item['signature'], status]))
        header.append('}')
        cpp.append('}')
    header += ['} // namespace IGI']
    cpp += [f'std::size_t NativeBindingCount() noexcept {{ return {len(selected)}; }}',
            '} // namespace IGI', '#undef NATIVE_INVOKE']
    outputs = {'include/igi_sdk/natives/mvp_natives.hpp': header,
               'src/igi_sdk/natives/mvp_natives.cpp': cpp,
               'src/igi_sdk/natives/mvp_registry.inc': records,
               'data/IGI1/mvp-300.tsv': manifest}
    for path, lines in outputs.items():
        (ROOT/path).write_text('\n'.join(lines)+'\n')
    print(f'Generated {len(selected)} typed natives:', dict(Counter(i['domain'] for i in selected)))

if __name__ == '__main__':
    main()
