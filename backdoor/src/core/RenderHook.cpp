#include "pch.h"
#include "RenderHook.h"
#include "../features/render/HUD.h"
#include "Bridge.h"

static RenderHook* g_HookInstance = nullptr;

LONG CALLBACK RenderHook::VehHandler(PEXCEPTION_POINTERS pExInfo) {
    if (!g_HookInstance || !g_HookInstance->m_Installed)
        return EXCEPTION_CONTINUE_SEARCH;
    
    if (pExInfo->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP) {
        if (g_HookInstance->m_SingleStepping) {
            g_HookInstance->m_SingleStepping = false;
            DWORD oldProt;
            VirtualProtect(g_HookInstance->m_TargetAddress, 1, PAGE_EXECUTE_READWRITE, &oldProt);
            *(uint8_t*)g_HookInstance->m_TargetAddress = 0xCC;
            VirtualProtect(g_HookInstance->m_TargetAddress, 1, oldProt, &oldProt);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
    
    if (pExInfo->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT) {
        if ((void*)pExInfo->ContextRecord->Rip == g_HookInstance->m_TargetAddress) {
            __try {
                HDC hdc = wglGetCurrentDC();
                if (hdc) {
                    g_HookInstance->OnRender(hdc);
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
            }
            
            DWORD oldProt;
            VirtualProtect(g_HookInstance->m_TargetAddress, 1, PAGE_EXECUTE_READWRITE, &oldProt);
            *(uint8_t*)g_HookInstance->m_TargetAddress = g_HookInstance->m_OriginalByte;
            VirtualProtect(g_HookInstance->m_TargetAddress, 1, oldProt, &oldProt);
            FlushInstructionCache(GetCurrentProcess(), g_HookInstance->m_TargetAddress, 1);
            
            g_HookInstance->m_SingleStepping = true;
            pExInfo->ContextRecord->EFlags |= 0x100; // TF (trap flag)
            
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    
    return EXCEPTION_CONTINUE_SEARCH;
}

bool RenderHook::Initialize() {
    HMODULE opengl = GetModuleHandleW(L"opengl32.dll");
    if (!opengl) {
        opengl = LoadLibraryW(L"opengl32.dll");
        if (!opengl) return false;
    }
    
    m_TargetAddress = (void*)GetProcAddress(opengl, "wglSwapBuffers");
    if (!m_TargetAddress) return false;
    
    g_HookInstance = this;
    
    m_VehHandle = AddVectoredExceptionHandler(1, VehHandler);
    if (!m_VehHandle) return false;
    
    m_OriginalByte = *(uint8_t*)m_TargetAddress;

    DWORD oldProtect;
    if (!VirtualProtect(m_TargetAddress, 1, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        RemoveVectoredExceptionHandler(m_VehHandle);
        m_VehHandle = nullptr;
        return false;
    }

    *(uint8_t*)m_TargetAddress = 0xCC;
    
    VirtualProtect(m_TargetAddress, 1, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), m_TargetAddress, 1);
    
    m_Installed = true;
    return true;
}

void RenderHook::Shutdown() {
    if (!m_Installed) return;

    if (m_TargetAddress) {
        DWORD oldProtect;
        if (VirtualProtect(m_TargetAddress, 1, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            *(uint8_t*)m_TargetAddress = m_OriginalByte;
            VirtualProtect(m_TargetAddress, 1, oldProtect, &oldProtect);
            FlushInstructionCache(GetCurrentProcess(), m_TargetAddress, 1);
        }
    }
    
    if (m_VehHandle) {
        RemoveVectoredExceptionHandler(m_VehHandle);
        m_VehHandle = nullptr;
    }
    
    m_Installed = false;
    g_HookInstance = nullptr;
}

void RenderHook::OnRender(HDC hdc) {
    auto* config = Bridge::Get()->GetConfig();
    if (!config) return;
    
    HUD::Get()->Render(hdc, config);
}
