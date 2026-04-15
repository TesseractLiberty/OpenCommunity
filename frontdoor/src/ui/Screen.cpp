#include "pch.h"
#include "Screen.h"
#include "../core/Bridge.h"
#include "../core/Injector.h"
#include "../utils/ProcessHelper.h"
#include "../config/ClientInfo.h"
#include "../config/ModuleConfig.h"
#include "../../vendors/imgui/colors.h"
#include "../../vendors/imgui/inter_bold_font.h"
#include "../../vendors/imgui/inter_regular_font.h"
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace
{
    float Clamp01(float value)
    {
        return (value < 0.0f) ? 0.0f : ((value > 1.0f) ? 1.0f : value);
    }

    float EaseOutCubic(float t)
    {
        const float clamped = Clamp01(t);
        const float inv = 1.0f - clamped;
        return 1.0f - inv * inv * inv;
    }

    float EaseInOutCubic(float t)
    {
        const float clamped = Clamp01(t);
        return (clamped < 0.5f)
            ? 4.0f * clamped * clamped * clamped
            : 1.0f - powf(-2.0f * clamped + 2.0f, 3.0f) * 0.5f;
    }

    float EaseOutBack(float t)
    {
        const float clamped = Clamp01(t);
        const float c1 = 1.70158f;
        const float c3 = c1 + 1.0f;
        const float x = clamped - 1.0f;
        return 1.0f + c3 * x * x * x + c1 * x * x;
    }

    float ProgressRange(float elapsed, float start, float duration)
    {
        if (duration <= 0.0f)
            return elapsed >= start ? 1.0f : 0.0f;
        return Clamp01((elapsed - start) / duration);
    }

    ImVec2 CalcTextSizeWithFont(ImFont* font, const char* text, float fontSize)
    {
        if (!font)
            return ImGui::CalcTextSize(text);
        return font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
    }

    void ApplyRoundedWindowRegion(HWND hwnd, int width, int height, int radius)
    {
        if (!hwnd || width <= 0 || height <= 0)
            return;

        HRGN region = CreateRectRgn(0, 0, width + 1, height + 1);
        if (region) {
            SetWindowRgn(hwnd, region, TRUE);
        }
    }

    void DrawWindowBase(ImDrawList* drawList, const ImVec2& origin, float width, float height)
    {
        const ImVec2 surfaceMin(origin.x + 0.5f, origin.y + 0.5f);
        const ImVec2 surfaceMax(origin.x + width - 0.5f, origin.y + height - 0.5f);
        drawList->AddRectFilled(surfaceMin, surfaceMax, color::GetBackgroundU32(), 0.0f);
    }

    void DrawTopographicLoop(ImDrawList* drawList, const ImVec2& center, float radiusX, float radiusY, float phase, ImU32 color, float thickness)
    {
        const int samples = 96;
        const float twoPi = 6.28318530718f;

        drawList->PathClear();
        for (int i = 0; i < samples; ++i) {
            const float angle = (static_cast<float>(i) / static_cast<float>(samples)) * twoPi;
            const float wobble = 1.0f
                + 0.055f * sinf(angle * 3.0f + phase)
                + 0.028f * cosf(angle * 5.0f - phase * 0.7f);

            drawList->PathLineTo(ImVec2(
                center.x + cosf(angle) * radiusX * wobble,
                center.y + sinf(angle) * radiusY * wobble
            ));
        }

        drawList->PathStroke(color, true, thickness);
    }

    void DrawTopographicGroup(ImDrawList* drawList, const ImVec2& center, float radiusX, float radiusY, int rings, float spacing, float phase, const ImVec4& tint)
    {
        for (int ring = 0; ring < rings; ++ring) {
            const float ringPhase = phase + ring * 0.18f;
            const float alpha = tint.w * (1.0f - (static_cast<float>(ring) / static_cast<float>(rings + 2)));
            DrawTopographicLoop(
                drawList,
                center,
                radiusX + ring * spacing,
                radiusY + ring * spacing * 0.82f,
                ringPhase,
                ImGui::ColorConvertFloat4ToU32(ImVec4(tint.x, tint.y, tint.z, alpha)),
                1.0f
            );
        }
    }

    void DrawTopographicBackground(ImDrawList* drawList, const ImVec2& origin, float width, float height, float elapsed)
    {
        const float phase = elapsed * 0.14f;
        const ImVec2 clipMin(origin.x + 10.0f, origin.y + 10.0f);
        const ImVec2 clipMax(origin.x + width - 10.0f, origin.y + height - 10.0f);
        drawList->PushClipRect(clipMin, clipMax, true);
        DrawTopographicGroup(drawList, ImVec2(origin.x + width * 0.15f, origin.y + height * 0.28f), 48.0f, 40.0f, 8, 18.0f, phase + 0.2f, color::GetModuleAltTextVec4(0.22f));
        DrawTopographicGroup(drawList, ImVec2(origin.x + width * 0.83f, origin.y + height * 0.20f), 66.0f, 58.0f, 9, 19.0f, phase + 1.1f, color::GetModuleTextVec4(0.18f));
        DrawTopographicGroup(drawList, ImVec2(origin.x + width * 0.17f, origin.y + height * 0.77f), 42.0f, 30.0f, 7, 15.0f, phase + 2.2f, color::GetModuleAltTextVec4(0.16f));
        DrawTopographicGroup(drawList, ImVec2(origin.x + width * 0.56f, origin.y + height * 0.54f), 115.0f, 74.0f, 4, 26.0f, phase + 0.7f, color::GetModuleTextVec4(0.10f));
        drawList->PopClipRect();
    }

    void DrawGlassCard(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, float elapsed, float rounding = 28.0f)
    {
        const float width = max.x - min.x;
        const float height = max.y - min.y;
        if (width <= 0.0f || height <= 0.0f)
            return;

        drawList->AddRectFilled(ImVec2(min.x, min.y + 14.0f), ImVec2(max.x, max.y + 14.0f), color::GetGlassShadowU32(0.11f), rounding);

        drawList->PushClipRect(min, max, true);
        DrawTopographicGroup(drawList, ImVec2(min.x + width * 0.20f, min.y + height * 0.28f), width * 0.18f, height * 0.16f, 5, 12.0f, elapsed * 0.16f + 0.4f, color::GetGlassStrongVec4(0.10f));
        DrawTopographicGroup(drawList, ImVec2(min.x + width * 0.76f, min.y + height * 0.35f), width * 0.16f, height * 0.13f, 5, 10.0f, elapsed * 0.18f + 1.2f, color::GetGlassSoftVec4(0.08f));
        drawList->PopClipRect();

        drawList->AddRectFilled(min, max, color::GetPanelU32(0.72f), rounding);
        drawList->AddRectFilled(ImVec2(min.x + 1.0f, min.y + 1.0f), ImVec2(max.x - 1.0f, min.y + height * 0.42f), color::GetGlassHighlightU32(0.18f), rounding, ImDrawFlags_RoundCornersTop);
        drawList->AddRect(min, max, color::GetBorderU32(0.40f), rounding, 0, 1.0f);
        drawList->AddRect(ImVec2(min.x + 1.0f, min.y + 1.0f), ImVec2(max.x - 1.0f, max.y - 1.0f), color::GetGlassHighlightU32(0.26f), rounding - 1.0f, 0, 1.0f);
    }

    void DrawCloseButton(float width, bool& running)
    {
        ImGui::SetCursorPos(ImVec2(width - 60.0f, 26.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, color::GetPanelVec4(0.76f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color::GetPanelHoverVec4(0.84f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, color::GetPanelActiveVec4(0.92f));
        ImGui::PushStyleColor(ImGuiCol_Text, color::GetStrongTextVec4());
        if (ImGui::Button("X", ImVec2(34.0f, 34.0f))) {
            running = false;
        }
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar();
    }

    float EaseBreathing(float t)
    {
        return 0.5f + 0.5f * sinf(t);
    }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static Screen* g_Instance = nullptr;

Screen::Screen(float width, float height) 
    : m_Width(width), m_Height(height) {
    g_Instance = this;
}

Screen::~Screen() {
    Shutdown();
    g_Instance = nullptr;
}

LRESULT WINAPI Screen::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_Instance) {
        return g_Instance->HandleWndProc(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT Screen::HandleWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
        return true;
    }
    
    switch (msg) {
    case WM_SIZE:
        if (m_Device && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            m_SwapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
            ApplyRoundedWindowRegion(hwnd, LOWORD(lParam), HIWORD(lParam), 42);
        }
        if (wParam == SIZE_MINIMIZED) {
            m_Minimized = true;
        } else {
            m_Minimized = false;
        }
        return 0;
        
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_CLOSE) {
            m_Running = false;
            return 0;
        }
        if ((wParam & 0xfff0) == SC_KEYMENU) {
            return 0;
        }
        break;
        
    case WM_CLOSE:
    case WM_DESTROY:
        m_Running = false;
        PostQuitMessage(0);
        return 0;
    }
    
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool Screen::CreateDeviceD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    
    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &m_SwapChain, &m_Device, &featureLevel, &m_Context) != S_OK) {
        return false;
    }
    
    CreateRenderTarget();
    return true;
}

