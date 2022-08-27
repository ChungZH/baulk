//
#ifndef BAULK_DOCK_UI_HPP
#define BAULK_DOCK_UI_HPP
#include <atlbase.h>
#include <atlwin.h>
#include <atlctl.h>
#include <d2d1_3.h>
#include <d2d1helper.h>
#include <d2d1svg.h>
#include <dwrite.h>
#include <wincodec.h>
#include <vector>
#include <bela/phmap.hpp>
#include <bela/base.hpp>
#include <baulk/graphics.hpp>
#include <baulk/vs/searcher.hpp>
#include "resource.h"

#ifndef SYSCOMMAND_ID_HANDLER
#define SYSCOMMAND_ID_HANDLER(id, func)                                                                                \
  if (uMsg == WM_SYSCOMMAND && id == LOWORD(wParam)) {                                                                 \
    bHandled = TRUE;                                                                                                   \
    lResult = func(HIWORD(wParam), LOWORD(wParam), (HWND)lParam, bHandled);                                            \
    if (bHandled)                                                                                                      \
      return TRUE;                                                                                                     \
  }
#endif

namespace baulk::dock {

constexpr const wchar_t *AppWindowName = L"Baulk.Dock.UI.Window";

using WindowTraits = CWinTraits<WS_OVERLAPPEDWINDOW, WS_EX_APPWINDOW | WS_EX_WINDOWEDGE>;

struct Label {
  Label(LONG left, LONG top, LONG right, LONG bottom, const wchar_t *text) : text(text) {
    layout.left = left;
    layout.top = top;
    layout.right = right;
    layout.bottom = bottom;
  }
  D2D1_RECT_F F() const {
    return D2D1::RectF((float)layout.left, (float)layout.top, (float)layout.right, (float)layout.bottom);
  }
  const wchar_t *data() const { return text.data(); }
  UINT32 length() const { return static_cast<UINT32>(text.size()); }
  bool empty() const { return text.empty(); }
  RECT layout;
  std::wstring text;
};

struct EnvNode {
  EnvNode() = default;
  EnvNode(std::wstring_view d, std::wstring_view v) : Desc(d), Value(v) {}
  std::wstring Desc;
  std::wstring Value;
};

struct BaulkDockTable {
  std::vector<std::wstring> archTarget;
  std::vector<EnvNode> envInstance;
  BaulkDockTable &Append(std::wstring_view d, std::wstring_view v) {
    envInstance.emplace_back(d, v);
    return *this;
  }
  BaulkDockTable &Append(EnvNode &&node) {
    envInstance.emplace_back(std::move(node));
    return *this;
  }
};

struct Widget {
  std::wstring text;
  HWND hWnd{nullptr};
  RECT layout;
  bool mono{false};
  int Height() const { return layout.bottom - layout.top; }
  int Width() const { return layout.right - layout.left; }
  const wchar_t *data() const { return text.data(); }
  auto size() const { return text.size(); }
  bool Enable(bool enable) { return ::EnableWindow(hWnd, enable ? TRUE : FALSE) == TRUE; }
};

class MainWindow : public CWindowImpl<MainWindow, CWindow, WindowTraits> {
private:
  HRESULT CreateDeviceIndependentResources();
  HRESULT InitializeControl();
  HRESULT CreateDeviceResources();
  void DiscardDeviceResources();
  HRESULT OnRender();
  D2D1_SIZE_U CalculateD2DWindowSize();
  void OnResize(UINT width, UINT height);
  ///////////
  bool InitializeBase(bela::error_code &ec);
  bool LoadPlacement(WINDOWPLACEMENT &placement);
  void SavePlacement(const WINDOWPLACEMENT &placement);
  INT_PTR OnCustomDraw(const NMCUSTOMDRAW *customDraw);
  void DrawButtonText(const NMCUSTOMDRAW *customDraw);

public:
  MainWindow(HINSTANCE hInstance) : hInst(hInstance) {}
  ~MainWindow();
  LRESULT InitializeWindow();
  DECLARE_WND_CLASS(AppWindowName)
  BEGIN_MSG_MAP(MainWindow)
  MESSAGE_HANDLER(WM_CREATE, OnCreate)
  MESSAGE_HANDLER(WM_CLOSE, OnClose)
  MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
  MESSAGE_HANDLER(WM_SIZE, OnSize)
  MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChanged)
  MESSAGE_HANDLER(WM_PAINT, OnPaint)
  MESSAGE_HANDLER(WM_CTLCOLORDLG, OnCtlColorStatic)
  MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnCtlColorStatic)
  MESSAGE_HANDLER(WM_NOTIFY, OnNotify)
  SYSCOMMAND_ID_HANDLER(IDM_BAULK_DOCK_ABOUT, OnSysMemuAbout)
  COMMAND_ID_HANDLER(IDC_BUTTON_STARTENV, OnStartupEnv)
  END_MSG_MAP()
  LRESULT OnCreate(UINT nMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandle);
  LRESULT OnDestroy(UINT nMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandle);
  LRESULT OnClose(UINT nMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandle);
  LRESULT OnSize(UINT nMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandle);
  LRESULT OnDpiChanged(UINT nMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandle);
  LRESULT OnPaint(UINT nMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandle);
  LRESULT OnCtlColorStatic(UINT nMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandle);
  LRESULT OnNotify(UINT nMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandle);
  LRESULT OnSysMemuAbout(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL &bHandled);
  LRESULT OnStartupEnv(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL &bHandled);
  ////
private:
  ID2D1Factory *m_pFactory{nullptr};
  IDWriteTextFormat *writeTextFormat{nullptr};
  IDWriteFactory *writeFactory{nullptr};
  // Image write
  IWICImagingFactory2 *wicFactory{nullptr};
  ID2D1Bitmap *bitmap{nullptr};
  //
  ID2D1HwndRenderTarget *renderTarget{nullptr};
  ID2D1SolidColorBrush *textBrush{nullptr};
  ID2D1SolidColorBrush *borderBrush{nullptr};
  baulk::windows::PersonalizeThemes themes;
  bela::windows_version systemVersion;
  HICON hIcon{nullptr};
  /// member
  HINSTANCE hInst{nullptr};
  HFONT hFont{nullptr};
  HFONT hMonoFont{nullptr};
  // combobox
  Widget vsInstanceBox;
  Widget archTargetBox;
  Widget envInstanceBox;
  // checkbox
  Widget makeCleanupEnvBox;
  // button about
  Widget hbaulkenv;
  std::vector<Label> labels;
  baulk::vs::vs_instances_t vsInstances;
  std::wstring baulkroot;
  BaulkDockTable tables;
  HBRUSH hBrush{nullptr};
  int dpiX{0};
  int dpiY{0};
};
} // namespace baulk::dock
#endif
