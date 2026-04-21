#include "pch.h"
#include "Injector.h"
#include <fstream>

#pragma runtime_checks("", off)
static void __stdcall Shellcode(Injector::MappingData* pData) {
    if (!pData) return;

    auto* base = reinterpret_cast<uint8_t*>(pData->BaseAddress);
    auto* dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
    auto* ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dosHeader->e_lfanew);
    auto* optHeader = &ntHeaders->OptionalHeader;

    auto _LoadLibraryA = pData->LoadLibraryAFn;
    auto _GetProcAddress = pData->GetProcAddressFn;

    auto delta = reinterpret_cast<uintptr_t>(base) - optHeader->ImageBase;
    if (delta) {
        if (optHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size) {
            auto* pReloc = reinterpret_cast<PIMAGE_BASE_RELOCATION>(base + optHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress);
            while (pReloc->VirtualAddress) {
                if (pReloc->SizeOfBlock >= sizeof(IMAGE_BASE_RELOCATION)) {
                    int count = (pReloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
                    WORD* list = reinterpret_cast<WORD*>(pReloc + 1);

                    for (int i = 0; i < count; i++) {
                        if (list[i]) {
                            auto* ptr = reinterpret_cast<uintptr_t*>(base + pReloc->VirtualAddress + (list[i] & 0xFFF));
                            *ptr += delta;
                        }
                    }
                }
                pReloc = reinterpret_cast<PIMAGE_BASE_RELOCATION>(reinterpret_cast<uint8_t*>(pReloc) + pReloc->SizeOfBlock);
            }
        }
    }

    if (optHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size) {
        auto* pImportDescr = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(base + optHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
        while (pImportDescr->Name) {
            char* szMod = reinterpret_cast<char*>(base + pImportDescr->Name);
            HINSTANCE hMag = _LoadLibraryA(szMod);

            auto* pThunk = reinterpret_cast<PIMAGE_THUNK_DATA>(base + pImportDescr->OriginalFirstThunk);
            auto* pIAT = reinterpret_cast<PIMAGE_THUNK_DATA>(base + pImportDescr->FirstThunk);

            if (!pThunk) pThunk = pIAT;

            while (pThunk->u1.AddressOfData) {
                if (IMAGE_SNAP_BY_ORDINAL(pThunk->u1.Ordinal)) {
                    pIAT->u1.Function = reinterpret_cast<uintptr_t>(_GetProcAddress(hMag, reinterpret_cast<char*>(pThunk->u1.Ordinal & 0xFFFF)));
                } else {
                    auto* pImportName = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(base + pThunk->u1.AddressOfData);
                    pIAT->u1.Function = reinterpret_cast<uintptr_t>(_GetProcAddress(hMag, pImportName->Name));
                }
                pThunk++;
                pIAT++;
            }
            pImportDescr++;
        }
    }

    if (optHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].Size) {
        auto* pTLS = reinterpret_cast<PIMAGE_TLS_DIRECTORY>(base + optHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress);
        auto* pCallback = reinterpret_cast<PIMAGE_TLS_CALLBACK*>(pTLS->AddressOfCallBacks);
        for (; pCallback && *pCallback; pCallback++) {
            (*pCallback)(base, DLL_PROCESS_ATTACH, nullptr);
        }
    }

    auto _DllMain = reinterpret_cast<BOOL(WINAPI*)(HINSTANCE, DWORD, LPVOID)>(base + optHeader->AddressOfEntryPoint);
    _DllMain(reinterpret_cast<HINSTANCE>(base), DLL_PROCESS_ATTACH, nullptr);
}
static void __stdcall ShellcodeEnd() {}
#pragma runtime_checks("", restore)

void Injector::Obfuscate(std::vector<uint8_t>& data) {
    const uint8_t xorKey = 0x69;
    const int shift = 7;
    for (auto& byte : data) {
        byte = (byte ^ xorKey) + shift;
    }
}

void Injector::Deobfuscate(std::vector<uint8_t>& data) {
    const uint8_t xorKey = 0x69;
    const int shift = 7;
    for (auto& byte : data) {
        byte = (byte - shift) ^ xorKey;
    }
}