void Screen::CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (m_SwapChain) { m_SwapChain->Release(); m_SwapChain = nullptr; }
    if (m_Context) { m_Context->Release(); m_Context = nullptr; }
    if (m_Device) { m_Device->Release(); m_Device = nullptr; }
}

void Screen::CreateRenderTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    m_SwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (backBuffer) {
        m_Device->CreateRenderTargetView(backBuffer, nullptr, &m_RenderTarget);
        backBuffer->Release();
    }
}

void Screen::CleanupRenderTarget() {
    if (m_RenderTarget) {
        m_RenderTarget->Release();
        m_RenderTarget = nullptr;
    }
}

void Screen::SetupImGuiStyle() {
    ImGuiStyle* style = &ImGui::GetStyle();
    ImVec4* colors = style->Colors;

    style->WindowRounding = 0.0f;
    style->FrameRounding = 0.0f;
    style->PopupRounding = 0.0f;
    style->GrabRounding = 0.0f;
    style->TabRounding = 0.0f;
    style->ChildRounding = 0.0f;
    style->ScrollbarRounding = 0.0f;

    style->IndentSpacing = 12.0f;
    style->ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style->ItemSpacing = ImVec2(10.0f, 10.0f);
    style->FramePadding = ImVec2(14.0f, 9.0f);
    style->WindowPadding = ImVec2(18.0f, 18.0f);

    style->WindowBorderSize = 0.0f;
    style->ChildBorderSize = 0.0f;
    style->FrameBorderSize = 1.0f;
    style->PopupBorderSize = 1.0f;

    colors[ImGuiCol_Text] = color::GetStrongTextVec4();
    colors[ImGuiCol_TextDisabled] = color::GetMutedTextVec4();
    colors[ImGuiCol_WindowBg] = color::GetBackgroundVec4();
    colors[ImGuiCol_ChildBg] = color::GetPanelVec4(0.44f);
    colors[ImGuiCol_PopupBg] = color::GetPanelVec4(0.84f);
    colors[ImGuiCol_Border] = color::GetBorderVec4(0.34f);
    colors[ImGuiCol_BorderShadow] = color::GetTransparentVec4();
    colors[ImGuiCol_FrameBg] = color::GetFieldBgVec4(0.68f);
    colors[ImGuiCol_FrameBgHovered] = color::GetFieldHoverVec4(0.78f);
    colors[ImGuiCol_FrameBgActive] = color::GetFieldActiveVec4(0.88f);
    colors[ImGuiCol_TitleBg] = color::GetBackgroundVec4();
    colors[ImGuiCol_TitleBgActive] = color::GetBackgroundVec4();
    colors[ImGuiCol_TitleBgCollapsed] = color::GetBackgroundVec4(0.95f);
    colors[ImGuiCol_MenuBarBg] = color::GetPanelVec4(0.32f);
    colors[ImGuiCol_ScrollbarBg] = color::GetTransparentVec4();
    colors[ImGuiCol_ScrollbarGrab] = color::GetPanelVec4(0.72f);
    colors[ImGuiCol_ScrollbarGrabHovered] = color::GetPanelHoverVec4(0.82f);
    colors[ImGuiCol_ScrollbarGrabActive] = color::GetPanelActiveVec4(0.92f);
    colors[ImGuiCol_CheckMark] = color::GetStrongTextVec4();
    colors[ImGuiCol_SliderGrab] = color::GetStrongTextVec4();
    colors[ImGuiCol_SliderGrabActive] = color::GetModuleAltTextVec4();
    colors[ImGuiCol_Button] = color::GetPanelVec4(0.72f);
    colors[ImGuiCol_ButtonHovered] = color::GetPanelHoverVec4(0.82f);
    colors[ImGuiCol_ButtonActive] = color::GetPanelActiveVec4(0.90f);
    colors[ImGuiCol_Header] = color::GetPanelHoverVec4(0.64f);
    colors[ImGuiCol_HeaderHovered] = color::GetPanelActiveVec4(0.76f);
    colors[ImGuiCol_HeaderActive] = color::GetPanelActiveVec4(0.88f);
    colors[ImGuiCol_Separator] = color::GetBorderVec4(0.38f);
    colors[ImGuiCol_ResizeGrip] = color::GetTransparentVec4();
    colors[ImGuiCol_ResizeGripHovered] = color::GetPanelHoverVec4(0.70f);
    colors[ImGuiCol_ResizeGripActive] = color::GetPanelActiveVec4(0.80f);
    colors[ImGuiCol_Tab] = color::GetPanelVec4(0.58f);
    colors[ImGuiCol_TabHovered] = color::GetPanelHoverVec4(0.76f);
    colors[ImGuiCol_TabActive] = color::GetPanelActiveVec4(0.84f);
    colors[ImGuiCol_TabUnfocused] = color::GetPanelVec4(0.44f);
    colors[ImGuiCol_TabUnfocusedActive] = color::GetPanelHoverVec4(0.58f);
    colors[ImGuiCol_TextSelectedBg] = color::GetGlassStrongVec4(0.35f);
    colors[ImGuiCol_ModalWindowDimBg] = color::GetBackgroundVec4(0.35f);
}

