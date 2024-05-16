// HolePunchExample.cpp : 定义应用程序的入口点。
//

#include "HolePunchExample.h"

#include <assert.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <dxgi1_3.h>
#include <wrl/client.h>  // For ComPtr

#include <memory>
#include <vector>

#include "framework.h"

using Microsoft::WRL::ComPtr;

#define MAX_LOADSTRING 100

namespace {
class ChildWindow;

// 全局变量:
HINSTANCE hInst;                      // 当前实例
WCHAR szTitle[MAX_LOADSTRING];        // 标题栏文本
WCHAR szWindowClass[MAX_LOADSTRING];  // 主窗口类名

// 此代码模块中包含的函数的前向声明:
ATOM MyRegisterClass(HINSTANCE hInstance);
ATOM MyRegisterChildClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
HWND InitInstanceChild(HINSTANCE hInstance, HWND parent,
                       std::shared_ptr<ChildWindow>* child_window, bool ontop);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK WndProcChild(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);

void OutputDebugStringFmt(const char* format, ...) {
  char buffer[1024];
  va_list args;
  va_start(args, format);
  vsprintf_s(buffer, format, args);
  va_end(args);
  OutputDebugStringA(buffer);
}

// Function to set a hole in a window
void SetWindowHole(HWND hwnd, int holeX, int holeY, int holeWidth,
                   int holeHeight, int width, int height) {
  // Create a region that covers the whole window
  HRGN hrgnWhole = CreateRectRgn(0, 0, width, height);

  // Create a region for the hole
  HRGN hrgnHole =
      CreateRectRgn(holeX, holeY, holeX + holeWidth, holeY + holeHeight);

  // Combine the regions to subtract the hole from the whole
  CombineRgn(hrgnWhole, hrgnWhole, hrgnHole, RGN_DIFF);

  // Set the new region for the window
  SetWindowRgn(hwnd, hrgnWhole, TRUE);

  // Clean up
  DeleteObject(hrgnHole);
}

class WindowBase {
 public:
  template <class T>
  static T* FromLParam(LPARAM lParam);
  template <class T>
  static T* FromHWND(HWND hWnd);
};

class MainWindow : public WindowBase {
 public:
  MainWindow();
  ~MainWindow();
  void OnCreate(HWND hwnd);
  void OnSize(size_t x, size_t y, size_t width, size_t height);
  void AddChild(std::shared_ptr<ChildWindow>& child);
  ID3D11Device* device() { return device_.Get(); }
  ID3D11DeviceContext* context() { return context_.Get(); }
  IDXGIFactory2* factory() { return factory_.Get(); }
  ID3D11VertexShader* vertex_shader() { return vertex_shader_.Get(); }
  ID3D11PixelShader* pixel_shader() { return pixel_shader_.Get(); }
  ID3D11InputLayout* input_layout() { return input_layout_.Get(); }
  ID3D11Buffer** vertex_buffer_address() {
    return vertex_buffer_.GetAddressOf();
  }

  UINT num_verts() { return num_verts_; }
  UINT* stride_address() { return &stride_; }
  UINT* offset_address() { return &offset_; }

 private:
  void InitializePaintContext();

  std::vector<std::weak_ptr<ChildWindow>> children_;
  // Define variables to hold the Device and Device Context interfaces
  ComPtr<ID3D11Device> device_;
  ComPtr<ID3D11DeviceContext> context_;
  ComPtr<IDXGIFactory2> factory_;
  ComPtr<ID3D11VertexShader> vertex_shader_;
  ComPtr<ID3D11PixelShader> pixel_shader_;
  ComPtr<ID3D11InputLayout> input_layout_;
  ComPtr<ID3D11Buffer> vertex_buffer_;

  UINT num_verts_;
  UINT stride_;
  UINT offset_;
  HWND hwnd_ = nullptr;
};

class ChildWindow : public WindowBase {
 public:
  explicit ChildWindow(HWND parent);
  ~ChildWindow();

