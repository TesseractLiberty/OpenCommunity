#include "pch.h"
#include "RenderHook.h"
#include "../features/render/HUD.h"
#include "../features/visuals/DamageIndicator.h"
#include "Bridge.h"
#include "../game/classes/ActiveRenderInfo.h"
#include "../game/classes/Minecraft.h"
#include "../game/classes/Player.h"
#include "../game\classes\RenderManager.h"
#include "../game\classes\Timer.h"
#include "../game/jni/GameInstance.h"
#include "../../../shared/common/FeatureManager.h"

#include "../../../deps/minhook/MinHook.h"
#include "../../../deps/imgui/imgui.h"
#include "../../../deps/imgui/imgui_impl_opengl2.hpp"
#include "../../../deps/imgui/fonts.hpp"
#include "../../../deps/imgui/open_sans_bold_font.h"
#include "../../../deps/imgui/open_sans_regular_font.h"
#include "../../../deps/imgui/play_bold_font.h"
#include "../../../deps/imgui/play_regular_font.h"

#include <filesystem>
#include <gl/GL.h>
#include <mutex>

static int(__stdcall* g_origWglSwapBuffers)(HDC) = nullptr;
static bool g_fontsInitialized = false;
static std::once_flag g_fontsOnce;
static ImFont* g_overlayRegularFont = nullptr;
static ImFont* g_overlayBoldFont = nullptr;
static ImFont* g_overlayVapeFont = nullptr;
static ImFont* g_overlayOpenSansRegularFont = nullptr;
static ImFont* g_overlayOpenSansBoldFont = nullptr;