bool Screen::Initialize() {
    ShowWindow(GetConsoleWindow(), SW_HIDE);
    m_Wc = { sizeof(m_Wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"OpenCommunity", nullptr };
    RegisterClassExW(&m_Wc);
    
    int screenX = GetSystemMetrics(SM_CXSCREEN);
    int screenY = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenX - static_cast<int>(m_Width)) / 2;
    int y = (screenY - static_cast<int>(m_Height)) / 2;
    
    m_Hwnd = CreateWindowExW(
        0,
        m_Wc.lpszClassName,
        L"OpenCommunity",
        WS_POPUP,
        x, y,
        static_cast<int>(m_Width), static_cast<int>(m_Height),
        nullptr, nullptr, m_Wc.hInstance, nullptr
    );
    
    if (!m_Hwnd) return false;
    ApplyRoundedWindowRegion(m_Hwnd, static_cast<int>(m_Width), static_cast<int>(m_Height), 42);
    
    if (!CreateDeviceD3D(m_Hwnd)) {
        DestroyWindow(m_Hwnd);
        UnregisterClassW(m_Wc.lpszClassName, m_Wc.hInstance);
        return false;
    }
    
    ShowWindow(m_Hwnd, SW_SHOWDEFAULT);
    UpdateWindow(m_Hwnd);
    
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // disable imgui.ini

    {
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;
        m_FontBody = io.Fonts->AddFontFromMemoryTTF((void*)fonts::inter_regular_data, fonts::inter_regular_size, 17.0f, &cfg);
        cfg.FontDataOwnedByAtlas = false;
        m_FontBold = io.Fonts->AddFontFromMemoryTTF((void*)fonts::inter_bold_data, fonts::inter_bold_size, 17.0f, &cfg);
        cfg.FontDataOwnedByAtlas = false;
        m_FontTitle = io.Fonts->AddFontFromMemoryTTF((void*)fonts::inter_bold_data, fonts::inter_bold_size, 32.0f, &cfg);
        cfg.FontDataOwnedByAtlas = false;
        m_FontHero = io.Fonts->AddFontFromMemoryTTF((void*)fonts::inter_bold_data, fonts::inter_bold_size, 110.0f, &cfg);
        cfg.FontDataOwnedByAtlas = false;
        m_FontBodyLarge = io.Fonts->AddFontFromMemoryTTF((void*)fonts::inter_regular_data, fonts::inter_regular_size, 32.0f, &cfg);
        cfg.FontDataOwnedByAtlas = false;
        m_FontBodyMed = io.Fonts->AddFontFromMemoryTTF((void*)fonts::inter_regular_data, fonts::inter_regular_size, 22.0f, &cfg);
        cfg.FontDataOwnedByAtlas = false;
        m_FontBoldMed = io.Fonts->AddFontFromMemoryTTF((void*)fonts::inter_bold_data, fonts::inter_bold_size, 22.0f, &cfg);
        if (m_FontBody) io.FontDefault = m_FontBody;
    }
    if (!io.FontDefault) {
        io.FontDefault = io.Fonts->AddFontDefault();
    }
    if (!m_FontBody) {
        m_FontBody = io.FontDefault;
    }
    if (!m_FontBold) {
        m_FontBold = m_FontBody;
    }
    if (!m_FontTitle) {
        m_FontTitle = m_FontBold;
    }
    if (!m_FontHero) {
        m_FontHero = m_FontTitle;
    }
    
    SetupImGuiStyle();
    
    ImGui_ImplWin32_Init(m_Hwnd);
    ImGui_ImplDX11_Init(m_Device, m_Context);
    
    m_Initialized = true;
    return true;
}

