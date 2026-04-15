#include "fault_code.h"
#include <vector>

namespace teleop::protocol {

std::map<std::string, FaultCode> FaultCodeManager::faultCodeMap_;

const FaultCode& FaultCodeManager::get(const std::string& code) {
    auto it = faultCodeMap_.find(code);
    if (it != faultCodeMap_.end()) {
        return it->second;
    }
    static FaultCode unknown{"UNKNOWN", "Unknown Fault", FaultSeverity::INFO, FaultDomain::TELEOP, false, "未知错误"};
    return unknown;
}

bool FaultCodeManager::exists(const std::string& code) {
    return faultCodeMap_.find(code) != faultCodeMap_.end();
}

void FaultCodeManager::registerFaultCode(const FaultCode& faultCode) {
    faultCodeMap_[faultCode.code] = faultCode;
}

std::vector<FaultCode> FaultCodeManager::getAllFaultCodes() {
    std::vector<FaultCode> result;
    for (auto const& [code, fault] : faultCodeMap_) {
        result.push_back(fault);
    }
    return result;
}

// 静态初始化：注册所有预定义的故障码
struct FaultCodeInitializer {
    FaultCodeInitializer() {
        // TELEOP
        FaultCodeManager::registerFaultCode(FaultCodes::TEL_1001);
        FaultCodeManager::registerFaultCode(FaultCodes::TEL_1002);
        FaultCodeManager::registerFaultCode(FaultCodes::TEL_1003);
                FaultCodeManager::registerFaultCode(FaultCodes::TEL_1004);
                FaultCodeManager::registerFaultCode(FaultCodes::TEL_1005);
                FaultCodeManager::registerFaultCode(FaultCodes::TEL_1006);

        // NETWORK
        FaultCodeManager::registerFaultCode(FaultCodes::NET_2001);
        FaultCodeManager::registerFaultCode(FaultCodes::NET_2002);
        FaultCodeManager::registerFaultCode(FaultCodes::NET_2003);
        FaultCodeManager::registerFaultCode(FaultCodes::NET_2004);
        
        // VEHICLE_CTRL
        FaultCodeManager::registerFaultCode(FaultCodes::VEH_3001);
        FaultCodeManager::registerFaultCode(FaultCodes::VEH_3002);
        FaultCodeManager::registerFaultCode(FaultCodes::VEH_3003);
        FaultCodeManager::registerFaultCode(FaultCodes::VEH_3004);
        FaultCodeManager::registerFaultCode(FaultCodes::VEH_3005);
        
        // CAMERA
        FaultCodeManager::registerFaultCode(FaultCodes::CAM_4001);
        FaultCodeManager::registerFaultCode(FaultCodes::CAM_4002);
        FaultCodeManager::registerFaultCode(FaultCodes::CAM_4003);
        
        // POWER
        FaultCodeManager::registerFaultCode(FaultCodes::PWR_5001);
        FaultCodeManager::registerFaultCode(FaultCodes::PWR_5002);
        FaultCodeManager::registerFaultCode(FaultCodes::PWR_5003);
        
        // SWEEPER
        FaultCodeManager::registerFaultCode(FaultCodes::SWP_6001);
        FaultCodeManager::registerFaultCode(FaultCodes::SWP_6002);
        FaultCodeManager::registerFaultCode(FaultCodes::SWP_6003);
        
        // SECURITY
        FaultCodeManager::registerFaultCode(FaultCodes::SEC_7001);
        FaultCodeManager::registerFaultCode(FaultCodes::SEC_7002);
        FaultCodeManager::registerFaultCode(FaultCodes::SEC_7003);
        FaultCodeManager::registerFaultCode(FaultCodes::SEC_7004);
        FaultCodeManager::registerFaultCode(FaultCodes::SEC_7005);
        FaultCodeManager::registerFaultCode(FaultCodes::SEC_7006);
    }
};

static FaultCodeInitializer g_faultCodeInitializer;

} // namespace teleop::protocol
