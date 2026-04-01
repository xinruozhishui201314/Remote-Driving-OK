#pragma once

#include <string>

/**
 * WHIP Demo 发布器（MVP）：
 * - 不负责真正的 WebRTC 媒体推流；
 * - 只负责按照 WHIP 规范对 ZLMediaKit 发送一次 HTTP POST SDP，
 *   并打印返回的 SDP Answer 片段，用于打通「信令 + URL」链路。
 *
 * 后续可将固定 SDP 替换为 WebRTC 库（如 GStreamer webrtcbin 或 libwebrtc）生成的 Offer。
 */
class WhipPublisherDemo {
public:
    /**
     * 运行一次 WHIP Demo：
     * @param whip_url 形如 whip://host:port/index/api/webrtc?app=teleop&stream=VIN-SESSION&type=push
     *                 函数内部会转换为 http://... 进行实际 HTTP POST。
     * @return true 表示 HTTP 请求成功并收到 2xx 响应。
     */
    static bool run_once(const std::string &whip_url);

    /**
     * 从 Backend/Keycloak 自动获取 token 和 session，提取 media.whip，
     * 然后执行一次 WHIP Demo 请求。
     *
     * 用于在 Docker 联合集群中自动打通链路：
     *   VehicleSide -> Backend -> Keycloak -> Backend -> ZLMediaKit
     *
     * 环境变量（可选，均有默认值）：
     *   BACKEND_BASE_URL  默认 http://backend:8080
     *   KEYCLOAK_BASE_URL 默认 http://keycloak:8080
     *   TEST_VIN          默认 E2ETESTVIN0000001
     *
     * 注意：仅用于联调验证，不用于正式车端逻辑。
     */
    static bool run_once_via_backend();
};

