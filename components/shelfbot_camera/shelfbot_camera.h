#ifndef SHELFBOT_CAMERA_H
#define SHELFBOT_CAMERA_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h" // For mutex
#include <rcl/publisher.h>
#include <sensor_msgs/msg/compressed_image.h>

#define SHELFBOT_CAMERA_TASK_STACK_SIZE 8192
#define SHELFBOT_CAMERA_TASK_PRIORITY 5
#define ROS_IMAGE_BUFFER_SIZE (40 * 1024) // 40KB buffer for the compressed image
#define CAMERA_PUBLISH_DELAY_MS 100

// Make the publisher and message handle available to other files
extern rcl_publisher_t compressed_image_publisher;
extern sensor_msgs__msg__CompressedImage compressed_image_msg;

// Shared buffer for the ROS image message and a mutex to protect it
extern uint8_t ros_image_buffer[];
extern SemaphoreHandle_t ros_image_buffer_mutex;

void shelfbot_camera_task(void *pvParameters);

#endif // SHELFBOT_CAMERA_H