  void set_on_top(bool ontop) { ontop_ = ontop; }
  void OnCreate(HWND hwnd);
  void OnParentSize(size_t x, size_t y, size_t width, size_t height);
  void OnPaint();
  void SetClearColor(float r, float g, float b, float a);

 private:
  void InitializePaintContext();
  HWND hwnd_ = nullptr;
  HWND hwnd_parent_;
  ComPtr<ID3D11Device> device_;
  ComPtr<ID3D11DeviceContext> context_;
  ComPtr<IDXGISwapChain1> swap_chain_;

  float r_ = 0.0f;
  float g_ = 0.0f;
  float b_ = 0.0f;
  float a_ = 0.0f;

  bool ontop_ = false;
};

template <class T>
T* WindowBase::FromLParam(LPARAM lParam) {
  return reinterpret_cast<T*>(lParam);
}

template <class T>
T* WindowBase::FromHWND(HWND hWnd) {
  return reinterpret_cast<T*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
}

MainWindow::MainWindow() {}
MainWindow::~MainWindow() {}

void MainWindow::OnCreate(HWND hWnd) {
  hwnd_ = hWnd;
  SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  InitializePaintContext();
}

void MainWindow::OnSize(size_t x, size_t y, size_t width, size_t height) {
  for (auto& weak_child : children_) {
    auto child = weak_child.lock();
    if (child) child->OnParentSize(x, y, width, height);
  }
}

void MainWindow::InitializePaintContext() {
#ifdef _DEBUG
  UINT createDeviceFlags = D3D11_CREATE_DEVICE_DEBUG;
#else
  UINT createDeviceFlags = 0;
#endif
  HRESULT hr;
  hr = CreateDXGIFactory2(0, IID_PPV_ARGS(factory_.GetAddressOf()));
  if (FAILED(hr)) {
    OutputDebugStringFmt("CreateDXGIFactory2 failed: hr: %lx", (long)hr);
    _exit(1);
  }
  hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                         createDeviceFlags, nullptr, 0, D3D11_SDK_VERSION,
                         device_.GetAddressOf(), nullptr,
                         context_.GetAddressOf());
  if (FAILED(hr)) {
    OutputDebugStringFmt("D3D11CreateDevice failed: hr: %lx", (long)hr);
    _exit(1);
  }
  // Set up debug layer to break on D3D11 errors
  ComPtr<ID3D11Debug> d3dDebug;
  device_->QueryInterface(__uuidof(ID3D11Debug),
                          reinterpret_cast<void**>(d3dDebug.GetAddressOf()));
  if (d3dDebug) {
    ComPtr<ID3D11InfoQueue> d3dInfoQueue;
    if (SUCCEEDED(d3dDebug->QueryInterface(
            __uuidof(ID3D11InfoQueue),
            reinterpret_cast<void**>(d3dInfoQueue.GetAddressOf())))) {
      d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
      d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);
    }
  }
  // Create Vertex Shader
  ComPtr<ID3DBlob> vsBlob;
  {
    ComPtr<ID3DBlob> shaderCompileErrorsBlob;
    HRESULT hResult = D3DCompileFromFile(
        L"shaders.hlsl", nullptr, nullptr, "vs_main", "vs_5_0", 0, 0,
        vsBlob.GetAddressOf(), shaderCompileErrorsBlob.GetAddressOf());
    if (FAILED(hResult)) {
      const char* errorString = NULL;
      if (hResult == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
        errorString = "Could not compile shader; file not found";
      else if (shaderCompileErrorsBlob) {
        errorString = (const char*)shaderCompileErrorsBlob->GetBufferPointer();
      }
      MessageBoxA(0, errorString, "Shader Compiler Error",
                  MB_ICONERROR | MB_OK);
      _exit(1);
    }

    hResult = device_->CreateVertexShader(vsBlob->GetBufferPointer(),
                                          vsBlob->GetBufferSize(), nullptr,
                                          vertex_shader_.GetAddressOf());
    assert(SUCCEEDED(hResult));
  }

  // Create Pixel Shader
  {
    ComPtr<ID3DBlob> psBlob;
    ComPtr<ID3DBlob> shaderCompileErrorsBlob;
    HRESULT hResult = D3DCompileFromFile(
        L"shaders.hlsl", nullptr, nullptr, "ps_main", "ps_5_0", 0, 0,
        psBlob.GetAddressOf(), shaderCompileErrorsBlob.GetAddressOf());
    if (FAILED(hResult)) {
      const char* errorString = NULL;
      if (hResult == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
        errorString = "Could not compile shader; file not found";
      else if (shaderCompileErrorsBlob) {
        errorString = (const char*)shaderCompileErrorsBlob->GetBufferPointer();
      }
      MessageBoxA(0, errorString, "Shader Compiler Error",
                  MB_ICONERROR | MB_OK);
      _exit(1);
    }

    hResult = device_->CreatePixelShader(psBlob->GetBufferPointer(),
                                         psBlob->GetBufferSize(), nullptr,
                                         pixel_shader_.GetAddressOf());
    assert(SUCCEEDED(hResult));
  }

  // Create Input Layout
  {
    D3D11_INPUT_ELEMENT_DESC inputElementDesc[] = {
        {"POS", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA,
         0},
        {"TEX", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT,
         D3D11_INPUT_PER_VERTEX_DATA, 0}};

    HRESULT hResult = device_->CreateInputLayout(
        inputElementDesc, ARRAYSIZE(inputElementDesc),
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
        input_layout_.GetAddressOf());
    assert(SUCCEEDED(hResult));
  }

  // Create Vertex Buffer
  {
    // clang-format off
		float vertexData[] = {// x, y, u, v
							  -0.5f, 0.5f, 0.f, 0.f,
							  0.5f, -0.5f, 1.f, 1.f,
							  -0.5f, -0.5f, 0.f, 1.f,
							  -0.5f, 0.5f, 0.f, 0.f,
							  0.5f, 0.5f, 1.f, 0.f,
							  0.5f, -0.5f, 1.f, 1.f};
    // clang-format on
    stride_ = 4 * sizeof(float);
    num_verts_ = sizeof(vertexData) / stride_;
    offset_ = 0;

    D3D11_BUFFER_DESC vertexBufferDesc = {};
    vertexBufferDesc.ByteWidth = sizeof(vertexData);
    vertexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vertexSubresourceData = {vertexData};

    HRESULT hResult =
        device_->CreateBuffer(&vertexBufferDesc, &vertexSubresourceData,
                              vertex_buffer_.GetAddressOf());
    assert(SUCCEEDED(hResult));
  }
}

void MainWindow::AddChild(std::shared_ptr<ChildWindow>& child) {
  children_.emplace_back(child);
}

ChildWindow::ChildWindow(HWND parent) : hwnd_parent_(parent) {}

ChildWindow::~ChildWindow() {}

void ChildWindow::OnCreate(HWND hwnd) { hwnd_ = hwnd; }

void ChildWindow::OnParentSize(size_t x, size_t y, size_t width,
                               size_t height) {
  HWND insert_order = ontop_ ? HWND_TOP : HWND_BOTTOM;
  SetWindowPos(hwnd_, insert_order, static_cast<INT>(x), static_cast<INT>(y),
               static_cast<INT>(width), static_cast<INT>(height), 0);
  InitializePaintContext();
}

void ChildWindow::OnPaint() {
  RECT winRect;
  GetClientRect(hwnd_, &winRect);
  D3D11_VIEWPORT viewport = {0.0f,
                             0.0f,
                             (FLOAT)(winRect.right - winRect.left),
                             (FLOAT)(winRect.bottom - winRect.top),
                             0.0f,
                             1.0f};

  context_->RSSetViewports(1, &viewport);

  // Create render target view
  ComPtr<ID3D11Texture2D> back_buffer;
  ComPtr<ID3D11RenderTargetView> render_target_view;
  HRESULT hr;
  hr = swap_chain_->GetBuffer(
      0, __uuidof(ID3D11Texture2D),
      reinterpret_cast<void**>(back_buffer.GetAddressOf()));
  if (FAILED(hr)) {
    OutputDebugStringFmt("GetBuffer failed: hr: %lx", (long)hr);
    _exit(1);
  }

  hr = device_->CreateRenderTargetView(back_buffer.Get(), nullptr,
                                       render_target_view.GetAddressOf());
  if (FAILED(hr)) {
    OutputDebugStringFmt("CreateRenderTargetView failed: hr: %lx", (long)hr);
    _exit(1);
  }

  context_->OMSetRenderTargets(1, render_target_view.GetAddressOf(), nullptr);

  float clearColor[4] = {r_, g_, b_, a_};
  context_->ClearRenderTargetView(render_target_view.Get(), clearColor);

  // Clear center if on top
  if (ontop_) {
    MainWindow* main_window = WindowBase::FromHWND<MainWindow>(hwnd_parent_);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->IASetInputLayout(main_window->input_layout());

    context_->VSSetShader(main_window->vertex_shader(), nullptr, 0);
    context_->PSSetShader(main_window->pixel_shader(), nullptr, 0);

    context_->IASetVertexBuffers(0, 1, main_window->vertex_buffer_address(),
                                 main_window->stride_address(),
                                 main_window->offset_address());

    context_->Draw(main_window->num_verts(), 0);
  } else {
    OutputDebugStringFmt("draw nothing! %d, %d %d %d", winRect.left,
                         winRect.right, winRect.top, winRect.bottom);
  }

  // Present back buffer in vsync
  swap_chain_->Present(0, 0);
}

void ChildWindow::InitializePaintContext() {
  HRESULT hr;
  MainWindow* main_window = WindowBase::FromHWND<MainWindow>(hwnd_parent_);
  device_ = main_window->device();
  context_ = main_window->context();
  DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {0};
  swapChainDesc.BufferCount = 2;
  swapChainDesc.Width = 0;
  swapChainDesc.Height = 0;
  swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  swapChainDesc.SampleDesc.Count = 1;
  hr = main_window->factory()->CreateSwapChainForHwnd(
      device_.Get(), hwnd_, &swapChainDesc, nullptr, nullptr,
      swap_chain_.GetAddressOf());
  if (FAILED(hr)) {
    OutputDebugStringFmt("CreateSwapChainForHwnd failed: hr: %lx", (long)hr);
    _exit(1);
  }
}

void ChildWindow::SetClearColor(float r, float g, float b, float a) {
  r_ = r;
  g_ = g;
  b_ = b;
  a_ = a;
}

//
//  函数: MyRegisterClass()
//
//  目标: 注册窗口类。
//
ATOM MyRegisterClass(HINSTANCE hInstance) {
  WNDCLASSEXW wcex;

  wcex.cbSize = sizeof(WNDCLASSEX);

  wcex.style = 0;
  wcex.lpfnWndProc = WndProc;
  wcex.cbClsExtra = 0;
  wcex.cbWndExtra = 0;
  wcex.hInstance = hInstance;
  wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_HOLEPUNCHEXAMPLE));
  wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wcex.hbrBackground = (HBRUSH)(nullptr);
  wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_HOLEPUNCHEXAMPLE);
  wcex.lpszClassName = szWindowClass;
  wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

  return RegisterClassExW(&wcex);
}

