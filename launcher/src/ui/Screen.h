#pragma once

#include <filesystem>

struct ID3D11ShaderResourceView;
struct ImFont;

class Screen {
public:
    Screen(float width, float height);
    ~Screen();

    void Run();
    void Close();

private:
    bool Initialize();
    void Shutdown();
    bool CreateDeviceD3D(HWND hwnd);
    void CleanupDeviceD3D();
    void CreateRenderTarget();
    void CleanupRenderTarget();

    void Render();
    void RenderIntro();
    void RenderInstanceChooser();
    void RenderInjecting();
    void RenderTransitionToInterface();
    void RenderMainInterface();
    void RenderHUDPreview();

    void RenderCombatTab();
    void RenderMovementTab();
    void RenderVisualsTab();
    void RenderSettingsTab();
    void RenderClosing();
    void DrawInjectionStatusText(ImDrawList* drawList, const ImVec2& windowPos, float alpha, float offsetY, float scale, const char* headline, float elapsed, bool showCursor, const char* detailText = nullptr);
    void RenderInjectingLayer(const char* windowName, float alpha, float offsetY, float scale, const char* headline, float elapsed, bool showCursor, bool drawTopographicBackground = false, const char* detailText = nullptr);
    void RenderMainInterfaceLayer(const char* windowName, const ImVec2& windowPos, bool interactive, float overlayAlpha);

    bool LoadTextureFromMemory(const unsigned char* data, unsigned int dataSize, ID3D11ShaderResourceView** outSrv, int* outW, int* outH, bool invertRGB = false);
    void LoadIconTextures();
    void ReleaseIconTextures();
    void ApplyInterfaceTheme();
    void LoadInterfaceThemeSettings();
    void SaveInterfaceThemeSettings() const;
    bool ImportAutomaticPaletteFromImage();

    static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    float m_Width;
    float m_Height;

    HWND m_Hwnd = nullptr;
    WNDCLASSEXW m_Wc = {};

    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;
    IDXGISwapChain* m_SwapChain = nullptr;
    ID3D11RenderTargetView* m_RenderTarget = nullptr;

    bool m_Running = true;
    bool m_Initialized = false;
    bool m_Minimized = false;
    bool m_IsWindowMoveActive = false;
    bool m_IsManualWindowDrag = false;
    float m_WindowMoveOverlayAlpha = 0.0f;
    POINT m_WindowDragOffset = {};
    int m_CurrentTab = 0;
    float m_IntroStartTime = -1.0f;

    enum class InterfaceTheme {
        Default = 0,
        CheckmarkContrast = 1,
        Automatic = 2,
        Custom = 3
    };

    InterfaceTheme m_InterfaceTheme = InterfaceTheme::Default;
    ImVec4 m_AutomaticPalette[4] = {
        ImVec4(0.92f, 0.84f, 0.71f, 1.0f),
        ImVec4(0.57f, 0.72f, 0.86f, 1.0f),
        ImVec4(0.34f, 0.48f, 0.63f, 1.0f),
        ImVec4(0.18f, 0.23f, 0.29f, 1.0f)
    };
    ImVec4 m_CustomTextColor = ImVec4(0.08f, 0.09f, 0.10f, 1.0f);
    ImVec4 m_CustomBackgroundColor = ImVec4(0.98f, 0.98f, 0.99f, 1.0f);
    int m_CustomIconColorBytes[3] = { 0, 0, 0 };
    std::filesystem::path m_ImportedPaletteSource;

    enum class AppState { Intro, InstanceChooser, Injecting, TransitionToInterface, MainInterface, Closing };
    AppState m_State = AppState::Intro;
    float m_ClosingStartTime = -1.0f;
    float m_InjectionCompleteTime = -1.0f;
    float m_InjectionViewStartTime = -1.0f;
    float m_InterfaceTransitionStartTime = -1.0f;

    bool m_InjectionDone = false;
    bool m_InjectionFailed = false;
    std::string m_InjectionStatus;

    ImFont* m_FontBody = nullptr;
    ImFont* m_FontBold = nullptr;
    ImFont* m_FontTitle = nullptr;
    ImFont* m_FontHero = nullptr;
    ImFont* m_FontBodyLarge = nullptr;
    ImFont* m_FontBodyMed = nullptr;
    ImFont* m_FontBoldMed = nullptr;
    ImFont* m_FontOverlayRegular = nullptr;
    ImFont* m_FontOverlayBold = nullptr;

    ID3D11ShaderResourceView* m_IconCombat = nullptr;
    ID3D11ShaderResourceView* m_IconMovement = nullptr;
    ID3D11ShaderResourceView* m_IconVisuals = nullptr;
    ID3D11ShaderResourceView* m_IconSettings = nullptr;
    ID3D11ShaderResourceView* m_InfoLampTexture = nullptr;
    ID3D11ShaderResourceView* m_UpdatesTexture = nullptr;
    ID3D11ShaderResourceView* m_InterfaceThemeTexture = nullptr;
    int m_IconW = 0, m_IconH = 0;

    void SetupImGuiStyle();
};
