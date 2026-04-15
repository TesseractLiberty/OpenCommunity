#include "pch.h"
#include "HUD.h"
#include "../../core/Bridge.h"
#include "../../../../shared/common/ModuleConfig.h"
#include "../../../../deps/imgui/colors.h"
#include "../../../../deps/imgui/inter_bold_font.h"
#include <GL/gl.h>
#include <algorithm>
#include <cmath>


HUD::~HUD() {
    CleanupFont();
}

void HUD::InitFont(HDC hdc) {
    if (m_FontInitialized) return;

    m_FontDC = hdc;
    m_FontBase = glGenLists(256);
    if (m_FontBase == 0) return;

        DWORD numFonts = 0;
    m_FontMemHandle = AddFontMemResourceEx(
        const_cast<unsigned char*>(fonts::inter_bold_data),
        fonts::inter_bold_size,
        nullptr, &numFonts);
    m_FontResourceLoaded = (m_FontMemHandle != nullptr && numFonts > 0);
    
    HFONT font = CreateFontW(
        -14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, FF_DONTCARE | DEFAULT_PITCH, L"Inter"
    );
    if (!font) return;
    
    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    wglUseFontBitmapsA(hdc, 0, 256, m_FontBase);
    
    TEXTMETRICA tm;
    GetTextMetricsA(hdc, &tm);
    m_FontHeight = tm.tmHeight;

    ABC abc[256];
    if (GetCharABCWidthsA(hdc, 0, 255, abc)) {
        for (int i = 0; i < 256; i++) {
            m_CharWidths[i] = abc[i].abcA + abc[i].abcB + abc[i].abcC;
        }
    } else {
        GetCharWidth32A(hdc, 0, 255, m_CharWidths);
    }
    
    SelectObject(hdc, oldFont);
    DeleteObject(font);
    
    m_FontInitialized = true;
}

void HUD::CleanupFont() {
    if (m_FontInitialized && m_FontBase) {
        glDeleteLists(m_FontBase, 256);
        m_FontBase = 0;
        m_FontInitialized = false;
    }
    if (m_FontResourceLoaded && m_FontMemHandle) {
        RemoveFontMemResourceEx(m_FontMemHandle);
        m_FontMemHandle = nullptr;
        m_FontResourceLoaded = false;
    }
}

int HUD::MeasureText(const char* text) {
    int w = 0;
    for (const char* p = text; *p; p++) {
        w += m_CharWidths[(unsigned char)*p];
    }
    return w;
}