void Screen::Shutdown() {
    if (!m_Initialized) return;
    
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    
    CleanupDeviceD3D();
    DestroyWindow(m_Hwnd);
    UnregisterClassW(m_Wc.lpszClassName, m_Wc.hInstance);
    
    m_Initialized = false;
}

void Screen::RenderIntro() {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(m_Width, m_Height));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground;

    if (ImGui::Begin("OpenCommunityIntro", nullptr, flags)) {
        if (m_IntroStartTime < 0.0f) {
            m_IntroStartTime = static_cast<float>(ImGui::GetTime());
        }

        const float elapsed = static_cast<float>(ImGui::GetTime()) - m_IntroStartTime;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos();

        DrawWindowBase(dl, wp, m_Width, m_Height);

        const float line1FontSize = 32.0f;
        const float line2FontSize = 22.0f;
        const float charsPerSec = 30.0f;

        const char* line1Full = "Hey! I'm Lopes.";
        const int line1Len = 17;
        const float line1Start = 0.3f;

        const char* line2Full = "Welcome to our open-source client, shaped by features the community really cares about.";
        const int line2Len = 88;
        const float line2Start = line1Start + (float)line1Len / charsPerSec + 0.3f;

        const int line1Chars = (int)fminf((float)line1Len, fmaxf(0.0f, (elapsed - line1Start) * charsPerSec));
        const int line2Chars = (int)fminf((float)line2Len, fmaxf(0.0f, (elapsed - line2Start) * charsPerSec));
        auto calcLineWidth = [&](auto* segs, int segCount, float fontSize, int totalChars) -> float {
            float w = 0.0f;
            int drawn = 0;
            for (int i = 0; i < segCount; i++) {
                int segLen = (int)strlen(segs[i].text);
                int segChars = (int)fminf((float)segLen, fmaxf(0.0f, (float)(totalChars - drawn)));
                if (segChars <= 0) break;
                char buf[128] = {};
                memcpy(buf, segs[i].text, segChars);
                buf[segChars] = '\0';
                ImFont* font = (fontSize > 35.0f) ? (segs[i].bold ? m_FontTitle : m_FontBodyLarge) : (segs[i].bold ? m_FontBoldMed : m_FontBodyMed);
                w += CalcTextSizeWithFont(font, buf, fontSize).x;
                drawn += segChars;
            }
            return w;
        };

        struct TextSegment { const char* text; bool bold; };

        const float line1Y = wp.y + 60.0f;
        const float line2Y = wp.y + 60.0f + line1FontSize + 30.0f;
        if (line1Chars > 0) {
            TextSegment segs1[] = {
                { "Hey! I'm ", false },
                { "Lopes", true },
                { ".", false }
            };

            float line1W = calcLineWidth(segs1, 3, line1FontSize, line1Chars);
            float curX = wp.x + (m_Width - line1W) * 0.5f;
            int charsDrawn = 0;
            for (auto& seg : segs1) {
                int segLen = (int)strlen(seg.text);
                int segChars = (int)fminf((float)segLen, fmaxf(0.0f, (float)(line1Chars - charsDrawn)));
                if (segChars <= 0) break;

                char buf[64] = {};
                memcpy(buf, seg.text, segChars);
                buf[segChars] = '\0';

                ImFont* font = seg.bold ? m_FontTitle : m_FontBodyLarge;
                dl->AddText(font, line1FontSize, ImVec2(curX, line1Y), color::GetAccentU32(1.0f), buf);

                ImVec2 fullSz = CalcTextSizeWithFont(font, buf, line1FontSize);
                curX += fullSz.x;
                charsDrawn += segChars;
            }

            if (line1Chars < line1Len) {
                float blinkAlpha = (sinf(elapsed * 6.0f) > 0.0f) ? 0.8f : 0.0f;
                dl->AddLine(ImVec2(curX + 2.0f, line1Y + 2.0f), ImVec2(curX + 2.0f, line1Y + line1FontSize - 2.0f), color::GetAccentU32(blinkAlpha), 1.5f);
            }
        }

        if (line2Chars > 0) {
            TextSegment segs2[] = {
                { "Welcome to our ", false },
                { "open-source", true },
                { " client, shaped by ", false },
                { "features", true },
                { " the community ", false },
                { "really cares about", true },
                { ".", false }
            };

            float line2W = calcLineWidth(segs2, 7, line2FontSize, line2Chars);
            float curX = wp.x + (m_Width - line2W) * 0.5f;
            int charsDrawn = 0;
            for (auto& seg : segs2) {
                int segLen = (int)strlen(seg.text);
                int segChars = (int)fminf((float)segLen, fmaxf(0.0f, (float)(line2Chars - charsDrawn)));
                if (segChars <= 0) break;

                char buf[128] = {};
                memcpy(buf, seg.text, segChars);
                buf[segChars] = '\0';

                ImFont* font = seg.bold ? m_FontBoldMed : m_FontBodyMed;
                dl->AddText(font, line2FontSize, ImVec2(curX, line2Y), color::GetAccentU32(1.0f), buf);

                ImVec2 fullSz = CalcTextSizeWithFont(font, buf, line2FontSize);
                curX += fullSz.x;
                charsDrawn += segChars;
            }

            if (line2Chars < line2Len) {
                float blinkAlpha = (sinf(elapsed * 6.0f) > 0.0f) ? 0.8f : 0.0f;
                dl->AddLine(ImVec2(curX + 2.0f, line2Y + 2.0f), ImVec2(curX + 2.0f, line2Y + line2FontSize - 2.0f), color::GetAccentU32(blinkAlpha), 1.5f);
            }
        }

        const float totalAnimTime = line2Start + (float)line2Len / charsPerSec + 0.5f;
        const bool showButton = elapsed >= totalAnimTime;
        if (showButton) {
            const char* btnLabel = "Let's go";
            ImGui::PushFont(m_FontTitle);
            
            const float breathTime = static_cast<float>(ImGui::GetTime()) * 2.5f;
            const float breathScale = 0.92f + 0.08f * sinf(breathTime);
            
            ImVec2 baseSize = ImGui::CalcTextSize(btnLabel);
            float scaledFontSize = 32.0f * breathScale;
            ImVec2 scaledSize = CalcTextSizeWithFont(m_FontTitle, btnLabel, scaledFontSize);
            
            float btnX = (m_Width - scaledSize.x) * 0.5f;
            float btnY = m_Height - scaledSize.y - 40.0f;

            ImGui::SetCursorPos(ImVec2((m_Width - baseSize.x) * 0.5f, m_Height - baseSize.y - 40.0f));
            if (ImGui::InvisibleButton("##letsgo", baseSize)) {
                m_State = AppState::InstanceChooser;
            }
            const bool hovered = ImGui::IsItemHovered();

            ImVec4 btnColor = hovered ? color::GetSecondaryTextVec4() : color::GetStrongTextVec4();
            dl->AddText(m_FontTitle, scaledFontSize, ImVec2(wp.x + btnX, wp.y + btnY), ImGui::ColorConvertFloat4ToU32(btnColor), btnLabel);
            
            ImGui::PopFont();
        }

        ImGui::End();
    }
}

