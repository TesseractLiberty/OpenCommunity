#include "pch.h"
#include "RenderHook.h"
#include "GameThreadHook.h"
#include "../features/render/HUD.h"
#include "../features/visuals/DamageIndicator.h"
#include "../features/visuals/Nametags.h"
#include "../features/visuals/Notifications.h"
#include "Bridge.h"
#include "../game/classes/ActiveRenderInfo.h"
#include "../game/classes/Minecraft.h"
#include "../game/classes/Player.h"
#include "../game\classes\RenderManager.h"
#include "../game\classes\Timer.h"
#include "../game/jni/Class.h"
#include "../game/jni/Field.h"
#include "../game/jni/GameInstance.h"
#include "../game/jni/Method.h"
#include "../game/mapping/Mapper.h"
#include "../../../shared/common/modules/ModuleManager.h"

#include "../../../deps/minhook/MinHook.h"
#include "../../../deps/imgui/imgui.h"
#include "../../../deps/imgui/imgui_impl_opengl2.hpp"
#include "../../../deps/imgui/fonts/fonts.hpp"
#include "../../../deps/imgui/fonts/inter_bold_font.h"
#include "../../../deps/imgui/fonts/inter_regular_font.h"
#include "../../../deps/imgui/fonts/open_sans_bold_font.h"
#include "../../../deps/imgui/fonts/open_sans_regular_font.h"
#include "../../../deps/imgui/fonts/play_bold_font.h"
#include "../../../deps/imgui/fonts/play_regular_font.h"
#include "../../../deps/imgui/fonts/sf_ui_display_bold_font.h"

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
static ImFont* g_overlaySanFranciscoBoldFont = nullptr;
static ImFont* g_overlayInterRegularFont = nullptr;
static ImFont* g_overlayInterBoldFont = nullptr;

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

    Vec3D ComputeThirdPersonCameraPosition(const Vec3D& eyePos, float yawDegrees, float pitchDegrees, float distance) {
        constexpr float kDegreesToRadians = 3.14159265358979f / 180.0f;
        const float yaw = (yawDegrees + 90.0f) * kDegreesToRadians;
        const float pitch = pitchDegrees * kDegreesToRadians;
        const float cosPitch = cosf(pitch);

        return {
            eyePos.x - static_cast<double>(cosf(yaw) * distance * cosPitch),
            eyePos.y + static_cast<double>(distance * sinf(pitch)),
            eyePos.z - static_cast<double>(sinf(yaw) * distance * cosPitch)
        };
    }

    float ReadEntityRendererFloatField(JNIEnv* env, jobject entityRendererObject, const char* fieldKey, float fallback) {
        if (!env || !entityRendererObject || !fieldKey || !g_Game || !g_Game->IsInitialized()) {
            return fallback;
        }

        const std::string className = Mapper::Get("net/minecraft/client/renderer/EntityRenderer");
        const std::string fieldName = Mapper::Get(fieldKey);
        if (className.empty() || fieldName.empty()) {
            return fallback;
        }

        Class* entityRendererClass = g_Game->FindClass(className);
        Field* field = entityRendererClass ? entityRendererClass->GetField(env, fieldName.c_str(), "F") : nullptr;
        const float value = field ? field->GetFloatField(env, entityRendererObject) : fallback;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return fallback;
        }

        return std::isfinite(value) ? value : fallback;
    }

    float ReadThirdPersonDistance(JNIEnv* env, float partialTicks, float fallback) {
        jobject entityRendererObject = Minecraft::GetEntityRenderer(env);
        if (env && env->ExceptionCheck()) {
            env->ExceptionClear();
            entityRendererObject = nullptr;
        }
        if (!entityRendererObject) {
            return fallback;
        }

        const float current = ReadEntityRendererFloatField(env, entityRendererObject, "thirdPersonDistance", fallback);
        const float previous = ReadEntityRendererFloatField(env, entityRendererObject, "thirdPersonDistanceTemp", current);
        env->DeleteLocalRef(entityRendererObject);

        const float distance = previous + (current - previous) * partialTicks;
        if (!std::isfinite(distance) || distance <= 0.05f || distance > 32.0f) {
            return fallback;
        }
        return distance;
    }

    float ReadCameraFov(JNIEnv* env, float partialTicks, float fallback) {
        if (!env || !g_Game || !g_Game->IsInitialized()) {
            return fallback;
        }

        jobject entityRendererObject = Minecraft::GetEntityRenderer(env);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            entityRendererObject = nullptr;
        }
        if (!entityRendererObject) {
            return fallback;
        }

        const std::string className = Mapper::Get("net/minecraft/client/renderer/EntityRenderer");
        const std::string methodName = Mapper::Get("getFOVModifier");
        Class* entityRendererClass = className.empty() ? nullptr : g_Game->FindClass(className);
        Method* method = (entityRendererClass && !methodName.empty())
            ? entityRendererClass->GetMethod(env, methodName.c_str(), "(FZ)F")
            : nullptr;

        const float fov = method
            ? method->CallFloatMethod(env, entityRendererObject, false, partialTicks, static_cast<jboolean>(JNI_TRUE))
            : fallback;
        env->DeleteLocalRef(entityRendererObject);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return fallback;
        }

        return std::isfinite(fov) && fov >= 10.0f && fov <= 170.0f ? fov : fallback;
    }

    bool TryCaptureMatricesFromActiveRenderInfo(JNIEnv* env, const GLint viewport[4]) {
        if (!env || viewport[2] <= 0 || viewport[3] <= 0) {
            return false;
        }

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

        return false;
    }

    bool TryCaptureMatricesFromCameraState(JNIEnv* env, const GLint viewport[4]) {
        if (!env || viewport[2] <= 0 || viewport[3] <= 0) {
            return false;
        }

        jobject viewEntityObject = Minecraft::GetRenderViewEntity(env);
        if (env->ExceptionCheck()) { env->ExceptionClear(); viewEntityObject = nullptr; }
        if (!viewEntityObject) {
            viewEntityObject = Minecraft::GetThePlayer(env);
            if (env->ExceptionCheck()) { env->ExceptionClear(); viewEntityObject = nullptr; }
        }
        jobject timerObject = Minecraft::GetTimer(env);
        if (env->ExceptionCheck()) { env->ExceptionClear(); timerObject = nullptr; }

        bool captured = false;
        if (viewEntityObject && timerObject && viewport[2] > 0 && viewport[3] > 0) {
            auto* viewEntity = reinterpret_cast<Player*>(viewEntityObject);
            auto* timer = reinterpret_cast<Timer*>(timerObject);

            float partialTicks = timer->GetRenderPartialTicks(env);
            if (env->ExceptionCheck()) { env->ExceptionClear(); partialTicks = 0.0f; }
            const float prevPitch = viewEntity->GetPrevRotationPitch(env);
            const float currentPitch = viewEntity->GetRotationPitch(env);
            const float prevYaw = viewEntity->GetPrevRotationYaw(env);
            const float currentYaw = viewEntity->GetRotationYaw(env);
            if (env->ExceptionCheck()) env->ExceptionClear();
            const float pitch = prevPitch + (currentPitch - prevPitch) * partialTicks;
            const float yaw = prevYaw + (currentYaw - prevYaw) * partialTicks;

            const Vec3D position = viewEntity->GetPos(env);
            const Vec3D lastPosition = viewEntity->GetLastTickPos(env);
            if (env->ExceptionCheck()) env->ExceptionClear();
            const float eyeHeight = viewEntity->GetEyeHeight(env);
            if (env->ExceptionCheck()) env->ExceptionClear();

            Vec3D cameraPos{
                lastPosition.x + (position.x - lastPosition.x) * partialTicks,
                lastPosition.y + (position.y - lastPosition.y) * partialTicks + static_cast<double>(eyeHeight),
                lastPosition.z + (position.z - lastPosition.z) * partialTicks
            };

            float cameraYaw = yaw;
            float cameraPitch = pitch;
            const int thirdPersonView = Minecraft::GetThirdPersonView(env);
            if (env->ExceptionCheck()) env->ExceptionClear();
            if (thirdPersonView != 0) {
                const float distance = ReadThirdPersonDistance(env, partialTicks, 4.0f);
                const float signedDistance = thirdPersonView == 2 ? -distance : distance;
                cameraPos = ComputeThirdPersonCameraPosition(cameraPos, yaw, pitch, signedDistance);

                if (thirdPersonView == 2) {
                    cameraYaw = yaw + 180.0f;
                    cameraPitch = -pitch;
                }
            }

            const float aspect = static_cast<float>(viewport[2]) / static_cast<float>(viewport[3]);
            const float fov = ReadCameraFov(env, partialTicks, 70.0f);

            std::vector<float> computedModelView;
            std::vector<float> computedProjection;
            BuildViewMatrix(cameraPitch, cameraYaw, cameraPos.x, cameraPos.y, cameraPos.z, computedModelView);
            BuildPerspective(fov, aspect, 0.05f, 512.0f, computedProjection);
            StoreRenderMatrices(
                computedModelView,
                computedProjection,
                viewport[2],
                viewport[3]);
            captured = true;
        }

        if (viewEntityObject) env->DeleteLocalRef(viewEntityObject);
        if (timerObject) env->DeleteLocalRef(timerObject);

        return captured;
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

        const bool thirdPersonActive = Minecraft::GetThirdPersonView(env) != 0;
        if (env->ExceptionCheck()) env->ExceptionClear();

        const auto gameVersion = g_Game->GetGameVersion();
        const bool preferCameraState =
            (gameVersion == GameVersions::LUNAR && !thirdPersonActive) ||
            (gameVersion == GameVersions::BADLION && thirdPersonActive);

        if (preferCameraState) {
            if (TryCaptureMatricesFromCameraState(env, viewport)) {
                return true;
            }

            return TryCaptureMatricesFromActiveRenderInfo(env, viewport);
        }

        if (TryCaptureMatricesFromActiveRenderInfo(env, viewport)) {
            return true;
        }

        return TryCaptureMatricesFromCameraState(env, viewport);
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

    if (renderEnv && renderEnv->ExceptionCheck()) renderEnv->ExceptionClear();
    CaptureRenderMatricesFromGame(viewport);
    if (renderEnv && renderEnv->ExceptionCheck()) renderEnv->ExceptionClear();

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

        fontCfg.FontDataOwnedByAtlas = false;
        g_overlaySanFranciscoBoldFont = io.Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char*>(fonts::sf_ui_display_bold_data),
            fonts::sf_ui_display_bold_size,
            16.0f,
            &fontCfg
        );

        fontCfg.FontDataOwnedByAtlas = false;
        g_overlayInterRegularFont = io.Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char*>(fonts::inter_regular_data),
            fonts::inter_regular_size,
            12.0f,
            &fontCfg
        );

        fontCfg.FontDataOwnedByAtlas = false;
        g_overlayInterBoldFont = io.Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char*>(fonts::inter_bold_data),
            fonts::inter_bold_size,
            14.0f,
            &fontCfg
        );

        if (g_overlaySanFranciscoBoldFont) {
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
        if (!g_overlaySanFranciscoBoldFont) {
            g_overlaySanFranciscoBoldFont = g_overlayRegularFont;
        }
        if (!g_overlayInterRegularFont) {
            g_overlayInterRegularFont = g_overlayRegularFont;
        }
        if (!g_overlayInterBoldFont) {
            g_overlayInterBoldFont = g_overlayInterRegularFont;
        }

        HUD::Get()->SetFonts(g_overlayRegularFont, g_overlayBoldFont, g_overlayVapeFont);
        DamageIndicator::SetFonts(g_overlayOpenSansRegularFont, g_overlayOpenSansBoldFont);
        Nametags::SetFont(g_overlaySanFranciscoBoldFont);
        Notifications::SetFonts(g_overlayInterRegularFont, g_overlayInterBoldFont);
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

        GameThreadHook::SanitizeInteractionState(renderEnv);
        HUD::Get()->Render(config, (float)viewport[2], (float)viewport[3]);
        if (renderEnv && renderEnv->ExceptionCheck()) renderEnv->ExceptionClear();
        ModuleManager::Get()->RenderOverlayAll(ImGui::GetWindowDrawList(), (float)viewport[2], (float)viewport[3]);
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
        g_overlaySanFranciscoBoldFont = nullptr;
        g_overlayInterRegularFont = nullptr;
        g_overlayInterBoldFont = nullptr;
        Nametags::SetFont(nullptr);
        Notifications::SetFonts(nullptr, nullptr);
        g_fontsInitialized = false;
    }

    m_Installed = false;
}
