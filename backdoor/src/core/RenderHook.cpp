#include "pch.h"
#include "RenderHook.h"
#include "../features/render/HUD.h"
#include "Bridge.h"

#include "../../../deps/minhook/MinHook.h"
#include "../../../deps/imgui/imgui.h"
#include "../../../deps/imgui/imgui_impl_opengl2.hpp"
#include "../../../deps/imgui/play_bold_font.h"
#include "../../../deps/imgui/play_regular_font.h"

#include <gl/GL.h>
#include <mutex>

static int(__stdcall* g_origWglSwapBuffers)(HDC) = nullptr;
static bool g_fontsInitialized = false;
static std::once_flag g_fontsOnce;
static ImFont* g_overlayRegularFont = nullptr;
static ImFont* g_overlayBoldFont = nullptr;

bool __stdcall wglSwapBuffersHook(HDC hdc)
{
    auto* config = Bridge::Get()->GetConfig();
    if (!config || config->m_Destruct) {
        return g_origWglSwapBuffers(hdc);
    }

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0) {
        return g_origWglSwapBuffers(hdc);
    }

    std::call_once(g_fontsOnce, []() {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;

        ImFontConfig fontCfg;
        fontCfg.FontDataOwnedByAtlas = false;
        g_overlayRegularFont = io.Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char*>(fonts::play_regular_data),
            fonts::play_regular_size,
            16.0f,
            &fontCfg
        );
        fontCfg.FontDataOwnedByAtlas = false;
        g_overlayBoldFont = io.Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char*>(fonts::play_bold_data),
            fonts::play_bold_size,
            16.0f,
            &fontCfg
        );

        if (g_overlayRegularFont) {
            io.FontDefault = g_overlayRegularFont;
        } else {
            io.FontDefault = io.Fonts->AddFontDefault();
            g_overlayRegularFont = io.FontDefault;
        }
        if (!g_overlayBoldFont) {
            g_overlayBoldFont = g_overlayRegularFont;
        }

        HUD::Get()->SetFonts(g_overlayRegularFont, g_overlayBoldFont);
        ImGui_ImplOpenGL2_Init();
        g_fontsInitialized = true;
    });

    if (!g_fontsInitialized) {
        return g_origWglSwapBuffers(hdc);
    }

    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)viewport[2], (float)viewport[3]);

    ImGui_ImplOpenGL2_NewFrame();
    ImGui::NewFrame();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f });
    ImGui::Begin("##overlay", nullptr,
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse);
    {
        ImGui::SetWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetWindowSize(ImVec2((float)viewport[2], (float)viewport[3]), ImGuiCond_Always);

        HUD::Get()->Render(config, (float)viewport[2], (float)viewport[3]);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);

    ImGui::Render();
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

    return g_origWglSwapBuffers(hdc);
}

bool RenderHook::Initialize() {
    if (m_Installed) return true;

    void* pSwapBuffers = (void*)GetProcAddress(GetModuleHandleW(L"opengl32.dll"), "wglSwapBuffers");
    if (!pSwapBuffers) return false;

    if (MH_CreateHook(pSwapBuffers, (LPVOID)wglSwapBuffersHook, (void**)&g_origWglSwapBuffers) != MH_OK) {
        return false;
    }

    if (MH_EnableHook(pSwapBuffers) != MH_OK) {
        MH_RemoveHook(pSwapBuffers);
        return false;
    }

    m_Installed = true;
    return true;
}

void RenderHook::Shutdown() {
    if (!m_Installed) return;

    MH_DisableHook(MH_ALL_HOOKS);

    if (g_fontsInitialized) {
        HUD::Get()->SetFonts(nullptr, nullptr);
        ImGui_ImplOpenGL2_Shutdown();
        ImGui::DestroyContext();
        g_overlayRegularFont = nullptr;
        g_overlayBoldFont = nullptr;
        g_fontsInitialized = false;
    }

    m_Installed = false;
}
