#if 0
#include "whip_publisher.h"

#include <curl/curl.h>
#include <iostream>
#include <string>

namespace {

size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    auto *out = static_cast<std::string *>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string to_http_url(const std::string &whip_url) {
    // 简单替换前缀：whip:// -> http://
    const std::string prefix = "whip://";
    if (whip_url.rfind(prefix, 0) == 0) {
        return "http://" + whip_url.substr(prefix.size());
    }
    // 已经是 http/https 的情况直接返回
    return whip_url;
}

// 一个极简的占位 SDP，用于打通 HTTP 流程；后续会被真实 WebRTC Offer 替换。
const char *kDummySdp =
    "v=0\r\n"
    "o=- 0 0 IN IP4 127.0.0.1\r\n"
    "s=Teleop WHIP Demo\r\n"
    "t=0 0\r\n";

} // namespace

static bool run_once_legacy(const std::string &whip_url) {
    std::string http_url = to_http_url(whip_url);

    std::cout << "[Vehicle-side][ZLM][Push][WHIP] 使用 URL: " << http_url << std::endl;

    CURL *curl = curl_easy_init();
    if (!curl) {
        std::cerr << "[Vehicle-side][ZLM][Push][WHIP] 初始化 libcurl 失败" << std::endl;
        return false;
    }

    std::string response;
    CURLcode res;

    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/sdp");
    headers = curl_slist_append(headers, "Accept: application/sdp");

    curl_easy_setopt(curl, CURLOPT_URL, http_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, kDummySdp);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(std::strlen(kDummySdp)));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "[Vehicle-side][ZLM][Push][WHIP] HTTP 请求失败: " << curl_easy_strerror(res)
                  << ", code=" << static_cast<int>(res) << std::endl;
        return false;
    }

    std::cout << "[Vehicle-side][ZLM][Push][WHIP] HTTP 响应码: " << http_code << std::endl;
    if (!response.empty()) {
        std::string preview = response.substr(0, 120);
        std::cout << "[Vehicle-side][ZLM][Push][WHIP] 响应前 120 字符:\n"
                  << preview << std::endl;
    } else {
        std::cout << "[Vehicle-side][ZLM][Push][WHIP] 响应体为空" << std::endl;
    }

    return http_code >= 200 && http_code < 300;
}
#endif

#include "whip_publisher.h"

#include <curl/curl.h>
#include <iostream>
#include <string>
#include <cstring>

namespace {

// 简单写入回调，将响应体保存在 std::string 中
size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    auto *out = static_cast<std::string *>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

// 将 whip:// 前缀替换为 http://（MVP 简化实现）
std::string normalize_whip_url(const std::string &whip_url) {
    const std::string prefix = "whip://";
    if (whip_url.rfind(prefix, 0) == 0) {
        return "http://" + whip_url.substr(prefix.size());
    }
    return whip_url;
}

// 一个极简占位 SDP，用于打通 HTTP 流程；后续会被真实 WebRTC Offer 取代
std::string dummy_sdp() {
    return
        "v=0\r\n"
        "o=- 0 0 IN IP4 127.0.0.1\r\n"
        "s=WHIP-Demo\r\n"
        "t=0 0\r\n"
        "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=rtpmap:96 H264/90000\r\n";
}

} // namespace

bool WhipPublisherDemo::run_once(const std::string &whip_url) {
    std::string http_url = normalize_whip_url(whip_url);
    std::string sdp = dummy_sdp();

    std::cout << "[Vehicle-side][ZLM][Push][WHIP] 准备向 ZLMediaKit 发送 WHIP 请求: " << http_url << std::endl;

    CURL *curl = curl_easy_init();
    if (!curl) {
        std::cerr << "[Vehicle-side][ZLM][Push][WHIP] 初始化 libcurl 失败" << std::endl;
        return false;
    }

    std::string response_body;
    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/sdp");
    headers = curl_slist_append(headers, "Accept: application/sdp");

    curl_easy_setopt(curl, CURLOPT_URL, http_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, sdp.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(sdp.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "[Vehicle-side][ZLM][Push][WHIP] HTTP 请求失败: " << curl_easy_strerror(res) << std::endl;
        return false;
    }

    std::cout << "[Vehicle-side][ZLM][Push][WHIP] HTTP 响应码: " << http_code << std::endl;
    if (!response_body.empty()) {
        std::cout << "[Vehicle-side][ZLM][Push][WHIP] SDP Answer 片段（前 256 字符）:" << std::endl;
        std::cout << response_body.substr(0, 256) << std::endl;
    }

    return http_code >= 200 && http_code < 300;
}

// 简单 POST x-www-form-urlencoded，返回 body
static bool http_post_form(const std::string &url,
                           const std::string &form_body,
                           std::string &out_body) {
    CURL *curl = curl_easy_init();
    if (!curl) return false;

    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

    out_body.clear();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form_body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(form_body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out_body);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return res == CURLE_OK;
}

// 简单 POST JSON（无 body），仅设置 Authorization 头，返回 body
static bool http_post_with_bearer(const std::string &url,
                                  const std::string &token,
                                  std::string &out_body) {
    CURL *curl = curl_easy_init();
    if (!curl) return false;

    struct curl_slist *headers = nullptr;
    std::string auth = "Authorization: Bearer " + token;
    headers = curl_slist_append(headers, auth.c_str());
    headers = curl_slist_append(headers, "Accept: application/json");

    out_body.clear();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out_body);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return res == CURLE_OK;
}

