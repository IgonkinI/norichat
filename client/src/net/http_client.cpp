#include "http_client.h"

#include <curl/curl.h>
#include <cstdio>

// ─── libcurl global init (RAII, once per process) ─────────────────────────────
namespace {
struct CurlGlobal {
    CurlGlobal()  { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobal() { curl_global_cleanup(); }
} g_curl;
} // namespace

// ─── Write callback ───────────────────────────────────────────────────────────

static size_t write_cb(char* data, size_t size, size_t nmemb, void* userp) {
    static_cast<std::string*>(userp)->append(data, size * nmemb);
    return size * nmemb;
}

// ─── HttpClient ───────────────────────────────────────────────────────────────

HttpClient::HttpClient(const std::string& host, int port)
    : host_(host), port_(port) {}

std::optional<HttpResponse>
HttpClient::post(const std::string& path, const std::string& json_body,
                 const std::string& auth_token) {
    return do_request("POST", path, json_body, auth_token);
}

std::optional<HttpResponse>
HttpClient::get(const std::string& path, const std::string& auth_token) {
    return do_request("GET", path, "", auth_token);
}

std::optional<HttpResponse>
HttpClient::do_request(const std::string& method, const std::string& path,
                       const std::string& body,   const std::string& auth_token) {
    CURL* curl = curl_easy_init();
    if (!curl) { fprintf(stderr, "[http] curl_easy_init failed\n"); return std::nullopt; }

    std::string url = "http://" + host_ + ":" + std::to_string(port_) + path;
    std::string response_body;

    curl_easy_setopt(curl, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &response_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

    curl_slist* hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
    if (!auth_token.empty()) {
        std::string auth_hdr = "Authorization: Bearer " + auth_token;
        hdrs = curl_slist_append(hdrs, auth_hdr.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);

    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST,          1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS,    body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    }

    CURLcode res = curl_easy_perform(curl);
    long status  = 0;
    if (res == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    else
        fprintf(stderr, "[http] %s %s : %s\n",
                method.c_str(), url.c_str(), curl_easy_strerror(res));

    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) return std::nullopt;
    return HttpResponse{(int)status, std::move(response_body)};
}
