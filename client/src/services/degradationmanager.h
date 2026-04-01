#pragma once
#include <QObject>
#include <QTimer>
#include <cstdint>
#include "../infrastructure/network/AdaptiveBitrate.h"
#include "../infrastructure/itransportmanager.h"

class SystemStateMachine;

/**
 * 自适应降级管理器（《客户端架构设计》§5.1 完整实现）。
 *
 * 6级降级策略（快降级慢升级，3s迟滞）：
 *   LEVEL_0_FULL:         全质量（正常运行）
 *   LEVEL_1_HIGH:         轻微降级（码率-25%）
 *   LEVEL_2_MEDIUM:       中等降级（码率-50%，辅助摄像头关闭）
 *   LEVEL_3_LOW:          严重降级（仅主摄像头，低分辨率）
 *   LEVEL_4_MINIMAL:      最低功能（360P，无音频，15fps）
 *   LEVEL_5_SAFETY_STOP:  安全停车（停止驾驶，通知恢复后重连）
 *
 * 网络质量评分 → 降级判断 → 滞后过滤 → 级别变更 → 发出信号
 */
class DegradationManager : public QObject {
    Q_OBJECT

public:
    enum class DegradationLevel : uint8_t {
        FULL         = 0,
        HIGH         = 1,
        MEDIUM       = 2,
        LOW          = 3,
        MINIMAL      = 4,
        SAFETY_STOP  = 5,
    };

    struct LevelPolicy {
        DegradationLevel level;
        QString name;
        double   maxBitrateKbps;
        uint32_t videoFps;
        VideoResolution videoResolution;
        bool enableAuxCameras;
        bool enableAudio;
        bool enableFEC;
        double maxSpeedKmh;
    };

    struct DegradationConfig {
        double level1ThresholdScore = 0.75;
        double level2ThresholdScore = 0.60;
        double level3ThresholdScore = 0.45;
        double level4ThresholdScore = 0.30;
        double level5ThresholdScore = 0.15;
        int    hysteresisMs = 3000;
        int    checkIntervalMs = 500;
    };

    explicit DegradationManager(SystemStateMachine* fsm, QObject* parent = nullptr);

    void setConfig(const DegradationConfig& cfg);
    bool initialize();
    void start();
    void stop();

    void updateNetworkQuality(const NetworkQuality& quality);
    DegradationLevel currentLevel() const { return m_currentLevel; }
    LevelPolicy currentPolicy() const;
    static LevelPolicy policyForLevel(DegradationLevel level);

signals:
    void levelChanged(DegradationLevel newLevel, DegradationLevel oldLevel);
    void bitrateChanged(uint32_t targetKbps);
    void maxSpeedChanged(double maxKmh);
    void safetyStopRequired();
    void auxiliaryCamerasEnabled(bool enabled);

private slots:
    void checkDegradation();

private:
    DegradationLevel calculateTargetLevel(double score) const;
    bool shouldUpgrade(DegradationLevel target) const;
    bool shouldDowngrade(DegradationLevel target) const;
    void applyLevel(DegradationLevel level);

    DegradationConfig m_config;
    SystemStateMachine* m_fsm = nullptr;
    QTimer m_checkTimer;

    DegradationLevel m_currentLevel = DegradationLevel::FULL;
    DegradationLevel m_pendingLevel = DegradationLevel::FULL;
    int64_t m_pendingLevelSince = 0;
    double m_currentScore = 1.0;
};
