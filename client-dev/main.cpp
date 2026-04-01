#include <iostream>
#include <string>
#include <cstdlib>

int main() {
    std::cout << "Client-Dev Service Starting..." << std::endl;
    
    // 读取环境变量
    const char* mqtt_broker = std::getenv("MQTT_BROKER_URL");
    const char* backend_url = std::getenv("BACKEND_URL");

    if (mqtt_broker) {
        std::cout << "Connecting to MQTT Broker: " << mqtt_broker << std::endl;
    } else {
        std::cout << "MQTT_BROKER_URL not set." << std::endl;
    }

    if (backend_url) {
        std::cout << "Backend URL: " << backend_url << std::endl;
    } else {
        std::cout << "BACKEND_URL not set." << std::endl;
    }

    // 模拟长时间运行
    while(true) {
        // 客户端逻辑将放在这里
        sleep(5);
    }

    return 0;
}
