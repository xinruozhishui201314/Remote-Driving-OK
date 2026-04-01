#include "teleop_state.h"
#include <unordered_map>
#include <string>

namespace teleopsession {

// Static storage for session states
static std::unordered_map<std::string, SessionInfo> sessionStates_;

/**
 * 状态转换辅助函数
 */
SessionState transitionState(
    const std::string& sessionId,
    SessionState currentState,
    SessionState newState
) {
    auto it = sessionStates_.find(sessionId);
    if (it != sessionStates_.end()) {
        // Validate state transition (Simple validation as per plan)
        // IDLE -> ARMED -> ACTIVE -> ...
        // For now, we allow transitions but log them for audit
        LOG_AUDIT("Session {} state transition: {} -> {}", sessionId, static_cast<int>(it->second.state), static_cast<int>(newState));
        it->second = newState;
    }
}

/**
 * 创建会话
 */
SessionInfo TeleopStateMachine::createSession(
    const std::string& vin,
    const std::string& sessionSecret
) {
    SessionInfo sessionInfo;
    sessionInfo.sessionId = sessionId;
    sessionInfo.vin = vin;
    sessionInfo.state = SessionState::ARMED;
    sessionInfo.startedAt = getCurrentTimestampMs();
    sessionInfo.lastHeartbeatAt = 0;
    sessionInfo.sessionSecret = sessionSecret;
    
    transitionState(sessionInfo.sessionId, SessionState::IDLE, SessionState::ARMED);
    
    return sessionInfo;
}

/**
 * 激活会话
 */
SessionState TeleopStateMachine::armSession(const std::string& sessionId) {
    return transitionState(sessionId, SessionState::ARMED, SessionState::ACTIVE);
}

/**
 * 结束会话
 */
SessionState TeleopStateMachine::endSession(
    const std::string& sessionId,
    const std::string& reason
) {
    SessionState newState = SessionState::ENDED;
    
    auto it = sessionStates_.find(sessionId);
    if (it != sessionStates_.end()) {
        it->second = newState;
        it->second.endedAt = getCurrentTimestampMs();
        // TODO: 记录会话结束事件
    }
    
    return newState;
}

/**
 * 触发安全停车
 */
SessionState TeleopStateMachine::triggerSafeStop(
    const std::string& sessionId,
    SessionState currentState
) {
    if (currentState == SessionState::ACTIVE) {
        newState = SessionState::SAFE_STOP;
    } else if (currentState == SessionState::FAULT_LOCKED) {
        newState = SessionState::STOPPED;
    } else if (currentState == SessionState::SAFE_STOP) {
        newState = SessionState::STOPPED;
    } else {
        return currentState;  // 不支持从当前状态转换
    }
    
    transitionState(sessionId, currentState, newState);
    
    return newState;
}

/**
 * 锁存故障
 */
SessionState TeleopStateMachine::lockSession(
    const std::string& sessionId,
    const FaultCode& errorCode
) {
    SessionState newState = SessionState::FAULT_LOCKED;
    
    auto it = sessionStates_.find(sessionId);
    if (it != sessionStates_.end()) {
        it->second = newState;
    }
    
    return newState;
}

/**
 * 获取当前状态
 */
SessionState TeleopStateMachine::getState() const {
    return SessionState::UNINITIALIZED;
}

/**
 * 重置状态（用于测试）
 */
void TeleopStateMachine::resetForTest(const std::string& sessionId) {
    sessionStates_.erase(sessionId);
}

} // namespace teleopsession