namespace RenderCache {
    std::vector<float> modelView(16, 0.0f);
    std::vector<float> projection(16, 0.0f);
    int viewportW = 0;
    int viewportH = 0;
    std::mutex mtx;
    bool captured = false;
}

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {
    void StoreRenderMatrices(
        const std::vector<float>& modelView,
        const std::vector<float>& projection,
        int viewportWidth,
        int viewportHeight) {
        if (modelView.size() != 16 || projection.size() != 16 || viewportWidth <= 0 || viewportHeight <= 0) {
            return;
        }

        std::lock_guard<std::mutex> lock(RenderCache::mtx);
        RenderCache::modelView = modelView;
        RenderCache::projection = projection;
        RenderCache::viewportW = viewportWidth;
        RenderCache::viewportH = viewportHeight;
    }

    void BuildPerspective(float fovDegrees, float aspect, float zNear, float zFar, std::vector<float>& out) {
        constexpr float kDegreesToRadians = 3.14159265358979f / 180.0f;
        const float scale = 1.0f / tanf(fovDegrees * kDegreesToRadians * 0.5f);

        out.assign(16, 0.0f);
        out[0] = scale / aspect;
        out[5] = scale;
        out[10] = (zFar + zNear) / (zNear - zFar);
        out[11] = -1.0f;
        out[14] = (2.0f * zFar * zNear) / (zNear - zFar);
    }

    void BuildViewMatrix(float pitchDegrees, float yawDegrees, double cameraX, double cameraY, double cameraZ, std::vector<float>& out) {
        constexpr float kDegreesToRadians = 3.14159265358979f / 180.0f;
        const float pitch = pitchDegrees * kDegreesToRadians;
        const float yaw = (yawDegrees + 180.0f) * kDegreesToRadians;

        const float cosPitch = cosf(pitch);
        const float sinPitch = sinf(pitch);
        const float cosYaw = cosf(yaw);
        const float sinYaw = sinf(yaw);

        const float x = static_cast<float>(cameraX);
        const float y = static_cast<float>(cameraY);
        const float z = static_cast<float>(cameraZ);

        const float tx = -(cosYaw * x + sinYaw * z);
        const float ty = -(sinPitch * sinYaw * x + cosPitch * y - sinPitch * cosYaw * z);
        const float tz = cosPitch * sinYaw * x - sinPitch * y - cosPitch * cosYaw * z;

        out.resize(16);
        out[0] = cosYaw;           out[4] = 0.0f;     out[8] = sinYaw;            out[12] = tx;
        out[1] = sinPitch * sinYaw; out[5] = cosPitch; out[9] = -sinPitch * cosYaw; out[13] = ty;
        out[2] = -cosPitch * sinYaw; out[6] = sinPitch; out[10] = cosPitch * cosYaw; out[14] = tz;
        out[3] = 0.0f;            out[7] = 0.0f;     out[11] = 0.0f;             out[15] = 1.0f;
    }

    bool CaptureRenderMatricesFromGame(const GLint viewport[4]) {
        if (!g_Game || !g_Game->IsInitialized()) {
            return false;
        }

        JNIEnv* env = g_Game->GetCurrentEnv();
        if (!env) {
            return false;
        }

        if (env->ExceptionCheck()) env->ExceptionClear();

        const auto activeModelView = ActiveRenderInfo::GetModelView(env);
        const auto activeProjection = ActiveRenderInfo::GetProjection(env);

        if (env->ExceptionCheck()) env->ExceptionClear();

        if (activeModelView.size() == 16 && activeProjection.size() == 16 &&
            std::fabs(activeProjection[11] + 1.0f) < 0.0001f) {
            StoreRenderMatrices(
                activeModelView,
                activeProjection,
                viewport[2],
                viewport[3]);
            return true;
        }

        jobject playerObject = Minecraft::GetThePlayer(env);
        if (env->ExceptionCheck()) { env->ExceptionClear(); playerObject = nullptr; }
        jobject timerObject = Minecraft::GetTimer(env);
        if (env->ExceptionCheck()) { env->ExceptionClear(); timerObject = nullptr; }
        jobject renderManagerObject = Minecraft::GetRenderManager(env);
        if (env->ExceptionCheck()) { env->ExceptionClear(); renderManagerObject = nullptr; }

        bool captured = false;
        if (playerObject && timerObject && renderManagerObject && viewport[2] > 0 && viewport[3] > 0) {
            auto* player = reinterpret_cast<Player*>(playerObject);
            auto* timer = reinterpret_cast<Timer*>(timerObject);
            auto* renderManager = reinterpret_cast<RenderManager*>(renderManagerObject);

            float partialTicks = timer->GetRenderPartialTicks(env);
            if (env->ExceptionCheck()) { env->ExceptionClear(); partialTicks = 0.0f; }
            const float prevPitch = player->GetPrevRotationPitch(env);
            const float currentPitch = player->GetRotationPitch(env);
            const float prevYaw = player->GetPrevRotationYaw(env);
            const float currentYaw = player->GetRotationYaw(env);
            if (env->ExceptionCheck()) env->ExceptionClear();
            const float pitch = prevPitch + (currentPitch - prevPitch) * partialTicks;
            const float yaw = prevYaw + (currentYaw - prevYaw) * partialTicks;

            const Vec3D renderPos = renderManager->GetRenderPos(env);
            if (env->ExceptionCheck()) env->ExceptionClear();
            const float aspect = static_cast<float>(viewport[2]) / static_cast<float>(viewport[3]);

            std::vector<float> computedModelView;
            std::vector<float> computedProjection;
            BuildViewMatrix(pitch, yaw, renderPos.x, renderPos.y + 1.62, renderPos.z, computedModelView);
            BuildPerspective(70.0f, aspect, 0.05f, 512.0f, computedProjection);
            StoreRenderMatrices(
                computedModelView,
                computedProjection,
                viewport[2],
                viewport[3]);
            captured = true;
        }

        if (playerObject) env->DeleteLocalRef(playerObject);
        if (timerObject) env->DeleteLocalRef(timerObject);
        if (renderManagerObject) env->DeleteLocalRef(renderManagerObject);

        return captured;
    }

    std::filesystem::path FindOverlayFontPath() {
        wchar_t modulePath[MAX_PATH] = {};
        if (GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH) == 0) {
            return {};
        }

        auto current = std::filesystem::path(modulePath).parent_path();
        while (!current.empty()) {
            const auto otfCandidate = current / "Proxima Nova.otf";
            if (std::filesystem::exists(otfCandidate)) {
                return otfCandidate;
            }

            const auto ttfCandidate = current / "Proxima Nova Light.ttf";
            if (std::filesystem::exists(ttfCandidate)) {
                return ttfCandidate;
            }

            const auto parent = current.parent_path();
            if (parent == current) {
                break;
            }
            current = parent;
        }

        return {};
    }
}