//
//  函数: MyRegisterClass()
//
//  目标: 注册窗口类。
//
ATOM MyRegisterChildClass(HINSTANCE hInstance) {
  WNDCLASSEXW wcex;

  wcex.cbSize = sizeof(WNDCLASSEX);

  wcex.style = 0;
  wcex.lpfnWndProc = WndProcChild;
  wcex.cbClsExtra = 0;
  wcex.cbWndExtra = 0;
  wcex.hInstance = hInstance;
  wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_HOLEPUNCHEXAMPLE));
  wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wcex.hbrBackground = (HBRUSH)(nullptr);
  wcex.lpszMenuName = nullptr;
  wcex.lpszClassName = L"childwindow";
  wcex.hIconSm = nullptr;

  return RegisterClassExW(&wcex);
}

//
//   函数: InitInstance(HINSTANCE, int)
//
//   目标: 保存实例句柄并创建主窗口
//
//   注释:
//
//        在此函数中，我们在全局变量中保存实例句柄并
//        创建和显示主程序窗口。
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow) {
  hInst = hInstance;  // 将实例句柄存储在全局变量中
  std::unique_ptr<MainWindow> main_window = std::make_unique<MainWindow>();

  HWND hWnd = CreateWindowExW(0, szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr,
                              nullptr, hInstance, main_window.get());

  if (!hWnd) {
    return FALSE;
  }
  main_window->OnCreate(hWnd);

  ShowWindow(hWnd, nCmdShow);
  std::unique_ptr<std::shared_ptr<ChildWindow>> child_window =
      std::make_unique<std::shared_ptr<ChildWindow>>(
          std::make_shared<ChildWindow>(hWnd));
  // set top child background to red.
  (*child_window)->SetClearColor(1.0f, 0.0f, 0.0f, 1.0f);
  main_window->AddChild(*child_window);
  InitInstanceChild(hInstance, hWnd, child_window.release(), true);
  child_window = std::make_unique<std::shared_ptr<ChildWindow>>(
      std::make_shared<ChildWindow>(hWnd));
  // set bottom child background to green.
  (*child_window)->SetClearColor(0.0f, 1.0f, 0.0f, 1.0f);
  main_window->AddChild(*child_window);
  InitInstanceChild(hInstance, hWnd, child_window.release(), false);
  main_window.release();

  UpdateWindow(hWnd);

  return TRUE;
}