bool Injector::ManualMap(HANDLE hProcess, const std::vector<uint8_t>& dllData) {
    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)dllData.data();
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return false;

    PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((uintptr_t)dllData.data() + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return false;

    m_BaseAddress = (uintptr_t)VirtualAllocEx(hProcess, nullptr, ntHeaders->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!m_BaseAddress) return false;

    m_ImageSize = ntHeaders->OptionalHeader.SizeOfImage;

    if (!WriteProcessMemory(hProcess, (LPVOID)m_BaseAddress, dllData.data(), ntHeaders->OptionalHeader.SizeOfHeaders, nullptr)) {
        VirtualFreeEx(hProcess, (LPVOID)m_BaseAddress, 0, MEM_RELEASE);
        return false;
    }

    PIMAGE_SECTION_HEADER sectionHeader = IMAGE_FIRST_SECTION(ntHeaders);
    for (int i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++) {
        if (sectionHeader[i].SizeOfRawData) {
            if (!WriteProcessMemory(hProcess, (LPVOID)(m_BaseAddress + sectionHeader[i].VirtualAddress), (LPVOID)((uintptr_t)dllData.data() + sectionHeader[i].PointerToRawData), sectionHeader[i].SizeOfRawData, nullptr)) {
                VirtualFreeEx(hProcess, (LPVOID)m_BaseAddress, 0, MEM_RELEASE);
                return false;
            }
        }
    }

    MappingData data = { 0 };
    data.BaseAddress = (LPVOID)m_BaseAddress;
    data.LoadLibraryAFn = LoadLibraryA;
    data.GetProcAddressFn = GetProcAddress;

    LPVOID remoteData = VirtualAllocEx(hProcess, nullptr, sizeof(MappingData), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteData) {
        VirtualFreeEx(hProcess, (LPVOID)m_BaseAddress, 0, MEM_RELEASE);
        return false;
    }

    WriteProcessMemory(hProcess, remoteData, &data, sizeof(MappingData), nullptr);

    size_t shellcodeSize = (uintptr_t)ShellcodeEnd - (uintptr_t)Shellcode;
    LPVOID remoteShellcode = VirtualAllocEx(hProcess, nullptr, shellcodeSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!remoteShellcode) {
        VirtualFreeEx(hProcess, remoteData, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, (LPVOID)m_BaseAddress, 0, MEM_RELEASE);
        return false;
    }

    WriteProcessMemory(hProcess, remoteShellcode, Shellcode, shellcodeSize, nullptr);

    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0, (LPTHREAD_START_ROUTINE)remoteShellcode, remoteData, 0, nullptr);
    if (!hThread) {
        VirtualFreeEx(hProcess, remoteShellcode, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, remoteData, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, (LPVOID)m_BaseAddress, 0, MEM_RELEASE);
        return false;
    }

    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);

    std::vector<uint8_t> headerZeros(ntHeaders->OptionalHeader.SizeOfHeaders, 0);
    WriteProcessMemory(hProcess, (LPVOID)m_BaseAddress, headerZeros.data(), ntHeaders->OptionalHeader.SizeOfHeaders, nullptr);

    VirtualFreeEx(hProcess, remoteShellcode, 0, MEM_RELEASE);
    VirtualFreeEx(hProcess, remoteData, 0, MEM_RELEASE);

    m_TargetProcess = hProcess;
    return true;
}

bool Injector::InjectFromMemory(DWORD pid, const std::vector<uint8_t>& dllData) {
    if (dllData.empty()) return false;

    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) return false;

    std::vector<uint8_t> data = dllData;
    Deobfuscate(data);

    bool result = ManualMap(hProcess, data);

    Obfuscate(data);
    if (!result) CloseHandle(hProcess);

    return result;
}

bool Injector::InjectFromFile(DWORD pid, const std::wstring& filePath) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        return false;
    }
    file.close();

    Obfuscate(data);
    m_StoredDll = data;

    return InjectFromMemory(pid, data);
}

void Injector::Unload() {
    if (m_TargetProcess && m_BaseAddress && m_ImageSize) {
        std::vector<uint8_t> zeros(m_ImageSize, 0);
        WriteProcessMemory(m_TargetProcess, (LPVOID)m_BaseAddress, zeros.data(), m_ImageSize, nullptr);
        VirtualFreeEx(m_TargetProcess, (LPVOID)m_BaseAddress, 0, MEM_RELEASE);
        CloseHandle(m_TargetProcess);
    }
    m_TargetProcess = nullptr;
    m_BaseAddress = 0;
    m_ImageSize = 0;

    if (!m_StoredDll.empty()) {
        SecureZeroMemory(m_StoredDll.data(), m_StoredDll.size());
        m_StoredDll.clear();
    }
}
