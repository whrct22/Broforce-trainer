#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <cstdint>

class GUI {
public:
    static GUI& Instance();

    // 初始化ImGui
    bool Initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context);

    // 渲染ImGui
    void Render();

    // 清理
    void Shutdown();

    // 显示/隐藏
    void Toggle();
    bool IsVisible() const { return m_visible; }

    // 窗口过程
    static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // 设置渲染目标
    void SetRenderTargetView(ID3D11RenderTargetView* rtv) { m_renderTargetView = rtv; }

    // 原始窗口过程（public以便外部访问）
    static WNDPROC s_originalWndProc;

private:
    GUI() = default;
    ~GUI() = default;

    bool m_visible = false;
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    ID3D11RenderTargetView* m_renderTargetView = nullptr;
    HWND m_hwnd = nullptr;
};
