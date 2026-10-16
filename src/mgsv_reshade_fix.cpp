// SPDX-License-Identifier: MIT
//
// MGSV ReShade Fix - lets ReShade run natively on D3D11 in Metal Gear Solid V.
//
// Root cause: during renderer init (mgsvtpp.exe 1.0.15.4, function at +0x2B9BA0) the game
// walks its modules with EnumProcessModules/GetModuleBaseNameA, takes the range of the
// module named "d3d11.dll" via GetModuleInformation, and checks that 97 vtable entries of
// its D3D11 object fall inside that range. ReShade hands the game wrapper objects whose
// vtables live in ReShade's own DLL, so the check fails and the game quietly calls
// ExitProcess(0) before its window ever shows. (DXVK passes because its vtables live in a
// module that is itself named d3d11.dll.)
//
// Fix: redirect the game's GetModuleInformation import so that, while ReShade is wrapping a
// D3D11 device, a query for d3d11.dll reports ReShade's module instead. The game calls
// GetModuleInformation from that one check only, and nothing else in the game is touched.

#include <windows.h>
#include <psapi.h>
#include <reshade.hpp>

extern "C" __declspec(dllexport) const char *NAME = "MGSV ReShade Fix";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "Makes Metal Gear Solid V accept ReShade's D3D11 wrappers (the game otherwise exits silently at startup).";

static HMODULE g_reshade_module = nullptr;
static bool g_reshade_wraps_d3d11 = false;
static ULONG_PTR *g_iat_slot = nullptr;
static decltype(&GetModuleInformation) g_original_get_module_information = nullptr;

static bool is_mgsv_executable()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    const wchar_t *name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    return _wcsicmp(name, L"mgsvtpp.exe") == 0 || _wcsicmp(name, L"MgsGroundZeroes.exe") == 0;
}

static HMODULE find_reshade_module()
{
    HMODULE modules[1024];
    DWORD needed = 0;
    if (!EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed))
        return nullptr;
    for (DWORD i = 0; i < needed / sizeof(HMODULE) && i < 1024; ++i)
        if (GetProcAddress(modules[i], "ReShadeRegisterAddon") != nullptr)
            return modules[i];
    return nullptr;
}

static BOOL WINAPI hook_get_module_information(HANDLE process, HMODULE module, LPMODULEINFO info, DWORD size)
{
    if (g_reshade_wraps_d3d11 && g_reshade_module != nullptr && module != nullptr)
    {
        wchar_t path[MAX_PATH];
        if (GetModuleFileNameW(module, path, MAX_PATH) != 0)
        {
            const wchar_t *name = wcsrchr(path, L'\\');
            name = name ? name + 1 : path;
            if (_wcsicmp(name, L"d3d11.dll") == 0)
            {
                reshade::log::message(reshade::log::level::info, "Reporting ReShade's module range for d3d11.dll to the game's wrapper check.");
                module = g_reshade_module;
            }
        }
    }
    return g_original_get_module_information(process, module, info, size);
}

// Finds the game's import slot for psapi!GetModuleInformation by name.
static ULONG_PTR *find_iat_slot(const char *dll_name, const char *function_name)
{
    BYTE *const base = reinterpret_cast<BYTE *>(GetModuleHandleW(nullptr));
    const auto nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + reinterpret_cast<IMAGE_DOS_HEADER *>(base)->e_lfanew);
    const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (dir.VirtualAddress == 0)
        return nullptr;

    for (auto desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress); desc->Name != 0; ++desc)
    {
        if (_stricmp(reinterpret_cast<const char *>(base + desc->Name), dll_name) != 0 || desc->OriginalFirstThunk == 0)
            continue;
        auto names = reinterpret_cast<IMAGE_THUNK_DATA *>(base + desc->OriginalFirstThunk);
        auto slots = reinterpret_cast<ULONG_PTR *>(base + desc->FirstThunk);
        for (; names->u1.AddressOfData != 0; ++names, ++slots)
        {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                continue;
            const auto by_name = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(base + names->u1.AddressOfData);
            if (strcmp(reinterpret_cast<const char *>(by_name->Name), function_name) == 0)
                return slots;
        }
    }
    return nullptr;
}

static void write_iat_slot(ULONG_PTR *slot, ULONG_PTR value)
{
    DWORD old_protect;
    VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old_protect);
    *slot = value;
    VirtualProtect(slot, sizeof(*slot), old_protect, &old_protect);
}

static void on_init_device(reshade::api::device *device)
{
    if (device->get_api() == reshade::api::device_api::d3d11)
        g_reshade_wraps_d3d11 = true;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        if (!reshade::register_addon(module))
            return FALSE;
        if (!is_mgsv_executable())
            break;

        g_reshade_module = find_reshade_module();
        g_iat_slot = find_iat_slot("psapi.dll", "GetModuleInformation");
        if (g_iat_slot == nullptr)
        {
            reshade::log::message(reshade::log::level::warning, "Game does not import psapi!GetModuleInformation; nothing to do.");
            break;
        }
        g_original_get_module_information = reinterpret_cast<decltype(&GetModuleInformation)>(*g_iat_slot);
        write_iat_slot(g_iat_slot, reinterpret_cast<ULONG_PTR>(&hook_get_module_information));
        reshade::register_event<reshade::addon_event::init_device>(on_init_device);
        break;
    case DLL_PROCESS_DETACH:
        if (g_iat_slot != nullptr)
            write_iat_slot(g_iat_slot, reinterpret_cast<ULONG_PTR>(g_original_get_module_information));
        reshade::unregister_addon(module);
        break;
    }
    return TRUE;
}
