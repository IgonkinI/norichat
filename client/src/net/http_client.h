#pragma once
#include <string>
#include <optional>

// Synchronous HTTP client backed by libcurl.
// Cross-platform: Windows and Linux.

struct HttpResponse {
    int         status_code = 0;
    std::string body;
};

class HttpClient {
public:
    HttpClient(const std::string& host, int port);
    ~HttpClient() = default;

    std::optional<HttpResponse> post(const std::string& path,
                                     const std::string& json_body,
                                     const std::string& auth_token = "");

    std::optional<HttpResponse> get(const std::string& path,
                                    const std::string& auth_token = "");

private:
    std::string host_;
    int         port_;

    std::optional<HttpResponse> do_request(const std::string& method,
                                           const std::string& path,
                                           const std::string& body,
                                           const std::string& auth_token);
};