HWND InitInstanceChild(HINSTANCE hInstance, HWND parent,
                       std::shared_ptr<ChildWindow>* child_window, bool ontop) {
  hInst = hInstance;  // 将实例句柄存储在全局变量中

  DWORD ex_style = 0;
  HWND hWnd = CreateWindowExW(
      ex_style, L"childwindow", L"", WS_CHILD | WS_VISIBLE, CW_USEDEFAULT, 0,
      CW_USEDEFAULT, 0, parent, nullptr, hInstance, nullptr);

  if (!hWnd) {
    OutputDebugStringFmt("Create child window failed: %lx.", GetLastError());
    _exit(1);
  }

  // SetLayeredWindowAttributes(hWnd, 0 /* color key */, 0 /* alpha */,
  // LWA_ALPHA);

  (*child_window)->set_on_top(ontop);
  (*child_window)->OnCreate(hWnd);
  SetWindowLongPtr(hWnd, GWLP_USERDATA,
                   reinterpret_cast<LONG_PTR>(child_window));

  // Resize the child now.
  RECT winRect;
  GetClientRect(parent, &winRect);
  (*child_window)
      ->OnParentSize(0, 0, winRect.right - winRect.left,
                     winRect.bottom - winRect.top);
  if (ontop && false) {
    int width = winRect.right - winRect.left;
    int height = winRect.bottom - winRect.top;
    SetWindowHole(hWnd, width / 4, height / 4, width / 2, height / 2, width,
                  height);
  }
  return hWnd;
}

