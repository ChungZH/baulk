#ifndef BAULK_NET_NATIVE_HPP
#define BAULK_NET_NATIVE_HPP
#include <bela/env.hpp>
#include <bela/strip.hpp>
#include <baulk/net/types.hpp>
#include <schannel.h>
#include <ws2tcpip.h>
#include <winhttp.h>

struct WINHTTP_SECURITY_INFO_X {
  SecPkgContext_ConnectionInfo ConnectionInfo;
  SecPkgContext_CipherInfo CipherInfo;
};

#ifndef WINHTTP_OPTION_SECURITY_INFO
#define WINHTTP_OPTION_SECURITY_INFO 151
#endif

#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif

#ifndef WINHTTP_PROTOCOL_FLAG_HTTP3
#define WINHTTP_PROTOCOL_FLAG_HTTP3 0x2
#endif

namespace baulk::net::native {

inline bela::error_code make_net_error_code(std::wstring_view prefix = L"") {
  bela::error_code ec;
  ec.code = GetLastError();
  if (ec.code >= WINHTTP_ERROR_BASE && ec.code <= WINHTTP_ERROR_LAST) {
    ec.message = bela::resolve_module_error_message(L"winhttp.dll", ec.code, prefix);
  } else {
    ec.message = bela::resolve_system_error_message(ec.code, prefix);
  }
  return ec;
}

struct url {
  std::wstring host;
  std::wstring filename;
  std::wstring uri;
  int nPort{80};
  int nScheme{INTERNET_SCHEME_HTTPS};
  inline DWORD TlsFlag() const { return nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0; }
};

inline std::optional<url> crack_url(std::wstring_view us, bela::error_code &ec) {
  URL_COMPONENTSW uc;
  ZeroMemory(&uc, sizeof(uc));
  uc.dwStructSize = sizeof(uc);
  uc.dwSchemeLength = (DWORD)-1;
  uc.dwHostNameLength = (DWORD)-1;
  uc.dwUrlPathLength = (DWORD)-1;
  uc.dwExtraInfoLength = (DWORD)-1;
  if (WinHttpCrackUrl(us.data(), static_cast<DWORD>(us.size()), 0, &uc) != TRUE) {
    ec = make_net_error_code();
    return std::nullopt;
  }
  std::wstring_view urlpath{uc.lpszUrlPath, uc.dwUrlPathLength};
  return std::make_optional(
      url{.host = {uc.lpszHostName, uc.dwHostNameLength},
          .filename = decoded_url_path_name(urlpath),
          .uri = bela::StringCat(urlpath, std::wstring_view{uc.lpszExtraInfo, uc.dwExtraInfoLength}),
          .nPort = uc.nPort,
          .nScheme = uc.nScheme});
}

class status_context {
public:
  status_context(bool debugMode_ = false) : debugMode{debugMode_} {}
  status_context(const status_context &) = delete;
  status_context &operator=(const status_context &) = delete;
  auto addressof() const { return reinterpret_cast<DWORD_PTR>(this); }
  // status_context_callback: please do not call this function directly
  void status_context_callback(HINTERNET hInternet, DWORD dwInternetStatus, LPVOID lpvStatusInformation,
                               DWORD dwStatusInformationLength) {
    switch (dwInternetStatus) {
    case WINHTTP_CALLBACK_STATUS_RESOLVING_NAME:
      if (lpvStatusInformation != nullptr) {
        resolving_name.assign(reinterpret_cast<LPWSTR>(lpvStatusInformation), dwStatusInformationLength);
      }
      break;
    case WINHTTP_CALLBACK_STATUS_NAME_RESOLVED:
      if (lpvStatusInformation != nullptr) {
        resolved_name.assign(reinterpret_cast<LPWSTR>(lpvStatusInformation), dwStatusInformationLength);
        DbgPrint(L"Resolve %v (%v)... %v", resolving_name, resolving_name, resolved_name);
      }
      break;
    case WINHTTP_CALLBACK_STATUS_CONNECTED_TO_SERVER:
      if (lpvStatusInformation != nullptr) {
        auto address = std::wstring_view{reinterpret_cast<LPWSTR>(lpvStatusInformation), dwStatusInformationLength};
        DbgPrint(L"Connecting to %v (%v)|%v| connected.", resolving_name, resolving_name, address);
      }
      break;
    case WINHTTP_CALLBACK_STATUS_REDIRECT:
      if (lpvStatusInformation != nullptr) {
        location.assign(reinterpret_cast<LPWSTR>(lpvStatusInformation), dwStatusInformationLength);
        DbgPrint(L"Location: %v [following]", location);
      }
      break;
    default:
      break;
    }
  }
  std::optional<url> crack_location_url() {
    if (location.empty()) {
      return std::nullopt;
    }
    bela::error_code ec;
    return crack_url(location, ec);
  }

private:
  std::wstring location;
  std::wstring resolving_name;
  std::wstring resolved_name;
  bool debugMode;
  template <typename... Args> bela::ssize_t DbgPrint(const wchar_t *fmt, const Args &...args) {
    if (!debugMode) {
      return 0;
    }
    const bela::format_internal::FormatArg arg_array[] = {args...};
    std::wstring str;
    str.append(L"\x1b[33m* ");
    bela::format_internal::StrAppendFormatInternal(&str, fmt, arg_array, sizeof...(args));
    if (str.back() == '\n') {
      str.pop_back();
    }
    str.append(L"\x1b[0m\n");
    return bela::terminal::WriteAuto(stderr, str);
  }
  bela::ssize_t DbgPrint(const wchar_t *fmt) {
    if (!debugMode) {
      return 0;
    }
    std::wstring_view msg(fmt);
    if (!msg.empty() && msg.back() == '\n') {
      msg.remove_suffix(1);
    }
    return bela::terminal::WriteAuto(stderr, bela::StringCat(L"\x1b[33m* ", msg, L"\x1b[0m\n"));
  }
};

inline std::optional<std::wstring> resolve_filename(std::wstring_view es) {
  constexpr std::wstring_view fns = L"filename";
  constexpr std::wstring_view fnsu = L"filename*";
  constexpr std::wstring_view utf8 = L"UTF-8";
  auto s = bela::StripAsciiWhitespace(es);
  auto pos = s.find('=');
  if (pos == std::wstring_view::npos) {
    return std::nullopt;
  }
  auto field = bela::StripAsciiWhitespace(s.substr(0, pos));
  auto v = bela::StripAsciiWhitespace(s.substr(pos + 1));
  if (field == fns) {
    bela::ConsumePrefix(&v, L"\"");
    bela::ConsumeSuffix(&v, L"\"");
    return std::make_optional<>(std::wstring(v));
  }
  if (field != fnsu) {
    return std::nullopt;
  }
  if (pos = v.find(L"''"); pos == std::wstring_view::npos) {
    bela::ConsumePrefix(&v, L"\"");
    bela::ConsumeSuffix(&v, L"\"");
    return std::make_optional<>(std::wstring(v));
  }
  if (bela::EqualsIgnoreCase(v.substr(0, pos), utf8)) {
    auto name = v.substr(pos + 2);
    return std::make_optional<>(bela::encode_into<char, wchar_t>(url_decode(name)));
  }
  // unsupported encoding
  return std::nullopt;
}

// https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Content-Disposition
// https://www.rfc-editor.org/rfc/rfc6266#section-5
inline std::optional<std::wstring> extract_filename(const headers_t &hkv) {
  auto it = hkv.find(L"Content-Disposition");
  if (it == hkv.end()) {
    return std::nullopt;
  }
  std::vector<std::wstring_view> pvv = bela::StrSplit(it->second, bela::ByChar(';'), bela::SkipEmpty());
  for (auto e : pvv) {
    if (auto result = resolve_filename(e); result) {
      return result;
    }
  }
  return std::nullopt;
}

// content_length
inline int64_t content_length(const headers_t &hkv) {
  if (auto it = hkv.find(L"Content-Length"); it != hkv.end()) {
    if (int64_t len = 0; bela::SimpleAtoi(bela::StripAsciiWhitespace(it->second), &len)) {
      return len;
    }
  }
  return -1;
}

inline bool enable_part_download(const headers_t &hkv) {
  if (auto it = hkv.find(L"Accept-Ranges"); it != hkv.end()) {
    return bela::EqualsIgnoreCase(bela::StripAsciiWhitespace(it->second), L"bytes");
  }
  return false;
}

class handle {
public:
  handle() = default;
  handle(HINTERNET h_) : h(h_) {}
  handle(const handle &) = delete;
  handle &operator=(const handle &) = delete;
  ~handle() {
    if (h != nullptr) {
      WinHttpCloseHandle(h);
    }
  }
  auto addressof() const { return h; }
  bool set_proxy_url(std::wstring &url) {
    WINHTTP_PROXY_INFOW proxy;
    proxy.dwAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
    proxy.lpszProxy = url.data();
    proxy.lpszProxyBypass = nullptr;
    return WinHttpSetOption(h, WINHTTP_OPTION_PROXY, &proxy, sizeof(proxy)) == TRUE;
  }
  void protocol_enable() {
    // ENABLE TLS 1.3
    DWORD secure_protocols(WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3);
    if (WinHttpSetOption(h, WINHTTP_OPTION_SECURE_PROTOCOLS, &secure_protocols, sizeof(secure_protocols)) != TRUE) {
      secure_protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
      WinHttpSetOption(h, WINHTTP_OPTION_SECURE_PROTOCOLS, &secure_protocols, sizeof(secure_protocols));
    }
    // ENABLE HTTP2 and HTTP3
    DWORD all_protocols(WINHTTP_PROTOCOL_FLAG_HTTP2 | WINHTTP_PROTOCOL_FLAG_HTTP3);
    if (WinHttpSetOption(h, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL, &all_protocols, sizeof(all_protocols)) != TRUE) {
      all_protocols = WINHTTP_PROTOCOL_FLAG_HTTP2;
      WinHttpSetOption(h, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL, &all_protocols, sizeof(all_protocols));
    }
  }
  void set_insecure_mode() {
    // Ignore check tls
    DWORD dwFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE |
                    SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
    WinHttpSetOption(h, WINHTTP_OPTION_SECURITY_FLAGS, &dwFlags, sizeof(dwFlags));
  }