void HUD::DrawRect(float x, float y, float w, float h, float r, float g, float b, float a) {
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

void HUD::DrawRoundedRect(float x, float y, float w, float h, float radius, float r, float g, float b, float a) {
    if (radius <= 0.0f) {
        DrawRect(x, y, w, h, r, g, b, a);
        return;
    }

    const float clampedRadius = std::min(radius, std::min(w, h) * 0.5f);
    const int segmentsPerCorner = 8;
    const float pi = 3.14159265f;
    std::vector<ImVec2> points;
    points.reserve((segmentsPerCorner + 1) * 4);

    auto addCorner = [&](float centerX, float centerY, float startAngle, float endAngle) {
        for (int i = 0; i <= segmentsPerCorner; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(segmentsPerCorner);
            const float angle = startAngle + (endAngle - startAngle) * t;
            points.emplace_back(centerX + cosf(angle) * clampedRadius, centerY + sinf(angle) * clampedRadius);
        }
    };

    addCorner(x + w - clampedRadius, y + clampedRadius, -pi * 0.5f, 0.0f);
    addCorner(x + w - clampedRadius, y + h - clampedRadius, 0.0f, pi * 0.5f);
    addCorner(x + clampedRadius, y + h - clampedRadius, pi * 0.5f, pi);
    addCorner(x + clampedRadius, y + clampedRadius, pi, pi * 1.5f);

    glColor4f(r, g, b, a);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(x + w * 0.5f, y + h * 0.5f);
    for (const auto& point : points) {
        glVertex2f(point.x, point.y);
    }
    glVertex2f(points.front().x, points.front().y);
    glEnd();
}

void HUD::DrawText(float x, float y, const char* text, float r, float g, float b) {
    glColor3f(r, g, b);
    glRasterPos2f(x, y + m_FontHeight);
    glListBase(m_FontBase);
    glCallLists((GLsizei)strlen(text), GL_UNSIGNED_BYTE, text);
}

void HUD::GetRainbowRGB(int offset, float& r, float& g, float& b) {
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    ImVec4 themed = color::GetCycleVec4((float)((ms + offset) % 10000) / 10000.f);
    r = themed.x;
    g = themed.y;
    b = themed.z;
}

std::vector<HUD::ModuleEntry> HUD::GetActiveModules() {
    std::vector<ModuleEntry> modules;
    
    auto* config = Bridge::Get()->GetConfig();
    if (!config) return modules;
    
    if (config->Modules.m_AutoClicker)
        modules.push_back({ "Auto Clicker", "", 0, true });
    if (config->Modules.m_RightClicker)
        modules.push_back({ "Right Clicker", "", 0, true });
    if (config->Modules.m_WTap)
        modules.push_back({ "WTap", "", 0, true });
    if (config->Modules.m_AimAssist)
        modules.push_back({ "Aim Assist", "", 0, true });
    if (config->Modules.m_BackTrack)
        modules.push_back({ "BackTrack", "", 0, true });
    
    // calcula larguras
    for (auto& mod : modules) {
        mod.width = (float)MeasureText(mod.name);
    }
    
    // ordena por largura (maior primeiro)
    std::sort(modules.begin(), modules.end(), [](const ModuleEntry& a, const ModuleEntry& b) {
        return a.width > b.width;
    });
    
    return modules;
}

void HUD::Render(HDC hdc, ModuleConfig* config) {
    if (!config || !config->HUD.m_Enabled) return;

    if (!m_FontInitialized) {
        InitFont(hdc);
        if (!m_FontInitialized) return;
    }
    
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    m_ScreenW = viewport[2];
    m_ScreenH = viewport[3];
    if (m_ScreenW <= 0 || m_ScreenH <= 0) return;
    
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushMatrix();

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, m_ScreenW, m_ScreenH, 0, -1, 1);
    
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    auto modules = GetActiveModules();
    
    float yPos = 4.f;
    float padding = 6.f;
    float barW = 3.f;
    float itemH = (float)m_FontHeight + 4.f;
    int idx = 0;
    
    const ImVec4 bg = color::GetBackgroundVec4();
    const ImVec4 txt = color::GetStrongTextVec4();
    const ImVec4 sub1 = color::GetModuleTextVec4();
    const ImVec4 sub2 = color::GetModuleAltTextVec4();
    const ImVec4 shadow = color::Mix(color::GetStrongTextVec4(), color::GetBackgroundVec4(), 0.55f);
    
    // header
    {
        float hr, hg, hb;
        if (config->HUD.m_Rainbow)
            GetRainbowRGB(0, hr, hg, hb);
        else { hr = txt.x; hg = txt.y; hb = txt.z; }
        
        int headerW = MeasureText("OpenCommunity");
        float headerX = (float)m_ScreenW - headerW - padding * 2 - barW - 6.f;
        
        DrawRoundedRect(headerX, yPos, (float)headerW + padding * 2 + barW, itemH, 9.0f, bg.x, bg.y, bg.z, 0.94f);
        DrawRoundedRect(headerX + headerW + padding * 2, yPos, barW, itemH, 3.0f, hr, hg, hb, 1.f);
        DrawText(headerX + padding + 1, yPos + 3.0f, "OpenCommunity", shadow.x, shadow.y, shadow.z);
        DrawText(headerX + padding, yPos + 2, "OpenCommunity", hr, hg, hb);
        
        yPos += itemH + 2.f;
        idx++;
    }
    
    // módulos
    for (const auto& mod : modules) {
        float cr, cg, cb;
        if (config->HUD.m_Rainbow)
            GetRainbowRGB(idx * 400, cr, cg, cb);
        else { 
            // alternate between two colors for subtitles
            if (idx % 2 == 1) { cr = sub1.x; cg = sub1.y; cb = sub1.z; }
            else { cr = sub2.x; cg = sub2.y; cb = sub2.z; }
        }
        
        int textW = MeasureText(mod.name);
        float totalW = (float)textW + padding * 2 + barW;
        float x = (float)m_ScreenW - totalW - 6.f;
        
        DrawRoundedRect(x, yPos, totalW, itemH, 9.0f, bg.x, bg.y, bg.z, 0.94f);
        DrawRoundedRect(x + totalW - barW, yPos, barW, itemH, 3.0f, cr, cg, cb, 1.f);
        DrawText(x + padding + 1, yPos + 3.0f, mod.name, shadow.x, shadow.y, shadow.z);
        DrawText(x + padding, yPos + 2, mod.name, cr, cg, cb);
        
        yPos += itemH + 2.f;
        idx++;
    }
    
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glPopMatrix();
    glPopAttrib();
}
