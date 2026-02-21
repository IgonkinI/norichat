#pragma once
#include <string>
#include <optional>

// Synchronous HTTP/HTTPS client backed by WinHTTP.
// Phase 1: plain HTTP only (server runs on ws://).

struct HttpResponse {
    int         status_code = 0;
    std::string body;
};

class HttpClient {
public:
    HttpClient(const std::string& host, int port);
    ~HttpClient() = default;

    // POST `path` with JSON body. Optionally add a Bearer token.
    std::optional<HttpResponse> post(const std::string& path,
                                     const std::string& json_body,
                                     const std::string& auth_token = "");

    // GET `path`. Optionally add a Bearer token.
    std::optional<HttpResponse> get(const std::string& path,
                                    const std::string& auth_token = "");

private:
    std::string host_;
    int         port_;

    std::optional<HttpResponse> request(const std::wstring& method,
                                        const std::string&  path,
                                        const std::string&  body,
                                        const std::string&  auth_token);
};
