///
#include <bela/base.hpp>
#include <bela/escapeargv.hpp>
#include <shlwapi.h>
#include <shobjidl_core.h>
#include <Shobjidl.h>
#include <ShlObj.h>
#include <wrl.h>
#include <wrl/module.h>
#include <wrl/client.h>
#include <wrl/implements.h>
#include <wrl/module.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.ApplicationModel.Resources.Core.h>
#include <filesystem>

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
  if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(hModule);
  }
  return TRUE;
}

EXTERN_C IMAGE_DOS_HEADER __ImageBase;
inline HINSTANCE GetModuleInstanceHandle() { return reinterpret_cast<HINSTANCE>(&__ImageBase); }

std::optional<std::wstring> LookupModuleBasePath(HMODULE hModule, bela::error_code &ec) {
  std::wstring buffer;
  buffer.resize(MAX_PATH);
  for (;;) {
    const auto blen = buffer.size();
    auto n = GetModuleFileNameW(hModule, buffer.data(), static_cast<DWORD>(blen));
    if (n == 0) {
      ec = bela::make_system_error_code();
      return std::nullopt;
    }
    buffer.resize(n);
    if (n < static_cast<DWORD>(blen)) {
      break;
    }
  }
  return std::make_optional(std::move(buffer));
}

class __declspec(uuid("951C66D1-98D1-4007-8AE4-412E985D70DB")) ExtractWithUnscrew
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom | Microsoft::WRL::InhibitFtmBase>,
          IExplorerCommand> {
public:
  // IExplorerCommand
  IFACEMETHODIMP GetTitle(_In_opt_ IShellItemArray *items, _Outptr_result_nullonfailure_ PWSTR *ppszTitle) {
    return SHStrDup(L"Extract Here", ppszTitle);
  }
  IFACEMETHODIMP GetIcon(_In_opt_ IShellItemArray *, _Outptr_result_nullonfailure_ PWSTR *ppszIcon) {
    constexpr std::wstring_view UnscrewExe{L"unscrew.exe"};
    bela::error_code ec;
    auto basePath = LookupModuleBasePath(reinterpret_cast<HMODULE>(GetModuleInstanceHandle()), ec);
    if (!basePath) {
      return 0;
    }
    std::filesystem::path modulePath{*basePath};
    modulePath.replace_filename(UnscrewExe);
    return SHStrDupW(modulePath.c_str(), ppszIcon);
  }

  IFACEMETHODIMP GetToolTip(_In_opt_ IShellItemArray *, _Outptr_result_nullonfailure_ PWSTR *ppszInfoTip) {
    *ppszInfoTip = nullptr;
    return E_NOTIMPL;
  }

  IFACEMETHODIMP GetCanonicalName(_Out_ GUID *guidCommandName) {
    *guidCommandName = __uuidof(this);
    return S_OK;
  }

  HRESULT GetState(IShellItemArray * /*psiItemArray*/, BOOL /*fOkToBeSlow*/, EXPCMDSTATE *pCmdState) {
    // compute the visibility of the verb here, respect "fOkToBeSlow" if this is
    // slow (does IO for example) when called with fOkToBeSlow == FALSE return
    // E_PENDING and this object will be called back on a background thread with
    // fOkToBeSlow == TRUE

    // We however don't need to bother with any of that, so we'll just return
    // ECS_ENABLED.

    *pCmdState = ECS_ENABLED;
    return S_OK;
  }

  IFACEMETHODIMP Invoke(_In_opt_ IShellItemArray *psiItemArray, _In_opt_ IBindCtx *) noexcept {
    if (psiItemArray == nullptr) {
      return S_FALSE;
    }
    std::vector<std::wstring> FilePaths;
    DWORD count = {0};
    if (!SUCCEEDED(psiItemArray->GetCount(&count))) {
      return S_FALSE;
    }
    for (DWORD i = 0; i < count; i++) {
      winrt::com_ptr<IShellItem> psi;
      if (!SUCCEEDED(psiItemArray->GetItemAt(0, psi.put()))) {
        return S_FALSE;
      }
      LPWSTR pszName;
      if (!SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszName))) {
        return S_FALSE;
      }
      FilePaths.emplace_back(pszName);
      CoTaskMemFree(pszName);
    }

    bela::error_code ec;
    auto basePath = LookupModuleBasePath(reinterpret_cast<HMODULE>(GetModuleInstanceHandle()), ec);
    if (!basePath) {
      return S_FALSE;
    }
    std::filesystem::path unscrewPath{*basePath};
    unscrewPath.replace_filename(L"unscrew.exe");
    std::error_code e;
    if (!std::filesystem::exists(unscrewPath, e)) {
      return S_FALSE;
    }
    bela::EscapeArgv ea;
    ea.Assign(unscrewPath.native()).Append(L"--flat");
    for (const auto &p : FilePaths) {
      ea.Append(p);
    }
    PROCESS_INFORMATION pi;
    STARTUPINFOEXW siEx{0};
    siEx.StartupInfo.cb = sizeof(STARTUPINFOEX);

    if (CreateProcessW(unscrewPath.c_str(), // lpApplicationName
                       ea.data(),
                       nullptr,                                                   // lpProcessAttributes
                       nullptr,                                                   // lpThreadAttributes
                       false,                                                     // bInheritHandles
                       EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, // dwCreationFlags
                       nullptr,                                                   // lpEnvironment
                       nullptr,
                       &siEx.StartupInfo, // lpStartupInfo
                       &pi                // lpProcessInformation
                       ) != TRUE) {
      return S_FALSE;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return S_OK;
  }

  HRESULT GetFlags(EXPCMDFLAGS *pFlags) {
    *pFlags = ECF_DEFAULT;
    return S_OK;
  }

  HRESULT EnumSubCommands(IEnumExplorerCommand **ppEnum) {
    *ppEnum = nullptr;
    return E_NOTIMPL;
  }
};

CoCreatableClass(ExtractWithUnscrew) CoCreatableClassWrlCreatorMapInclude(ExtractWithUnscrew); //

STDAPI DllGetActivationFactory(_In_ HSTRING activatableClassId, _COM_Outptr_ IActivationFactory **factory) {
  return Microsoft::WRL::Module<Microsoft::WRL::ModuleType::InProc>::GetModule().GetActivationFactory(
      activatableClassId, factory);
}

STDAPI DllCanUnloadNow() {
  return Microsoft::WRL::Module<Microsoft::WRL::InProc>::GetModule().GetObjectCount() == 0 ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(_In_ REFCLSID rclsid, _In_ REFIID riid, _COM_Outptr_ void **instance) {
  return Microsoft::WRL::Module<Microsoft::WRL::InProc>::GetModule().GetClassObject(rclsid, riid, instance);
}