BOOL SetPicture(HWND hWnd, HBITMAP hBmp, COLORREF nColor) {
  BITMAP bm;
  GetObject(hBmp, sizeof(bm), &bm);
  SIZE szBmp = {bm.bmWidth, bm.bmHeight};

  HDC hDCScreen = GetDC(NULL);
  HDC hDCMem = CreateCompatibleDC(hDCScreen);
  HBITMAP hBmpOld = (HBITMAP)SelectObject(hDCMem, hBmp);

  BLENDFUNCTION blend = {0};
  blend.BlendOp = AC_SRC_OVER;
  blend.SourceConstantAlpha = 255;
  // blend.AlphaFormat = AC_SRC_OVER;
  blend.AlphaFormat = AC_SRC_ALPHA;

  RECT rc;
  GetWindowRect(hWnd, &rc);

  POINT ptSrc = {0};
  POINT ptDest = {rc.left, rc.top};
  BOOL bRet = UpdateLayeredWindow(hWnd, hDCScreen, &ptDest, &szBmp, hDCMem,
                                  &ptSrc, nColor, &blend, ULW_ALPHA);

  SelectObject(hDCMem, hBmpOld);
  DeleteDC(hDCMem);
  ReleaseDC(NULL, hDCScreen);
  return bRet;
}

