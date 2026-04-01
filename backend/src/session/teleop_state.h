#pragma once
#ifndef TELEOP_SESSION_TELEOP_STATE_H
#define TELEOP_SESSION_TELEOP_STATE_H

#include <string>
#include <map>
#include <memory>
#include <chrono>
#include <queue>

#include "protocol/message_types.h"
#include "telemetry_data.h"

namespace teleopsession {

/**
 * 会话状态机（根据 project_spec.md §9 安全状态机）
 */
enum class SessionState {
    UNINITIALIZED = 0,
    IDLE,            /* 无会话 */
    ARMED,           /* 会话已创建但未满足条件（死手为false 或未收到有效指令）*/
    ACTIVE,          /* 正常控制 */
    SAFE_STOP,       /* 看门狗/断链/严重故障 */
    STOPPED,          /* 安全停车完成（刹死） */
    FAILED            /* 会话创建失败 */
    FAULT_LOCKED       /* 锁存故障状态，必须人工清除 */
};

/**
 * 会话状态机 - 字符串转换辅助
 */
inline const char* toString(SessionState state) {
    switch(state) {
        case UNINITIALIZED: return "UNINITIALIZED";
        case IDLE: return "IDLE";
        case ARMED: return "ARMED";
        case ACTIVE: return "ACTIVE";
        case SAFE_STOP: return "SAFE_STOP";
        case STOPPED: return "STOPPED";
        case FAULT_LOCKED: return "FAULT_LOCKED";
        case FAILED: return "FAILED";
        default: return "UNKNOWN";
    }
}

/**
 * 会话状态机
 *
 * 负责管理远程驾驶会话的状态转换
 */
class TeleopStateMachine {
public:
    /**
     * 状态转换：创建会话
     * @param vin VIN 硬
     * @param sessionSecret 会话密钥（用于签名）
     * @return SessionInfo（包含初始状态）
     */
    static SessionInfo createSession(
        const std::string& vin,
        const std::string& sessionSecret
    );

    /**
     * 状态转换：激活会话
     * @param sessionId 会话 ID
     * @return 新状态
     */
    SessionState armSession(const std::string& sessionId);

    /**
     * 状态转换：结束会话
     * @param sessionId 会话ID
     @return 新状态
     */
    SessionState endSession(const std::string& sessionId, const std::string& reason);

    /**
     * 状态转换：强制安全停车
     * @param sessionId 会话ID
     * @return 新状态
     */
    SessionState triggerSafeStop(const std::string& sessionId, SessionState currentState);

    /**
     * 状态转换：锁存故障
     * @param sessionId 会话ID
     * @param errorCode 故障码
     * @return 新状态
     */
    SessionState lockSession(const std::string& sessionId, const FaultCode& errorCode);

    /**
     * 获取当前状态
     * @return 状态枚举
     */
    SessionState getState() const;

    /**
     * 重置状态（用于测试）
     */
    static void resetForTest(const std::string& sessionId);

private:
    static std::unordered_map<std::string, SessionInfo> sessionStates_;

    // 状态转换辅助函数
    SessionState transitionState(
        const std::string& sessionId,
        SessionState currentState,
        SessionState newState
    );
};

/**
 * 状态机测试用（单元测试用）
 */
class SessionStateMachineTest {
public:
    static bool testStateTransitions();
};

} // namespace teleopsession

#endif // TELEOP_SESSION_TELEOP_STATE_H