void Screen::RenderInstanceChooser() {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(m_Width, m_Height));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground;

    if (ImGui::Begin("InstanceChooser", nullptr, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();

        DrawWindowBase(dl, wp, m_Width, m_Height);

        {
            const char* title = "Select Instance";
            ImGui::PushFont(m_FontBody);
            ImVec2 tSz = ImGui::CalcTextSize(title);
            ImGui::SetCursorPos(ImVec2((m_Width - tSz.x) * 0.5f, 60.f));
            ImGui::TextColored(color::GetStrongTextVec4(0.96f), "%s", title);
            ImGui::PopFont();
        }
        
        {
            const char* sub = "Choose your Minecraft window";
            ImGui::PushFont(m_FontBody);
            ImVec2 sSz = ImGui::CalcTextSize(sub);
            ImGui::SetCursorPos(ImVec2((m_Width - sSz.x) * 0.5f, 95.f));
            ImGui::TextColored(color::GetSecondaryTextVec4(0.92f), "%s", sub);
            ImGui::PopFont();
        }
        
        std::vector<std::pair<HWND, std::string>> instances;
        for (HWND hwnd = GetTopWindow(NULL); hwnd != NULL; hwnd = GetNextWindow(hwnd, GW_HWNDNEXT)) {
            if (!IsWindowVisible(hwnd)) continue;
            int length = GetWindowTextLength(hwnd);
            if (length == 0) continue;
            
            CHAR cName[MAX_PATH];
            GetClassNameA(hwnd, cName, _countof(cName));
            if (strcmp(cName, "LWJGL") != 0 && strcmp(cName, "GLFW30") != 0)
                continue;
            
            std::vector<char> title(length + 1);
            GetWindowTextA(hwnd, title.data(), length + 1);
            instances.push_back({ hwnd, std::string(title.data()) });
        }
        
        if (instances.empty()) {
            const char* w = "Waiting for Minecraft...";
            ImGui::PushFont(m_FontBody);
            ImVec2 wSz = ImGui::CalcTextSize(w);
            ImGui::SetCursorPos(ImVec2((m_Width - wSz.x) * 0.5f, m_Height * 0.5f - 10.f));
            ImGui::TextColored(color::GetModuleAltTextVec4(0.95f), "%s", w);
            ImGui::PopFont();
        } else {
            float btnY = m_Height * 0.5f + 30.f - (float)instances.size() * 23.f;
            for (const auto& inst : instances) {
                ImGui::SetCursorPos(ImVec2((m_Width - 320.f) * 0.5f, btnY));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, color::GetPanelVec4());
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color::GetPanelHoverVec4());
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, color::GetPanelActiveVec4());
                ImGui::PushStyleColor(ImGuiCol_Text, color::GetStrongTextVec4());
                
                if (ImGui::Button(inst.second.c_str(), ImVec2(320.f, 36.f))) {
                    DWORD pid = 0;
                    GetWindowThreadProcessId(inst.first, &pid);
                    if (pid != 0) {
                        Singleton<ClientInfo>::Get()->m_TargetPid = pid;
                        m_State = AppState::Injecting;
                        m_InjectionDone = false;
                        m_InjectionFailed = false;
                        m_InjectionStatus = "Injecting...";
                        
                        std::thread([this, pid]() {
                            Bridge::Get()->Initialize();
                            
                            wchar_t exePath[MAX_PATH] = {};
                            GetModuleFileNameW(nullptr, exePath, MAX_PATH);
                            std::wstring dllPath(exePath);
                            dllPath = dllPath.substr(0, dllPath.find_last_of(L"\\/") + 1);
                            dllPath += L"backdoor.dll";

                            m_InjectionStatus = "Attaching to process...";
                            Sleep(300);

                            bool success = Injector::Get()->InjectFromFile(pid, dllPath);
                            
                            if (success) {
                                m_InjectionStatus = "Injection complete!";
                                m_InjectionDone = true;
                                Sleep(1000);
                                m_State = AppState::MainInterface;
                            } else {
                                m_InjectionStatus = "Injection failed! Check if backdoor.dll exists.";
                                m_InjectionFailed = true;
                                Sleep(3000);
                                m_State = AppState::InstanceChooser;
                            }
                        }).detach();
                    }
                }
                
                ImGui::PopStyleColor(4);
                ImGui::PopStyleVar();
                btnY += 46.f;
            }
        }
        
        ImGui::End();
    }
}

