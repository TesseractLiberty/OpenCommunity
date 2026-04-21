#pragma once

struct ClientInfo {
    DWORD m_TargetPid = 0;
    HWND m_Hwnd = nullptr;
    float m_Width = 900.0f;
    float m_Height = 550.0f;
    bool m_Injected = false;
    bool m_ShouldClose = false;
    wchar_t m_TargetProcess[32] = L"javaw.exe";
};
