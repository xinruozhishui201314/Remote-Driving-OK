#ifdef ENABLE_ROS2

#include "ros2_bridge.h"
#include <iostream>

Ros2Bridge::Ros2Bridge()
    : Node("vehicle_side_ros2_bridge")
    , m_controller(std::make_shared<VehicleController>())
{
    // 订阅控制话题
    m_controlSub = this->create_subscription<geometry_msgs::msg::Twist>(
        "vehicle/cmd_vel",
        10,
        std::bind(&Ros2Bridge::controlCallback, this, std::placeholders::_1)
    );
    
    std::cout << "[Vehicle-side][ROS2] bridge init, subscribed to vehicle/cmd_vel" << std::endl;
}

Ros2Bridge::~Ros2Bridge()
{
}

void Ros2Bridge::controlCallback(const geometry_msgs::msg::Twist::SharedPtr msg)
{
    // 将 ROS2 Twist 消息转换为车辆控制指令
    double steering = msg->angular.z;  // 角速度作为转向
    
    // Explicit handling for neutral (near-zero) state
    double linear_x = msg->linear.x;
    double throttle = 0.0;
    double brake = 0.0;
    int gear = 0; // Explicit gear variable initialization
    
    if (std::abs(linear_x) < 0.01) {
        // Near zero is Neutral
        throttle = 0.0;
        brake = 0.0;
        gear = 0; // Explicit Neutral gear
    } else if (linear_x > 0.0) {
        throttle = std::max(0.0, linear_x);  // 线速度作为油门
        gear = 1;
    } else {
        brake = std::max(0.0, -linear_x);    // 负线速度作为刹车
        gear = -1;
    }
    
    VehicleController::ControlCommand cmd;
    cmd.steering = std::clamp(steering, -1.0, 1.0);
    cmd.throttle = std::clamp(throttle, 0.0, 1.0);
    cmd.brake = std::clamp(brake, 0.0, 1.0);
    cmd.gear = gear;
    
    m_controller->processCommand(cmd);
}

#endif // ENABLE_ROS2
