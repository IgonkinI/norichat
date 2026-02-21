#include "http_client.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <vector>

// WinHTTP is available without additional dependencies on Windows.
#pragma comment(lib, "winhttp.lib")

// ─── Helpers ──────────────────────────────────────────────────────────────────

static std::wstring to_wstring(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), len);
    return w;
}

// ─── HttpClient ───────────────────────────────────────────────────────────────

HttpClient::HttpClient(const std::string& host, int port)
    : host_(host), port_(port) {}

std::optional<HttpResponse>
HttpClient::post(const std::string& path,
                 const std::string& json_body,
                 const std::string& auth_token) {
    return request(L"POST", path, json_body, auth_token);
}

std::optional<HttpResponse>
HttpClient::get(const std::string& path, const std::string& auth_token) {
    return request(L"GET", path, "", auth_token);
}

std::optional<HttpResponse>
HttpClient::request(const std::wstring& method,
                    const std::string&  path,
                    const std::string&  body,
                    const std::string&  auth_token) {
    HINTERNET hSession = WinHttpOpen(L"NoriChat-Client/1.0",
                                     WINHTTP_ACCESS_TYPE_NO_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        fprintf(stderr, "[http] WinHttpOpen failed: %lu\n", GetLastError());
        return std::nullopt;
    }

    HINTERNET hConnect = WinHttpConnect(hSession,
                                        to_wstring(host_).c_str(),
                                        (INTERNET_PORT)port_, 0);
    if (!hConnect) {
        fprintf(stderr, "[http] WinHttpConnect failed: %lu\n", GetLastError());
        WinHttpCloseHandle(hSession);
        return std::nullopt;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect,
                                            method.c_str(),
                                            to_wstring(path).c_str(),
                                            nullptr,
                                            WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            0 /* no HTTPS for Phase 1 */);
    if (!hRequest) {
        fprintf(stderr, "[http] WinHttpOpenRequest failed: %lu\n", GetLastError());
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return std::nullopt;
    }

    // Build headers
    std::wstring headers = L"Content-Type: application/json\r\n";
    if (!auth_token.empty())
        headers += L"Authorization: Bearer " + to_wstring(auth_token) + L"\r\n";

    WinHttpAddRequestHeaders(hRequest, headers.c_str(), (DWORD)-1L,
                             WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

    // Send
    BOOL ok = WinHttpSendRequest(hRequest,
                                 WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 body.empty() ? nullptr : (LPVOID)body.data(),
                                 (DWORD)body.size(),
                                 (DWORD)body.size(), 0);
    if (!ok) {
        fprintf(stderr, "[http] WinHttpSendRequest failed: %lu\n", GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return std::nullopt;
    }

    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        fprintf(stderr, "[http] WinHttpReceiveResponse failed: %lu\n", GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return std::nullopt;
    }

    // Status code
    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    WinHttpQueryHeaders(hRequest,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX,
                        &status_code, &status_size,
                        WINHTTP_NO_HEADER_INDEX);

    // Read body
    std::string resp_body;
    DWORD bytes_avail = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytes_avail) && bytes_avail > 0) {
        std::vector<char> buf(bytes_avail + 1, 0);
        DWORD bytes_read = 0;
        if (WinHttpReadData(hRequest, buf.data(), bytes_avail, &bytes_read))
            resp_body.append(buf.data(), bytes_read);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return HttpResponse{(int)status_code, std::move(resp_body)};
}