// 极简 JSON parser：从 {"...","key":"value",...} 中取出 value（不处理转义）
static std::string extract_json_string(const std::string &body,
                                       const std::string &key) {
    std::string pattern = "\"" + key + "\":\"";
    auto pos = body.find(pattern);
    if (pos == std::string::npos) return {};
    pos += pattern.size();
    auto end = body.find('"', pos);
    if (end == std::string::npos) return {};
    return body.substr(pos, end - pos);
}

bool WhipPublisherDemo::run_once_via_backend() {
    const char *backend_env = std::getenv("BACKEND_BASE_URL");
    const char *keycloak_env = std::getenv("KEYCLOAK_BASE_URL");
    const char *vin_env = std::getenv("TEST_VIN");

    std::string backend_base = backend_env && std::strlen(backend_env) > 0
                               ? backend_env
                               : "http://backend:8080";
    std::string keycloak_base = keycloak_env && std::strlen(keycloak_env) > 0
                                ? keycloak_env
                                : "http://keycloak:8080";
    std::string vin = vin_env && std::strlen(vin_env) > 0
                      ? vin_env
                      : "E2ETESTVIN0000001";

    std::cout << "[Vehicle-side][ZLM][Push][WHIP] 使用 Backend/Keycloak 自动获取 media.whip" << std::endl;
    std::cout << "[Vehicle-side][ZLM][Push][WHIP] BACKEND_BASE_URL=" << backend_base
              << ", KEYCLOAK_BASE_URL=" << keycloak_base
              << ", TEST_VIN=" << vin << std::endl;

    // 1) 从 Keycloak 获取 access_token
    std::string token_url = keycloak_base + "/realms/teleop/protocol/openid-connect/token";
    std::string form = "grant_type=password&client_id=teleop-client"
                       "&username=e2e-test&password=e2e-test-password";
    std::string token_body;
    if (!http_post_form(token_url, form, token_body)) {
        std::cerr << "[Vehicle-side][ZLM][Push][WHIP] 向 Keycloak 请求 token 失败" << std::endl;
        return false;
    }
    std::string access_token = extract_json_string(token_body, "access_token");
    if (access_token.empty()) {
        std::cerr << "[Vehicle-side][ZLM][Push][WHIP] 无法从 Keycloak 响应中解析 access_token" << std::endl;
        return false;
    }

    // 2) 调用 Backend 创建 session，获取 sessionId + media.whip
    std::string sessions_url = backend_base + "/api/v1/vins/" + vin + "/sessions";
    std::string session_body;
    if (!http_post_with_bearer(sessions_url, access_token, session_body)) {
        std::cerr << "[Vehicle-side][ZLM][Push][WHIP] 调用 Backend 创建 session 失败" << std::endl;
        return false;
    }
    std::string session_id = extract_json_string(session_body, "sessionId");
    if (session_id.empty()) {
        std::cerr << "[Vehicle-side][ZLM][Push][WHIP] 无法从 Backend 响应中解析 sessionId" << std::endl;
        std::cerr << "[Vehicle-side][ZLM][Push][WHIP] 响应体片段: " << session_body.substr(0, 256) << std::endl;
        return false;
    }
    std::string whip_url = extract_json_string(session_body, "whip");
    if (whip_url.empty()) {
        std::cerr << "[Vehicle-side][ZLM][Push][WHIP] 无法从 Backend 响应中解析 media.whip" << std::endl;
        std::cerr << "[Vehicle-side][ZLM][Push][WHIP] 响应体片段: " << session_body.substr(0, 256) << std::endl;
        return false;
    }

    std::cout << "[Vehicle-side][ZLM][Push][WHIP] 从 Backend 获取的 sessionId: " << session_id << std::endl;
    std::cout << "[Vehicle-side][ZLM][Push][WHIP] 从 Backend 获取的 WHIP URL: " << whip_url << std::endl;

    // 3) 先对会话加锁：POST /api/v1/sessions/{sessionId}/lock
    std::string lock_url = backend_base + "/api/v1/sessions/" + session_id + "/lock";
    std::string lock_body;
    if (!http_post_with_bearer(lock_url, access_token, lock_body)) {
        std::cerr << "[Vehicle-side][ZLM][Push][WHIP] 调用 /sessions/{id}/lock 失败" << std::endl;
        return false;
    }
    if (lock_body.find("\"locked\":true") == std::string::npos) {
        std::cerr << "[Vehicle-side][ZLM][Push][WHIP] 会话未成功加锁，响应: "
                  << lock_body.substr(0, 256) << std::endl;
        return false;
    }
    std::cout << "[Vehicle-side][ZLM][Push][WHIP] 会话加锁成功，开始发送 WHIP 请求..." << std::endl;

    // 4) 使用现有 run_once 流程对 ZLMediaKit 发起 WHIP 请求
    return run_once(whip_url);
}


