#pragma once
#include <QByteArray>
#include <vector>
#include <cstdint>

/**
 * 前向纠错编解码器（《客户端架构设计》§3.1.1）。
 * 使用 XOR 奇偶校验方案（生产中建议替换为 Reed-Solomon 或 raptor codes）。
 * redundancy = 0.3 时每 10 个原始包附加 3 个修复包。
 */
class FECEncoder {
public:
    explicit FECEncoder(double redundancy = 0.3);

    // 编码：返回 [原始包 + 修复包]
    std::vector<QByteArray> encode(const std::vector<QByteArray>& packets);

    // 解码：从收到的包（可能有丢失）还原原始包
    // receivedMask: 位掩码，bit i=1 表示收到第 i 包
    std::vector<QByteArray> decode(const std::vector<QByteArray>& received,
                                    uint32_t receivedMask, int originalCount);

    double redundancy() const { return m_redundancy; }
    void setRedundancy(double r) { m_redundancy = r; }

private:
    int repairCount(int originalCount) const;

    double m_redundancy;
};