bool __stdcall wglSwapBuffersHook(HDC hdc)
{
    auto* config = Bridge::Get()->GetConfig();
    if (!config || config->m_Destruct) {
        return g_origWglSwapBuffers(hdc);
    }

    JNIEnv* renderEnv = nullptr;
    if (g_Game && g_Game->IsInitialized()) {
        renderEnv = g_Game->GetCurrentEnv();
    }

    if (renderEnv && renderEnv->PushLocalFrame(256) != 0) {
        return g_origWglSwapBuffers(hdc);
    }

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0) {
        if (renderEnv) renderEnv->PopLocalFrame(nullptr);
        return g_origWglSwapBuffers(hdc);
    }

    {
        std::lock_guard<std::mutex> lock(RenderCache::mtx);
        RenderCache::viewportW = viewport[2];
        RenderCache::viewportH = viewport[3];
    }

    if (!RenderCache::captured) {
        if (renderEnv && renderEnv->ExceptionCheck()) renderEnv->ExceptionClear();
        CaptureRenderMatricesFromGame(viewport);
        if (renderEnv && renderEnv->ExceptionCheck()) renderEnv->ExceptionClear();
    }

    if (renderEnv && renderEnv->ExceptionCheck()) renderEnv->ExceptionClear();

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

        if (g_overlayRegularFont) {
            static const ImWchar iconRanges[] = { static_cast<ImWchar>(ICON_MIN_MD), static_cast<ImWchar>(ICON_MAX_16_MD), 0 };
            ImFontConfig iconCfg;
            iconCfg.MergeMode = true;
            iconCfg.PixelSnapH = true;
            io.Fonts->AddFontFromMemoryCompressedTTF(
                fonts::materialicons_compressed_data,
                fonts::materialicons_compressed_size,
                16.0f,
                &iconCfg,
                iconRanges
            );
        }

        fontCfg.FontDataOwnedByAtlas = false;
        g_overlayBoldFont = io.Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char*>(fonts::play_bold_data),
            fonts::play_bold_size,
            16.0f,
            &fontCfg
        );

        fontCfg.FontDataOwnedByAtlas = false;
        g_overlayOpenSansRegularFont = io.Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char*>(fonts::open_sans_regular_data),
            fonts::open_sans_regular_size,
            16.0f,
            &fontCfg
        );

        fontCfg.FontDataOwnedByAtlas = false;
        g_overlayOpenSansBoldFont = io.Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char*>(fonts::open_sans_bold_data),
            fonts::open_sans_bold_size,
            16.0f,
            &fontCfg
        );

        const auto vapeFontPath = FindOverlayFontPath();
        if (!vapeFontPath.empty()) {
            ImFontConfig vapeCfg;
            vapeCfg.FontDataOwnedByAtlas = false;
            g_overlayVapeFont = io.Fonts->AddFontFromFileTTF(
                vapeFontPath.string().c_str(),
                16.0f,
                &vapeCfg
            );
        }

        if (g_overlayRegularFont) {
            io.FontDefault = g_overlayRegularFont;
        } else {
            io.FontDefault = io.Fonts->AddFontDefault();
            g_overlayRegularFont = io.FontDefault;
        }
        if (!g_overlayBoldFont) {
            g_overlayBoldFont = g_overlayRegularFont;
        }
        if (!g_overlayVapeFont) {
            g_overlayVapeFont = g_overlayRegularFont;
        }
        if (!g_overlayOpenSansRegularFont) {
            g_overlayOpenSansRegularFont = g_overlayRegularFont;
        }
        if (!g_overlayOpenSansBoldFont) {
            g_overlayOpenSansBoldFont = g_overlayOpenSansRegularFont;
        }

        HUD::Get()->SetFonts(g_overlayRegularFont, g_overlayBoldFont, g_overlayVapeFont);
        DamageIndicator::SetFonts(g_overlayOpenSansRegularFont, g_overlayOpenSansBoldFont);
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
        if (renderEnv && renderEnv->ExceptionCheck()) renderEnv->ExceptionClear();
        FeatureManager::Get()->RenderOverlayAll(ImGui::GetWindowDrawList(), (float)viewport[2], (float)viewport[3]);
        if (renderEnv && renderEnv->ExceptionCheck()) renderEnv->ExceptionClear();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);

    ImGui::Render();
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

    if (renderEnv && renderEnv->ExceptionCheck()) renderEnv->ExceptionClear();

    RenderCache::captured = false;

    if (renderEnv) renderEnv->PopLocalFrame(nullptr);

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
        HUD::Get()->SetFonts(nullptr, nullptr, nullptr);
        ImGui_ImplOpenGL2_Shutdown();
        ImGui::DestroyContext();
        g_overlayRegularFont = nullptr;
        g_overlayBoldFont = nullptr;
        g_overlayVapeFont = nullptr;
        g_overlayOpenSansRegularFont = nullptr;
        g_overlayOpenSansBoldFont = nullptr;
        g_fontsInitialized = false;
    }

    m_Installed = false;
}
