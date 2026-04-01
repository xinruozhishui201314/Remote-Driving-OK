/**
 * JWT 校验器 - M1 第二批
 * 仅校验 iss / aud / exp（不验签，开发/测试模式）。
 * 生产环境应增加 JWKS 验签。
 */

#pragma once

#include <string>
#include <optional>
#include <vector>

namespace teleop {
namespace auth {

/// 校验 JWT 并返回 payload 的 JSON 字符串；失败返回 nullopt。
/// expected_issuers: 可接受的 iss 列表（如 http://keycloak:8080/realms/teleop、http://localhost:8080/realms/teleop）
/// expected_aud: 可选，若为空则只校验 iss 与 exp；若 token 无 aud 则放行（兼容部分 Keycloak 配置）
std::optional<std::string> validate_jwt_claims(
    const std::string& token,
    const std::vector<std::string>& expected_issuers,
    const std::vector<std::string>& expected_aud);

}  // namespace auth
}  // namespace teleop
