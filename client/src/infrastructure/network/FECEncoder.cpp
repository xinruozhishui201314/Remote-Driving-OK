#include "FECEncoder.h"

#include <algorithm>
#include <cstring>

FECEncoder::FECEncoder(double redundancy) : m_redundancy(redundancy) {}

int FECEncoder::repairCount(int originalCount) const {
  return std::max(1, static_cast<int>(std::ceil(originalCount * m_redundancy)));
}

std::vector<QByteArray> FECEncoder::encode(const std::vector<QByteArray>& packets) {
  if (packets.empty())
    return {};

  const int n = static_cast<int>(packets.size());
  const int r = repairCount(n);

  // Find max packet size for uniform length XOR
  size_t maxLen = 0;
  for (const auto& p : packets)
    maxLen = std::max(maxLen, static_cast<size_t>(p.size()));

  std::vector<QByteArray> result = packets;

  // Generate r repair packets via XOR grouping
  for (int i = 0; i < r; ++i) {
    QByteArray repair(static_cast<int>(maxLen), '\0');
    // XOR every packet that maps to repair group i
    for (int j = 0; j < n; ++j) {
      if (j % r == i) {
        const auto& src = packets[j];
        for (int k = 0; k < static_cast<int>(maxLen); ++k) {
          repair[k] = static_cast<char>(static_cast<uint8_t>(repair[k]) ^
                                        static_cast<uint8_t>(k < src.size() ? src[k] : 0));
        }
      }
    }
    result.push_back(std::move(repair));
  }
  return result;
}

std::vector<QByteArray> FECEncoder::decode(const std::vector<QByteArray>& received,
                                           uint32_t receivedMask, int originalCount) {
  // Simple recovery: XOR repair packets can recover 1 lost packet per group
  // For production, replace with systematic RS codes
  std::vector<QByteArray> result;
  result.resize(originalCount);

  const int r = repairCount(originalCount);

  for (int i = 0; i < originalCount; ++i) {
    if (receivedMask & (1u << i)) {
      if (i < static_cast<int>(received.size())) {
        result[i] = received[i];
      }
    }
  }

  // Attempt XOR recovery for missing packets
  for (int g = 0; g < r; ++g) {
    int missingIdx = -1;
    bool canRecover = true;

    // Check which packets in this group are missing
    for (int j = g; j < originalCount; j += r) {
      if (!(receivedMask & (1u << j))) {
        if (missingIdx == -1) {
          missingIdx = j;
        } else {
          canRecover = false;  // two missing in group, can't recover
          break;
        }
      }
    }

    if (missingIdx == -1 || !canRecover)
      continue;

    // Recover via XOR
    const int repairIndex = originalCount + g;
    if (repairIndex >= static_cast<int>(received.size()))
      continue;

    size_t maxLen = 0;
    for (const auto& p : result)
      maxLen = std::max(maxLen, static_cast<size_t>(p.size()));
    maxLen = std::max(maxLen, static_cast<size_t>(received[repairIndex].size()));

    QByteArray recovered(static_cast<int>(maxLen), '\0');
    // Start with repair packet
    const auto& rep = received[repairIndex];
    for (int k = 0; k < static_cast<int>(maxLen); ++k) {
      recovered[k] = k < rep.size() ? rep[k] : 0;
    }
    // XOR with all other packets in group
    for (int j = g; j < originalCount; j += r) {
      if (j == missingIdx)
        continue;
      if (!(receivedMask & (1u << j)))
        continue;
      const auto& src = result[j];
      for (int k = 0; k < static_cast<int>(maxLen); ++k) {
        recovered[k] = static_cast<char>(static_cast<uint8_t>(recovered[k]) ^
                                         static_cast<uint8_t>(k < src.size() ? src[k] : 0));
      }
    }
    result[missingIdx] = std::move(recovered);
  }

  return result;
}
