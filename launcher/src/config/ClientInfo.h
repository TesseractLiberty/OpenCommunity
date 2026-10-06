#pragma once

struct ClientInfo {
    DWORD m_TargetPid = 0;
    HWND m_Hwnd = nullptr;
    float m_Width = 900.0f;
    float m_Height = 550.0f;
    bool m_Injected = false;
    bool m_ShouldClose = false;
    // NOTA: default com nome do processo-alvo removido (plaintext em .rdata, campo sem leitura).
    // Processo-alvo hoje vem da janela LWJGL/GLFW (Screen.cpp) via m_TargetPid.
    wchar_t m_TargetProcess[32] = {};
};
