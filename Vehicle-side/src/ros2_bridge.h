#ifndef ROS2_BRIDGE_H
#define ROS2_BRIDGE_H

#ifdef ENABLE_ROS2
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include "vehicle_controller.h"

/**
 * @brief ROS2 桥接
 * 将 ROS2 话题转换为车辆控制指令
 */
class Ros2Bridge : public rclcpp::Node {
public:
    explicit Ros2Bridge();
    ~Ros2Bridge();

private:
    void controlCallback(const geometry_msgs::msg::Twist::SharedPtr msg);
    
    rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr m_controlSub;
    std::shared_ptr<VehicleController> m_controller;
};

#endif // ENABLE_ROS2

#endif // ROS2_BRIDGE_H
