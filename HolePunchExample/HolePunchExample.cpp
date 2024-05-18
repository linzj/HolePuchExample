// HolePunchExample.cpp : 定义应用程序的入口点。
//

#include "HolePunchExample.h"

#include <assert.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <dcomp.h>
#include <dxgi1_3.h>
#include <wrl/client.h>  // For ComPtr

#include <functional>
#include <memory>
#include <random>
#include <vector>

#include "framework.h"

using Microsoft::WRL::ComPtr;

#define MAX_LOADSTRING 100
#define ASSERT_HRESULT_SUCCEEDED(expr)                                   \
  {                                                                      \
    hr = expr;                                                           \
    if (FAILED(hr)) {                                                    \
      OutputDebugStringFmt(#expr " failed %lx.", static_cast<long>(hr)); \
      throw std::exception();                                            \
    }                                                                    \
  }

#define ASSERT_NE(a, b)                                       \
  if ((a) == (b)) {                                           \
    OutputDebugStringFmt(#a " should not equals to " #b "!"); \
    throw std::exception();                                   \
  }

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
void InitInstanceChild(HINSTANCE hInstance, HWND parent,
                       std::shared_ptr<ChildWindow>* child_window, bool ontop);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);

void OutputDebugStringFmt(const char* format, ...) {
  char buffer[1024];
  va_list args;
  va_start(args, format);
  vsprintf_s(buffer, format, args);
  va_end(args);
  OutputDebugStringA(buffer);
  OutputDebugStringA("\n");
}

// Function to generate a random float in range [0.0f, 1.0f]
float getRandomFloat() {
  static std::random_device rd;   // Obtain a random number from hardware
  static std::mt19937 gen(rd());  // Seed the generator
  static std::uniform_real_distribution<float> dis(0.0f,
                                                   1.0f);  // Define the range
  return dis(gen);
}

ComPtr<IDCompositionVisual2> NewBackgroundVisual(
    IDCompositionDevice3* dcomp_device, ID3D11Device* d3d11_device, int width,
    int height, const float background_fill_color[4]) {
  HRESULT hr;
  ComPtr<IDCompositionSurface> background_fill;
  ASSERT_HRESULT_SUCCEEDED(dcomp_device->CreateSurface(
      width, height, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED,
      &background_fill));

  RECT update_rect = {0, 0, width, height};
  ComPtr<ID3D11Texture2D> update_texture;
  POINT update_offset;
  ASSERT_HRESULT_SUCCEEDED(background_fill->BeginDraw(
      &update_rect, IID_PPV_ARGS(&update_texture), &update_offset));
  {
    ComPtr<ID3D11RenderTargetView> render_target;
    ASSERT_HRESULT_SUCCEEDED(d3d11_device->CreateRenderTargetView(
        update_texture.Get(), nullptr, &render_target));
    ASSERT_NE(render_target, nullptr);
    ComPtr<ID3D11DeviceContext> d3d11_device_context;
    d3d11_device->GetImmediateContext(&d3d11_device_context);
    ASSERT_NE(d3d11_device_context, nullptr);
    d3d11_device_context->ClearRenderTargetView(render_target.Get(),
                                                background_fill_color);
  }
  ASSERT_HRESULT_SUCCEEDED(background_fill->EndDraw());

  ComPtr<IDCompositionVisual2> result;
  ASSERT_HRESULT_SUCCEEDED(dcomp_device->CreateVisual(&result));
  // The content of a visual is always drawn behind its children, so we'll
  // use it for the background fill.
  ASSERT_HRESULT_SUCCEEDED(result->SetContent(background_fill.Get()));
  return result;
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
  void OnPaint();
  void RepaintChildren();
  void AddChild(std::shared_ptr<ChildWindow>& child);
  void AddVisualOnTop(IDCompositionVisual2*);
  void DrawHalfRect(float[4]);
  ID3D11Device* d3d11_device() { return d3d11_device_.Get(); }
  ID3D11DeviceContext* context() { return context_.Get(); }
  IDXGIFactory2* factory() { return factory_.Get(); }
  ID3D11VertexShader* vertex_shader() { return vertex_shader_.Get(); }
  ID3D11PixelShader* pixel_shader() { return pixel_shader_.Get(); }
  ID3D11InputLayout* input_layout() { return input_layout_.Get(); }
  IDCompositionDevice3* dcomp_device() { return dcomp_device_.Get(); }
  IDCompositionVisual2* root_visual() { return root_visual_.Get(); }

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
  ComPtr<ID3D11Device> d3d11_device_;
  ComPtr<ID3D11DeviceContext> context_;
  ComPtr<IDXGIFactory2> factory_;
  ComPtr<ID3D11VertexShader> vertex_shader_;
  ComPtr<ID3D11PixelShader> pixel_shader_;
  ComPtr<ID3D11InputLayout> input_layout_;
  ComPtr<ID3D11Buffer> vertex_buffer_;
  // DComps
  ComPtr<IDCompositionDevice3> dcomp_device_;
  ComPtr<IDCompositionTarget> dcomp_target_;
  ComPtr<IDCompositionVisual2> root_visual_;
  ComPtr<IDCompositionVisual2> current_visual_;

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
  void OnParentSize(size_t x, size_t y, size_t width, size_t height);
  void OnPaint();
  void SetClearColor(float r, float g, float b, float a);
  void AddDrawColorGenerator(
      std::function<void(float[4], D3D11_VIEWPORT&)>&& draw_color_generator);

 private:
  void InitializePaintContext();
  HWND hwnd_parent_;
  ComPtr<ID3D11Device> d3d11_device_;
  ComPtr<ID3D11DeviceContext> context_;
  ComPtr<IDXGISwapChain1> swap_chain_;
  std::vector<std::function<void(float[4], D3D11_VIEWPORT&)>>
      draw_color_generators_;

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
  SetTimer(hwnd_, 0, 1000, nullptr);
}

void MainWindow::OnSize(size_t x, size_t y, size_t width, size_t height) {
  if (width == 0 || height == 0) return;
  for (auto& weak_child : children_) {
    auto child = weak_child.lock();
    if (child) child->OnParentSize(x, y, width, height);
  }
}

void MainWindow::OnPaint() { dcomp_device_->Commit(); }

void MainWindow::RepaintChildren() {
  for (auto& weak_child : children_) {
    auto child = weak_child.lock();
    if (child) child->OnPaint();
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
                         d3d11_device_.GetAddressOf(), nullptr,
                         context_.GetAddressOf());
  if (FAILED(hr)) {
    OutputDebugStringFmt("D3D11CreateDevice failed: hr: %lx", (long)hr);
    _exit(1);
  }
  // Set up debug layer to break on D3D11 errors
  ComPtr<ID3D11Debug> d3dDebug;
  d3d11_device_->QueryInterface(
      __uuidof(ID3D11Debug), reinterpret_cast<void**>(d3dDebug.GetAddressOf()));
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

    hResult = d3d11_device_->CreateVertexShader(
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr,
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

    hResult = d3d11_device_->CreatePixelShader(psBlob->GetBufferPointer(),
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

    HRESULT hResult = d3d11_device_->CreateInputLayout(
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
        d3d11_device_->CreateBuffer(&vertexBufferDesc, &vertexSubresourceData,
                                    vertex_buffer_.GetAddressOf());
    assert(SUCCEEDED(hResult));
  }

  ComPtr<IDXGIDevice> dxgi_device;
  hr = d3d11_device_.As(&dxgi_device);
  if (FAILED(hr)) {
    OutputDebugStringFmt("d3d device failed to cast to dxgi device: hr: %lx",
                         (long)hr);
    _exit(1);
  }

  // Create DComp
  ComPtr<IDCompositionDesktopDevice> desktop_device;
  hr = DCompositionCreateDevice3(dxgi_device.Get(),
                                 IID_PPV_ARGS(desktop_device.GetAddressOf()));
  if (FAILED(hr)) {
    OutputDebugStringFmt("DCompositionCreateDevice failed: hr: %lx", (long)hr);
    _exit(1);
  }

  hr = desktop_device.As(&dcomp_device_);
  if (FAILED(hr)) {
    OutputDebugStringFmt(
        "dcomp device failed to cast to desktop device: hr: %lx", (long)hr);
    _exit(1);
  }

  hr = desktop_device->CreateTargetForHwnd(hwnd_, TRUE,
                                           dcomp_target_.GetAddressOf());
  if (FAILED(hr)) {
    OutputDebugStringFmt("CreateTargetForHwnd failed: hr: %lx", (long)hr);
    _exit(1);
  }

  hr = dcomp_device_->CreateVisual(&root_visual_);
  if (FAILED(hr)) {
    OutputDebugStringFmt("CreateRootVisual failed: hr: %lx", (long)hr);
    _exit(1);
  }

  hr = dcomp_target_->SetRoot(root_visual_.Get());

  if (FAILED(hr)) {
    OutputDebugStringFmt("SetRoot failed: hr: %lx", (long)hr);
    _exit(1);
  }

  if (1) {
    // Fill the background so we have a consistent backdrop instead of relying
    // on the color of the redirection surface when testing alpha blending. We
    // default to magenta to make it obvious when something shouldn't be
    // visible.
    const float background_fill_color[4] = {1.0f, 1.0f, 0.0f, 1.0f};
    RECT winRect;
    GetClientRect(hwnd_, &winRect);
    int width = winRect.right - winRect.left;
    int height = winRect.bottom - winRect.top;
    ComPtr<IDCompositionVisual2> background_visual =
        NewBackgroundVisual(dcomp_device_.Get(), d3d11_device_.Get(), width,
                            height, background_fill_color);
    AddVisualOnTop(background_visual.Get());
  }
}

void MainWindow::AddChild(std::shared_ptr<ChildWindow>& child) {
  children_.emplace_back(child);
}

void MainWindow::AddVisualOnTop(IDCompositionVisual2* visual) {
  HRESULT hr = root_visual_->AddVisual(visual, TRUE, current_visual_.Get());
  if (FAILED(hr)) {
    OutputDebugStringFmt("AddVisual failed: hr: %lx", (long)hr);
    _exit(1);
  }
  current_visual_ = visual;
}

void MainWindow::DrawHalfRect(float color[4]) {
  HRESULT hr;
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_->IASetInputLayout(input_layout());

  context_->VSSetShader(vertex_shader(), nullptr, 0);
  context_->PSSetShader(pixel_shader(), nullptr, 0);

  context_->IASetVertexBuffers(0, 1, vertex_buffer_address(), stride_address(),
                               offset_address());

  // Create rect_color_buffer
  ComPtr<ID3D11Buffer> rect_color_buffer;

  D3D11_BUFFER_DESC bufferDesc;
  ZeroMemory(&bufferDesc, sizeof(D3D11_BUFFER_DESC));
  bufferDesc.Usage = D3D11_USAGE_DEFAULT;
  bufferDesc.ByteWidth = sizeof(float[4]);
  bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  bufferDesc.CPUAccessFlags = 0;

  D3D11_SUBRESOURCE_DATA initData;
  initData.pSysMem = color;
  ASSERT_HRESULT_SUCCEEDED(
      d3d11_device_->CreateBuffer(&bufferDesc, &initData, &rect_color_buffer));
  ID3D11Buffer* rect_color_buffer_raw = rect_color_buffer.Get();
  context_->PSSetConstantBuffers(0, 1, &rect_color_buffer_raw);
  context_->Draw(num_verts(), 0);
}

ChildWindow::ChildWindow(HWND parent) : hwnd_parent_(parent) {}

ChildWindow::~ChildWindow() {}

void ChildWindow::OnParentSize(size_t x, size_t y, size_t width,
                               size_t height) {
  InitializePaintContext();
  // Must paint to get the content.
  OnPaint();
  InvalidateRect(hwnd_parent_, nullptr, FALSE);
}

void ChildWindow::OnPaint() {
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

  hr = d3d11_device_->CreateRenderTargetView(back_buffer.Get(), nullptr,
                                             render_target_view.GetAddressOf());
  if (FAILED(hr)) {
    OutputDebugStringFmt("CreateRenderTargetView failed: hr: %lx", (long)hr);
    _exit(1);
  }

  context_->OMSetRenderTargets(1, render_target_view.GetAddressOf(), nullptr);

  float clearColor[4] = {r_, g_, b_, a_};
  context_->ClearRenderTargetView(render_target_view.Get(), clearColor);

  // Clear center if on top
  MainWindow* main_window = WindowBase::FromHWND<MainWindow>(hwnd_parent_);
  for (auto& generator : draw_color_generators_) {
    float color[4];
    D3D11_VIEWPORT viewport;
    generator(color, viewport);
    context_->RSSetViewports(1, &viewport);
    main_window->DrawHalfRect(color);
  }

  // Present back buffer in vsync
  swap_chain_->Present(0, 0);
}

void ChildWindow::InitializePaintContext() {
  HRESULT hr;
  MainWindow* main_window = WindowBase::FromHWND<MainWindow>(hwnd_parent_);
  d3d11_device_ = main_window->d3d11_device();
  context_ = main_window->context();
  swap_chain_.Reset();
  RECT winRect;
  GetClientRect(hwnd_parent_, &winRect);
  int width = winRect.right - winRect.left;
  int height = winRect.bottom - winRect.top;
  // Init dcomp
  ComPtr<IDCompositionDevice3> dcomp_device = main_window->dcomp_device();
  ComPtr<IDCompositionVisual2> visual;

  ASSERT_HRESULT_SUCCEEDED(dcomp_device->CreateVisual(&visual));

  DXGI_SWAP_CHAIN_DESC1 scd;
  ZeroMemory(&scd, sizeof(scd));
  scd.SampleDesc.Count = 1;
  scd.SampleDesc.Quality = 0;
  scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  scd.Scaling = DXGI_SCALING_STRETCH;
  scd.Width = width;
  scd.Height = height;
  scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  scd.Stereo = FALSE;
  scd.BufferUsage = DXGI_USAGE_BACK_BUFFER | DXGI_USAGE_RENDER_TARGET_OUTPUT;
  scd.Flags = 0;
  scd.BufferCount = 4;
  scd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

  ASSERT_HRESULT_SUCCEEDED(
      main_window->factory()->CreateSwapChainForComposition(
          d3d11_device_.Get(), &scd, NULL, &swap_chain_));
  ASSERT_HRESULT_SUCCEEDED(visual->SetContent(swap_chain_.Get()));

  // Setup the clip
  if (0) {
    float background_fill_color[4] = {r_, g_, b_, a_};
    ComPtr<IDCompositionVisual2> background_visual;
    background_visual =
        NewBackgroundVisual(dcomp_device.Get(), d3d11_device_.Get(), width,
                            height, background_fill_color);
    ASSERT_HRESULT_SUCCEEDED(
        visual->AddVisual(background_visual.Get(), FALSE, nullptr));
  }

  main_window->AddVisualOnTop(visual.Get());
}

void ChildWindow::SetClearColor(float r, float g, float b, float a) {
  r_ = r;
  g_ = g;
  b_ = b;
  a_ = a;
}

void ChildWindow::AddDrawColorGenerator(
    std::function<void(float[4], D3D11_VIEWPORT&)>&& draw_color_generator) {
  draw_color_generators_.emplace_back(std::move(draw_color_generator));
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
  wcex.lpszMenuName = nullptr;
  wcex.lpszClassName = szWindowClass;
  wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

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

  HWND hWnd =
      CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, szWindowClass, szTitle,
                      WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, CW_USEDEFAULT, 0,
                      nullptr, nullptr, hInstance, main_window.get());

  if (!hWnd) {
    return FALSE;
  }
  main_window->OnCreate(hWnd);

  ShowWindow(hWnd, nCmdShow);
  std::unique_ptr<std::shared_ptr<ChildWindow>> child_window =
      std::make_unique<std::shared_ptr<ChildWindow>>(
          std::make_shared<ChildWindow>(hWnd));
  // set bottom child background to green.
  (*child_window)->SetClearColor(0.0f, 1.0f, 0.0f, 1.0f);
  (*child_window)
      ->AddDrawColorGenerator(
          std::move([hWnd](float outcolor[4], D3D11_VIEWPORT& viewport) {
            // Generate random color values for red, green, and blue
            float r = getRandomFloat();
            float g = getRandomFloat();
            float b = getRandomFloat();
            OutputDebugStringFmt("rand color: %f, %f %f.", r, g, b);
            float color[4] = {r, g, b, 1.0f};
            memcpy(outcolor, color, sizeof(float[4]));
            RECT winRect;
            GetClientRect(hWnd, &winRect);
            viewport = D3D11_VIEWPORT{0.0f,
                                      0.0f,
                                      (FLOAT)(winRect.right - winRect.left),
                                      (FLOAT)(winRect.bottom - winRect.top),
                                      0.0f,
                                      1.0f};
          }));
  main_window->AddChild(*child_window);
  InitInstanceChild(hInstance, hWnd, child_window.release(), false);
  if (true) {
    child_window = std::make_unique<std::shared_ptr<ChildWindow>>(
        std::make_shared<ChildWindow>(hWnd));
    // set top child background to red.
    (*child_window)->SetClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    (*child_window)
        ->AddDrawColorGenerator(
            std::move([hWnd](float outcolor[4], D3D11_VIEWPORT& viewport) {
              float color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
              memcpy(outcolor, color, sizeof(float[4]));
              RECT winRect;
              GetClientRect(hWnd, &winRect);
              viewport = D3D11_VIEWPORT{0.0f,
                                        0.0f,
                                        (FLOAT)(winRect.right - winRect.left),
                                        (FLOAT)(winRect.bottom - winRect.top),
                                        0.0f,
                                        1.0f};
            }));
    (*child_window)
        ->AddDrawColorGenerator(
            std::move([hWnd](float outcolor[4], D3D11_VIEWPORT& viewport) {
              float color[4] = {0.1f, 0.1f, 0.1f, 0.0f};
              memcpy(outcolor, color, sizeof(float[4]));
              RECT winRect;
              GetClientRect(hWnd, &winRect);
              viewport = D3D11_VIEWPORT{
                  static_cast<FLOAT>(winRect.right - winRect.left) / 4.0f,
                  static_cast<FLOAT>(winRect.bottom - winRect.top) / 4.0f,
                  static_cast<FLOAT>(winRect.right - winRect.left) / 2.0f,
                  static_cast<FLOAT>(winRect.bottom - winRect.top) / 2.0f,
                  0.0f,
                  1.0f};
            }));
    main_window->AddChild(*child_window);
    InitInstanceChild(hInstance, hWnd, child_window.release(), true);
  }
  // Add debug visual to test src-over alpha blending.

  if (0) {
    // Fill the background so we have a consistent backdrop instead of relying
    // on the color of the redirection surface when testing alpha blending. We
    // default to magenta to make it obvious when something shouldn't be
    // visible.
    const float background_fill_color[4] = {0.0f, 0.0f, 1.0f, 0.0f};
    RECT winRect;
    GetClientRect(hWnd, &winRect);
    int width = winRect.right - winRect.left;
    int height = winRect.bottom - winRect.top;
    ComPtr<IDCompositionVisual2> background_visual = NewBackgroundVisual(
        main_window->dcomp_device(), main_window->d3d11_device(), width, height,
        background_fill_color);
    // ComPtr<IDCompositionVisual3> background_visual3;
    // ASSERT_HRESULT_SUCCEEDED(background_visual.As(&background_visual3));
    // ASSERT_HRESULT_SUCCEEDED(background_visual3->SetOpacity(0.4f));
    main_window->AddVisualOnTop(background_visual.Get());
  }
  main_window.release();

  UpdateWindow(hWnd);

  return TRUE;
}

void InitInstanceChild(HINSTANCE hInstance, HWND parent,
                       std::shared_ptr<ChildWindow>* child_window, bool ontop) {
  (*child_window)->set_on_top(ontop);

  // Resize the child now.
  RECT winRect;
  GetClientRect(parent, &winRect);
  (*child_window)
      ->OnParentSize(0, 0, winRect.right - winRect.left,
                     winRect.bottom - winRect.top);
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
      MainWindow* main_window = WindowBase::FromHWND<MainWindow>(hWnd);
      main_window->OnPaint();
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
    case WM_TIMER: {
      MainWindow* main_window = WindowBase::FromHWND<MainWindow>(hWnd);
      main_window->RepaintChildren();
      SetTimer(hWnd, 0, 1000, nullptr);
    } break;
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