//
//  函数: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  目标: 处理主窗口的消息。
//
//  WM_COMMAND  - 处理应用程序菜单
//  WM_PAINT    - 绘制主窗口
//  WM_DESTROY  - 发送退出消息并返回
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam,
                         LPARAM lParam) {
  switch (message) {
    case WM_ERASEBKGND: {
      return 1;
    }
    case WM_COMMAND: {
      int wmId = LOWORD(wParam);
      // 分析菜单选择:
      switch (wmId) {
        case IDM_ABOUT:
          DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
          break;
        case IDM_EXIT:
          DestroyWindow(hWnd);
          break;
        default:
          return DefWindowProc(hWnd, message, wParam, lParam);
      }
    } break;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hWnd, &ps);
      // TODO: 在此处添加使用 hdc 的任何绘图代码...
      EndPaint(hWnd, &ps);
    } break;
    case WM_DESTROY: {
      PostQuitMessage(0);
      MainWindow* main_window = WindowBase::FromHWND<MainWindow>(hWnd);
      delete main_window;
      SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)0);
      break;
    }
    case WM_CREATE: {
      break;
    }
    case WM_SIZE: {
      MainWindow* main_window = WindowBase::FromHWND<MainWindow>(hWnd);
      main_window->OnSize(0, 0, LOWORD(lParam), HIWORD(lParam));
      break;
    }
    default:
      return DefWindowProc(hWnd, message, wParam, lParam);
  }
  return 0;
}

// “关于”框的消息处理程序。
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
  UNREFERENCED_PARAMETER(lParam);
  switch (message) {
    case WM_INITDIALOG:
      return (INT_PTR)TRUE;

    case WM_COMMAND:
      if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
        EndDialog(hDlg, LOWORD(wParam));
        return (INT_PTR)TRUE;
      }
      break;
  }
  return (INT_PTR)FALSE;
}

LRESULT CALLBACK WndProcChild(HWND hWnd, UINT message, WPARAM wParam,
                              LPARAM lParam) {
  switch (message) {
    case WM_ERASEBKGND: {
      return 1;
    }
    case WM_COMMAND: {
      int wmId = LOWORD(wParam);
      // 分析菜单选择:
      switch (wmId) {
        case IDM_ABOUT:
          DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
          break;
        case IDM_EXIT:
          DestroyWindow(hWnd);
          break;
        default:
          return DefWindowProc(hWnd, message, wParam, lParam);
      }
    } break;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hWnd, &ps);
      (*WindowBase::FromHWND<std::shared_ptr<ChildWindow>>(hWnd))->OnPaint();
      EndPaint(hWnd, &ps);
    } break;
    case WM_DESTROY:
      delete WindowBase::FromHWND<std::shared_ptr<ChildWindow>>(hWnd);
      SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)0);
      break;
    case WM_CREATE:
      break;
    default:
      return DefWindowProc(hWnd, message, wParam, lParam);
  }
  return 0;
}

}  // namespace

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                      _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine,
                      _In_ int nCmdShow) {
  UNREFERENCED_PARAMETER(hPrevInstance);
  UNREFERENCED_PARAMETER(lpCmdLine);
  // Initialize COM
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(hr)) {
    return -1;
  }
  // TODO: 在此处放置代码。

  // 初始化全局字符串
  LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
  LoadStringW(hInstance, IDC_HOLEPUNCHEXAMPLE, szWindowClass, MAX_LOADSTRING);
  MyRegisterClass(hInstance);
  MyRegisterChildClass(hInstance);

  // 执行应用程序初始化:
  if (!InitInstance(hInstance, nCmdShow)) {
    return FALSE;
  }

  HACCEL hAccelTable =
      LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_HOLEPUNCHEXAMPLE));

  MSG msg;

  // 主消息循环:
  while (GetMessage(&msg, nullptr, 0, 0)) {
    if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }

  return (int)msg.wParam;
}