void Screen::RenderInjecting() {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(m_Width, m_Height));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground;

    if (ImGui::Begin("Injecting", nullptr, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        DrawWindowBase(dl, wp, m_Width, m_Height);

        {
        const char* title = "Injecting";
        ImGui::PushFont(m_FontBody);
        ImVec2 tSz = ImGui::CalcTextSize(title);
        ImGui::SetCursorPos(ImVec2((m_Width - tSz.x) * 0.5f, m_Height * 0.35f));
        ImGui::TextColored(color::GetStrongTextVec4(0.96f), "%s", title);
        
        ImVec2 sSz = ImGui::CalcTextSize(m_InjectionStatus.c_str());
        ImGui::SetCursorPos(ImVec2((m_Width - sSz.x) * 0.5f, m_Height * 0.35f + 35.f));
        if (m_InjectionFailed) {
            ImGui::TextColored(color::GetModuleTextVec4(0.95f), "%s", m_InjectionStatus.c_str());
        } else if (m_InjectionDone) {
            ImGui::TextColored(color::GetStrongTextVec4(0.95f), "%s", m_InjectionStatus.c_str());
        } else {
            ImGui::TextColored(color::GetSecondaryTextVec4(0.92f), "%s", m_InjectionStatus.c_str());
        }
        ImGui::PopFont();

        ImGui::End();
    }
}

void Screen::RenderHUDPreview() {
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    float screenW = ImGui::GetIO().DisplaySize.x;
    auto* config = Bridge::Get()->GetConfig();
    const bool animatedPalette = config && config->HUD.m_Rainbow;

    const char* modules[] = { "hud", "aim assist", "wtap", "backtrack" };
    int count = 4;

    float y = 50.0f;
    for (int i = 0; i < count; i++) {
        ImVec2 size = ImGui::CalcTextSize(modules[i]);
        float x = screenW - size.x - 20.0f;

        ImU32 color = animatedPalette
            ? color::GetCycleU32(static_cast<float>(ImGui::GetTime()) * 0.15f + i * 0.1f, 0.96f)
            : (i % 3 == 0 ? color::GetAccentU32(0.96f) : (i % 3 == 1 ? color::GetModuleTextU32(0.96f) : color::GetModuleAltTextU32(0.96f)));

        draw->AddText(ImVec2(x, y), color, modules[i]);
        y += size.y + 5.0f;
    }
}

void Screen::RenderCombatTab() {
    ImGui::TextColored(color::GetStrongTextVec4(), "combat");
    ImGui::Separator();

    auto* config = Bridge::Get()->GetConfig();
    if (!config) {
        ImGui::TextColored(color::GetModuleAltTextVec4(), "waiting for injection...");
        return;
    }

    // auto clicker
    ImGui::Checkbox("auto clicker", &config->AutoClicker.m_Enabled);
    if (config->AutoClicker.m_Enabled) {
        ImGui::Indent(15.f);
        ImGui::SliderInt("min cps", &config->AutoClicker.m_MinCps, 1, 20);
        ImGui::SliderInt("max cps", &config->AutoClicker.m_MaxCps, 1, 20);
        if (config->AutoClicker.m_MinCps > config->AutoClicker.m_MaxCps)
            config->AutoClicker.m_MinCps = config->AutoClicker.m_MaxCps;
        ImGui::Checkbox("jitter", &config->AutoClicker.m_Jitter);
        ImGui::Checkbox("only while holding LMB", &config->AutoClicker.m_OnlyWhileHolding);
        ImGui::Unindent(15.f);
    }
    
    ImGui::Spacing();
    ImGui::Separator();

    static bool wtap = false;
    static bool aimassist = false;
    ImGui::Checkbox("wtap (em breve)", &wtap);
    ImGui::Checkbox("aim assist (em breve)", &aimassist);
}

void Screen::RenderVisualsTab() {
    ImGui::TextColored(color::GetStrongTextVec4(), "visuals");
    ImGui::Separator();
    
    auto* config = Bridge::Get()->GetConfig();
    if (config) {
        ImGui::Checkbox("hud", &config->HUD.m_Enabled);
        ImGui::Checkbox("rainbow", &config->HUD.m_Rainbow);
        ImGui::Checkbox("color bar", &config->HUD.m_ColorBar);
        ImGui::Checkbox("background", &config->HUD.m_Background);
        
        ImGui::SliderFloat("rainbow speed", &config->HUD.m_RainbowSpeed, 0.1f, 5.0f);
        
        const char* styles[] = { "old", "new" };
        ImGui::Combo("style", &config->HUD.m_Style, styles, IM_ARRAYSIZE(styles));
    }
}

void Screen::RenderSettingsTab() {
    ImGui::TextColored(color::GetStrongTextVec4(), "configuracoes");
    ImGui::Separator();
    
    if (ImGui::Button("salvar config")) {
        // TODO: salvar em arquivo
    }
    
    if (ImGui::Button("carregar config")) {
        // TODO: carregar de arquivo
    }
    
    ImGui::Spacing();
    if (ImGui::Button("fechar cheat", ImVec2(-1, 30))) {
        m_Running = false;
    }
}

void Screen::RenderMainInterface() {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(m_Width, m_Height));
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBackground;
    
    if (ImGui::Begin("OpenCommunity", nullptr, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        DrawWindowBase(dl, wp, m_Width, m_Height);

        if (ImGui::BeginMenuBar()) {
            if (ImGui::MenuItem("combat", nullptr, m_CurrentTab == 0)) m_CurrentTab = 0;
            if (ImGui::MenuItem("visuals", nullptr, m_CurrentTab == 1)) m_CurrentTab = 1;
            if (ImGui::MenuItem("settings", nullptr, m_CurrentTab == 2)) m_CurrentTab = 2;
            ImGui::EndMenuBar();
        }
        
        switch (m_CurrentTab) {
        case 0: RenderCombatTab(); break;
        case 1: RenderVisualsTab(); break;
        case 2: RenderSettingsTab(); break;
        }
        
        if (auto* config = Bridge::Get()->GetConfig()) {
            if (config->HUD.m_Enabled) {
                RenderHUDPreview();
            }
        }
        
        ImGui::End();
    }
}

void Screen::Render() {
    const ImVec4 clear = color::GetBackgroundVec4();
    const float clearColor[4] = { clear.x, clear.y, clear.z, clear.w };
    
    m_Context->OMSetRenderTargets(1, &m_RenderTarget, nullptr);
    m_Context->ClearRenderTargetView(m_RenderTarget, clearColor);

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    
    switch (m_State) {
    case AppState::Intro:
        RenderIntro();
        break;
    case AppState::InstanceChooser:
        RenderInstanceChooser();
        break;
    case AppState::Injecting:
        RenderInjecting();
        break;
    case AppState::MainInterface:
        RenderMainInterface();
        break;
    }
    
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    
    m_SwapChain->Present(1, 0);
}

void Screen::Run() {
    if (!Initialize()) {
        return;
    }

    MSG msg = {};
    while (m_Running) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                m_Running = false;
            }
        }
        
        if (m_Minimized) {
            Sleep(50);
            continue;
        }

        if (auto* info = Singleton<ClientInfo>::Get()) {
            if (info->m_TargetPid != 0 && !proc::IsProcessRunning(info->m_TargetPid)) {
                m_Running = false;
            }
        }
        
        if (!m_Running) break;

        Render();
    }

    if (auto* config = Bridge::Get()->GetConfig()) {
        config->m_Destruct = true;
    }

    Sleep(500);
}

void Screen::Close() {
    m_Running = false;
}
