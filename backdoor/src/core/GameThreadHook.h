#pragma once

namespace GameThreadHook {
    bool Initialize();
    bool ShouldRunFallback();
    void SanitizeInteractionState(void* env);
    void Shutdown();
}
