/**
 * JWT 校验实现：base64url 解码 payload，校验 iss/aud/exp。
 * 不验签（开发模式）。
 */

#include "auth/jwt_validator.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>

namespace teleop {
namespace auth {

namespace {

std::string base64url_decode(const std::string& in) {
    std::string s = in;
    for (char& c : s) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    while (s.size() % 4) s += '=';
    // 简单 base64 解码（无外部依赖）；填充 '=' 不参与查表，否则会被误当作 0 导致错误输出
    const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    auto idx = [&tbl](char c) -> std::optional<unsigned> {
        const char* p = std::strchr(tbl, static_cast<unsigned char>(c));
        return p ? std::optional<unsigned>(static_cast<unsigned>(p - tbl)) : std::nullopt;
    };
    size_t pad = 0;
    if (s.size() >= 2 && s.back() == '=') { pad++; if (s.size() >= 2 && s[s.size()-2] == '=') pad++; }
    std::string out;
    out.reserve((s.size() / 4) * 3);
    for (size_t i = 0; i + 4 <= s.size(); i += 4) {
        const bool is_last_with_pad = (i + 4 == s.size()) && (pad > 0);
        unsigned n;
        if (is_last_with_pad && pad == 2) {
            auto a = idx(s[i]), b = idx(s[i+1]);
            if (!a || !b) throw std::runtime_error("base64url_decode: invalid char");
            n = (*a << 18) | (*b << 12);
            out.push_back(static_cast<char>((n >> 16) & 0xff));
        } else if (is_last_with_pad && pad == 1) {
            auto a = idx(s[i]), b = idx(s[i+1]), c = idx(s[i+2]);
            if (!a || !b || !c) throw std::runtime_error("base64url_decode: invalid char");
            n = (*a << 18) | (*b << 12) | (*c << 6);
            out.push_back(static_cast<char>((n >> 16) & 0xff));
            out.push_back(static_cast<char>((n >> 8) & 0xff));
        } else {
            auto a = idx(s[i]), b = idx(s[i+1]), c = idx(s[i+2]), d = idx(s[i+3]);
            if (!a || !b || !c || !d) throw std::runtime_error("base64url_decode: invalid char");
            n = (*a << 18) | (*b << 12) | (*c << 6) | *d;
            out.push_back(static_cast<char>((n >> 16) & 0xff));
            out.push_back(static_cast<char>((n >> 8) & 0xff));
            out.push_back(static_cast<char>(n & 0xff));
        }
    }
    return out;
}

}  // namespace

std::optional<std::string> validate_jwt_claims(
    const std::string& token,
    const std::vector<std::string>& expected_issuers,
    const std::vector<std::string>& expected_aud) {
    if (token.empty()) return std::nullopt;
    std::string payload_b64;
    {
        size_t d1 = token.find('.');
        size_t d2 = token.find('.', d1 + 1);
        if (d1 == std::string::npos || d2 == std::string::npos) return std::nullopt;
        payload_b64 = token.substr(d1 + 1, d2 - d1 - 1);
    }
    std::string payload_json;
    try {
        payload_json = base64url_decode(payload_b64);
    } catch (...) {
        return std::nullopt;
    }
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(payload_json);
    } catch (...) {
        return std::nullopt;
    }
    // exp: 必须存在且 > 当前时间（允许 60 秒时钟偏差）
    auto exp_it = j.find("exp");
    if (exp_it == j.end() || !exp_it->is_number()) return std::nullopt;
    auto now = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    if (exp_it->get<int64_t>() < now - 60) return std::nullopt;
    // iss: 须在可接受列表中
    auto iss_it = j.find("iss");
    if (iss_it == j.end() || !iss_it->is_string()) return std::nullopt;
    std::string iss = iss_it->get<std::string>();
    bool iss_ok = false;
    for (const auto& e : expected_issuers) { if (iss == e) { iss_ok = true; break; } }
    if (!iss_ok) return std::nullopt;
    // aud: 若 expected_aud 非空且 token 含 aud，则须为其中之一；无 aud 时放行（兼容部分 Keycloak 配置）
    if (!expected_aud.empty()) {
        auto aud_it = j.find("aud");
        if (aud_it != j.end()) {
            bool ok = false;
            if (aud_it->is_string()) {
                std::string a = aud_it->get<std::string>();
                for (const auto& e : expected_aud) { if (a == e) { ok = true; break; } }
            } else if (aud_it->is_array()) {
                for (const auto& a : *aud_it) {
                    if (!a.is_string()) continue;
                    std::string as = a.get<std::string>();
                    for (const auto& e : expected_aud) { if (as == e) { ok = true; break; } }
                    if (ok) break;
                }
            }
            if (!ok) return std::nullopt;
        }
    }
    return payload_json;
}

}  // namespace auth
}  // namespace teleop
