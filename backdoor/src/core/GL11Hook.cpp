#include "pch.h"
#include "GL11Hook.h"

#include "RenderHook.h"

#include "../../../deps/minhook/MinHook.h"

#include <jni.h>
#include <gl/GL.h>

namespace {
    using NgColor4fFn = void(JNICALL*)(JNIEnv* env, jclass clazz, jfloat r, jfloat g, jfloat b, jfloat a, jlong addr);
    NgColor4fFn g_OriginalNglColor4f = nullptr;

    void JNICALL HookedNglColor4f(JNIEnv* env, jclass clazz, jfloat r, jfloat g, jfloat b, jfloat a, jlong addr) {
        g_OriginalNglColor4f(env, clazz, r, g, b, a, addr);

        if (RenderCache::captured) {
            return;
        }

        GLfloat modelView[16] = {};
        GLfloat projection[16] = {};
        glGetFloatv(GL_MODELVIEW_MATRIX, modelView);
        glGetFloatv(GL_PROJECTION_MATRIX, projection);

        if (projection[11] != -1.0f) {
            return;
        }

        GLint viewport[4] = {};
        glGetIntegerv(GL_VIEWPORT, viewport);

        std::lock_guard<std::mutex> lock(RenderCache::mtx);
        RenderCache::modelView.assign(modelView, modelView + 16);
        RenderCache::projection.assign(projection, projection + 16);
        RenderCache::viewportW = viewport[2];
        RenderCache::viewportH = viewport[3];
        RenderCache::captured = true;
    }
}

void GL11Hook::Initialize() {
    HMODULE lwjgl = GetModuleHandleW(L"lwjgl64.dll");
    if (!lwjgl) {
        lwjgl = GetModuleHandleW(L"lwjgl.dll");
    }
    if (!lwjgl) {
        return;
    }

    void* nglColor4fAddress = reinterpret_cast<void*>(GetProcAddress(lwjgl, "Java_org_lwjgl_opengl_GL11_nglColor4f"));
    if (!nglColor4fAddress) {
        return;
    }

    if (MH_CreateHook(nglColor4fAddress, HookedNglColor4f, reinterpret_cast<void**>(&g_OriginalNglColor4f)) != MH_OK) {
        return;
    }

    MH_EnableHook(nglColor4fAddress);
}