  // fill header
  bool write_headers(const headers_t &hkv, const std::vector<std::wstring> &cookies, int64_t position, int64_t length,
                     bela::error_code &ec) {
    std::wstring flattened_headers;
    for (const auto &[key, value] : hkv) {
      bela::StrAppend(&flattened_headers, key, L": ", value, L"\r\n");
    }
    // part download
    if (position > 0) {
      // https://developer.mozilla.org/zh-CN/docs/Web/HTTP/Headers/Range
      bela::StrAppend(&flattened_headers, L"Range: bytes=", position, L"-");
    }
    if (!cookies.empty()) {
      bela::StrAppend(&flattened_headers, L"Cookie: ", bela::StrJoin(cookies, L"; "), L"\r\n");
    }

    if (flattened_headers.empty()) {
      return true;
    }
    if (WinHttpAddRequestHeaders(h, flattened_headers.data(), static_cast<DWORD>(flattened_headers.size()),
                                 WINHTTP_ADDREQ_FLAG_ADD) != TRUE) {
      ec = make_net_error_code();
      return false;
    }
    return true;
  }
  bool write_body(std::wstring_view body, std::wstring_view content_type, WINHTTP_STATUS_CALLBACK callback,
                  DWORD_PTR dwContext, bela::error_code &ec) {
    if (callback != nullptr) {
      WinHttpSetStatusCallback(h, callback,
                               WINHTTP_CALLBACK_FLAG_RESOLVE_NAME | WINHTTP_CALLBACK_STATUS_REDIRECT |
                                   WINHTTP_CALLBACK_STATUS_CONNECTED_TO_SERVER,
                               NULL);
    }
    if (body.empty()) {
      if (WinHttpSendRequest(h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, dwContext) != TRUE) {
        ec = make_net_error_code();
        return false;
      }
      return true;
    }
    auto addheader = bela::StringCat(L"Content-Type: ", content_type.empty() ? content_type : L"text/plain",
                                     L"\r\nContent-Length: ", body.size(), L"\r\n");
    if (WinHttpAddRequestHeaders(h, addheader.data(), static_cast<DWORD>(addheader.size()),
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE) != TRUE) {
      ec = make_net_error_code();
      return false;
    }
    if (WinHttpSendRequest(h, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           const_cast<LPVOID>(reinterpret_cast<LPCVOID>(body.data())), static_cast<DWORD>(body.size()),
                           static_cast<DWORD>(body.size()), dwContext) != TRUE) {
      ec = make_net_error_code();
      return false;
    }
    return true;
  }
  bool write_body(std::wstring_view body, std::wstring_view content_type, bela::error_code &ec) {
    return write_body(body, content_type, nullptr, 0, ec);
  }
  // recv_minimal_response: read minimal response --> status code and headers
  std::optional<minimal_response> recv_minimal_response(bela::error_code &ec) {
    if (WinHttpReceiveResponse(h, nullptr) != TRUE) {
      ec = make_net_error_code();
      return std::nullopt;
    }
    DWORD status_code = 0;
    DWORD dwSize = sizeof(status_code);
    if (WinHttpQueryHeaders(h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status_code, &dwSize,
                            nullptr) != TRUE) {
      ec = make_net_error_code(L"status code");
      return std::nullopt;
    }
    std::wstring status;
    status.resize(128);
    dwSize = static_cast<DWORD>(status.size() * 2);
    if (WinHttpQueryHeaders(h, WINHTTP_QUERY_STATUS_TEXT, nullptr, status.data(), &dwSize, nullptr) == TRUE) {
      status.resize(dwSize / 2);
    }
    dwSize = 0;
    WinHttpQueryHeaders(h, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER,
                        &dwSize, WINHTTP_NO_HEADER_INDEX); // ignore error
    std::wstring buffer;
    buffer.resize(dwSize / 2 + 1);
    if (WinHttpQueryHeaders(h, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, buffer.data(), &dwSize,
                            WINHTTP_NO_HEADER_INDEX) != TRUE) {
      ec = make_net_error_code(L"raw headers");
      return std::nullopt;
    }
    buffer.resize(dwSize / 2);
    DWORD dwOption = 0;
    dwSize = sizeof(dwOption);
    auto version = protocol_version::HTTP11;
    if (WinHttpQueryOption(h, WINHTTP_OPTION_HTTP_PROTOCOL_USED, &dwOption, &dwSize) == TRUE) {
      if ((dwOption & WINHTTP_PROTOCOL_FLAG_HTTP3) != 0) {
        version = protocol_version::HTTP3;
      } else if ((dwOption & WINHTTP_PROTOCOL_FLAG_HTTP2) != 0) {
        version = protocol_version::HTTP2;
      }
    }
    // split headers
    std::vector<std::wstring_view> hlines = bela::StrSplit(buffer, bela::ByString(L"\r\n"), bela::SkipEmpty());
    headers_t hdrs;
    for (size_t i = 1; i < hlines.size(); i++) {
      auto line = hlines[i];
      if (auto pos = line.find(':'); pos != std::wstring_view::npos) {
        auto k = bela::StripAsciiWhitespace(line.substr(0, pos));
        auto v = bela::StripAsciiWhitespace(line.substr(pos + 1));
        hdrs.emplace(k, v);
      }
    }
    return std::make_optional(minimal_response{.headers = std::move(hdrs),
                                               .status_code = static_cast<unsigned long>(status_code),
                                               .version = version,
                                               .status_text = std::move(status)});
  }
  //
  int64_t recv_completely(int64_t len, std::vector<char> &buffer, size_t max_body_size, bela::error_code &ec) {
    if (len == 0) {
      return 0;
    }
    // content-length
    if (len > 0) {
      auto total_size =
          static_cast<size_t>((std::min)(static_cast<uint64_t>(max_body_size), static_cast<uint64_t>(len)));
      buffer.resize(total_size);
      size_t download_size = 0;
      auto buf = buffer.data();
      do {
        DWORD dwSize = 0;
        if (WinHttpQueryDataAvailable(h, &dwSize) != TRUE) {
          ec = make_net_error_code();
          return -1;
        }
        auto recv_size = (std::min)(total_size - download_size, static_cast<size_t>(dwSize));
        if (WinHttpReadData(h, buf + download_size, static_cast<DWORD>(recv_size), &dwSize) != TRUE) {
          ec = make_net_error_code();
          return -1;
        }
        download_size += static_cast<size_t>(dwSize);
      } while (total_size > download_size);
      return download_size;
    }
    // chunk receive
    buffer.resize(256 * 1024); // 256kb buffer
    size_t download_size = 0;
    do {
      DWORD dwSize = 0;
      if (WinHttpQueryDataAvailable(h, &dwSize) != TRUE) {
        ec = make_net_error_code();
        return -1;
      }
      if (dwSize == 0) {
        break;
      }
      if (buffer.size() < static_cast<size_t>(dwSize) + download_size) {
        buffer.resize(download_size + static_cast<size_t>(dwSize));
      }
      if (WinHttpReadData(h, buffer.data() + download_size, dwSize, &dwSize) != TRUE) {
        ec = make_net_error_code();
        return -1;
      }
      download_size += dwSize;
    } while (download_size < max_body_size);
    return download_size;
  }

  // a session handle create a connection
  std::optional<handle> connect(std::wstring_view host, int port, bela::error_code &ec) {
    auto hConnect = WinHttpConnect(h, host.data(), port, 0);
    if (hConnect == nullptr) {
      ec = make_net_error_code();
      return std::nullopt;
    }
    return std::make_optional<handle>(hConnect);
  }
  // a connection handle open a request
  std::optional<handle> open_request(std::wstring_view method, std::wstring_view uri, DWORD dwFlags,
                                     bela::error_code &ec) {
    auto hRequest = WinHttpOpenRequest(h, method.data(), uri.data(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, dwFlags);
    if (hRequest == nullptr) {
      ec = make_net_error_code();
      return std::nullopt;
    }
    return std::make_optional<handle>(hRequest);
  }

private:
  friend std::optional<handle> make_session(std::wstring_view ua, bela::error_code &ec);
  HINTERNET h{nullptr};
};

// make a session handle
inline std::optional<handle> make_session(std::wstring_view ua, bela::error_code &ec) {
  auto hSession =
      WinHttpOpen(ua.data(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (hSession == nullptr) {
    ec = make_net_error_code();
    return std::nullopt;
  }
  return std::make_optional<handle>(hSession);
}

} // namespace baulk::net::native

#endif