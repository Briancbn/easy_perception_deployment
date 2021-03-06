// Copyright 2020 Advanced Remanufacturing and Technology Centre
// Copyright 2020 ROS-Industrial Consortium Asia Pacific Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#ifndef EPD_UTILS_LIB__PROCESSOR_HPP_
#define EPD_UTILS_LIB__PROCESSOR_HPP_

#include <chrono>
#include <string>
#include <memory>
#include <functional>

// OpenCV LIB
#include "opencv2/opencv.hpp"

// ROS2 LIB
#include "cv_bridge/cv_bridge.h"
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "sensor_msgs/msg/region_of_interest.hpp"

// EPD_UTILS LIB
#include "epd_utils_lib/epd_container.hpp"
#include "epd_msgs/msg/epd_image_classification.hpp"
#include "epd_msgs/msg/epd_object_detection.hpp"
#include "epd_utils_lib/message_utils.hpp"

/*! \class Processor
    \brief An Processor class object.
    This class object inherits rclcpp::Node object and acts the main bridge
    between the ROS2 interface and the underlying ort_cpp_lib library that is
    based on ONNXRuntime Library.
*/
class Processor : public rclcpp::Node
{
public:
  /*! \brief A Constructor function*/
  Processor(void);

private:
  /*! \brief A subscriber member variable to receive remote calls to shutdown.*/
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr status_sub;
  /*! \brief A subscriber member variable to receive images to receive.*/
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr image_sub;
  /*! \brief A publisher member variable to output visualization of inference
  results*/
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr visual_pub;
  /*! \brief A publisher member variable to output Precision-Level 1 (P1)
  specific inference output suitable for external agents.*/
  rclcpp::Publisher<epd_msgs::msg::EPDImageClassification>::SharedPtr p1_pub;
  /*! \brief A publisher member variable to output Precision-Level 2 (P2)
  specific inference output suitable for external agents.*/
  rclcpp::Publisher<epd_msgs::msg::EPDObjectDetection>::SharedPtr p2_pub;
  /*! \brief A publisher member variable to output Precision-Level 3 (P3)
  specific inference output suitable for external agents.*/
  rclcpp::Publisher<epd_msgs::msg::EPDObjectDetection>::SharedPtr p3_pub;
  /*! \brief A EPDContainer member object that serves as the aforementioned
  bridge.*/
  mutable EPD::EPDContainer ortAgent_;

  /*! \brief A ROS2 callback function utilized by image_sub.\n
  It gets ortAgent_ to initialize once and only once when the first input image
  is received.\n
  It also populates the appropriate ROS messages with EPDImageClassification/
  EPDObjectDetection when the onlyVisualize boolean flag is set to false.\n
  */
  void topic_callback(const sensor_msgs::msg::Image::SharedPtr msg) const;
  /*! \brief A ROS2 callback function utilized by status_sub.*/
  void state_callback(const std_msgs::msg::String::SharedPtr msg) const;
};

Processor::Processor(void)
: Node("processer")
{
  // Creating subscriber
  image_sub = this->create_subscription<sensor_msgs::msg::Image>(
    "/processor/image_input",
    10,
    std::bind(&Processor::topic_callback, this, std::placeholders::_1));

  status_sub = this->create_subscription<std_msgs::msg::String>(
    "/processor/state_input",
    10,
    std::bind(&Processor::state_callback, this, std::placeholders::_1));

  // Creating publisher
  visual_pub = this->create_publisher<sensor_msgs::msg::Image>(
    "/processor/output",
    10);
  p1_pub = this->create_publisher<epd_msgs::msg::EPDImageClassification>(
    "/processor/epd_p1_output",
    10);
  p2_pub = this->create_publisher<epd_msgs::msg::EPDObjectDetection>(
    "/processor/epd_p2_output",
    10);
  p3_pub = this->create_publisher<epd_msgs::msg::EPDObjectDetection>(
    "/processor/epd_p3_output",
    10);
}

void Processor::state_callback(const std_msgs::msg::String::SharedPtr msg) const
{
  std::string requested_state = msg->data.c_str();

  if (requested_state.compare("shutdown") == 0) {
    rclcpp::shutdown();
  } else {
    RCLCPP_WARN(this->get_logger(), "Invalid state requested.");
  }
}

void Processor::topic_callback(const sensor_msgs::msg::Image::SharedPtr msg) const
{
  // RCLCPP_INFO(this->get_logger(), "Image received");

  /* Check if input image is empty or not.
  If empty, discard image and don't process.
  Otherwise, proceed with processing.
  */
  if (msg->height == 0) {
    RCLCPP_WARN(this->get_logger(), "Input image empty. Discarding.");
    return;
  }

  // Convert ROS Image message to cv::Mat for processing.
  std::shared_ptr<cv_bridge::CvImage> imgptr = cv_bridge::toCvCopy(msg, "bgr8");
  cv::Mat img = imgptr->image;

  if (!ortAgent_.isInit()) {
    ortAgent_.setFrameDimension(img.cols, img.rows);
    ortAgent_.initORTSessionHandler();
    ortAgent_.setInitBoolean(true);
  } else {
    // TODO(cardboardcode) Implement auto reinitialization of Ort Session.
    /*
    Check if height and width has changed or not.
    If either dim changed, throw runtime error.
    Otherwise, proceed.
    */
    if (ortAgent_.getWidth() != img.cols && ortAgent_.getHeight() != img.rows) {
      throw std::runtime_error("Input camera changed. Please restart.");
    }
  }
  // DEBUG
  // Initialize timer
  std::chrono::high_resolution_clock::time_point begin = std::chrono::high_resolution_clock::now();

  cv::Mat resultImg;
  switch (ortAgent_.precision_level) {
    case 1:
      {
        epd_msgs::msg::EPDImageClassification output_msg;
        output_msg.object_names = ortAgent_.p1_ort_session->infer(img);

        // TODO(cardboardcode) Populate header information with timestamp
        // output_msg.header = std_msgs::msg::Header();

        p1_pub->publish(output_msg);
        break;
      }
    case 2:
      {
        if (ortAgent_.isVisualize()) {
          resultImg = ortAgent_.p2_ort_session->infer_visualize(img);
          sensor_msgs::msg::Image::SharedPtr output_msg =
            cv_bridge::CvImage(std_msgs::msg::Header(), "bgr8", resultImg).toImageMsg();
          visual_pub->publish(*output_msg);
        } else {
          EPD::EPDObjectDetection result = ortAgent_.p2_ort_session->infer_action(img);
          epd_msgs::msg::EPDObjectDetection output_msg;
          for (size_t i = 0; i < result.data_size; i++) {
            output_msg.class_indices.push_back(result.classIndices[i]);

            output_msg.scores.push_back(result.scores[i]);

            sensor_msgs::msg::RegionOfInterest roi;
            roi.x_offset = result.bboxes[i][0];
            roi.y_offset = result.bboxes[i][1];
            roi.width = result.bboxes[i][2] - result.bboxes[i][0];
            roi.height = result.bboxes[i][3] - result.bboxes[i][1];
            roi.do_rectify = false;
            output_msg.bboxes.push_back(roi);
          }
          p2_pub->publish(output_msg);
        }

        break;
      }
    case 3:
      {
        if (ortAgent_.isVisualize()) {
          resultImg = ortAgent_.p3_ort_session->infer_visualize(img);
          sensor_msgs::msg::Image::SharedPtr output_msg =
            cv_bridge::CvImage(std_msgs::msg::Header(), "bgr8", resultImg).toImageMsg();
          visual_pub->publish(*output_msg);
        } else {
          EPD::EPDObjectDetection result = ortAgent_.p3_ort_session->infer_action(img);
          epd_msgs::msg::EPDObjectDetection output_msg;
          for (size_t i = 0; i < result.data_size; i++) {
            output_msg.class_indices.push_back(result.classIndices[i]);

            output_msg.scores.push_back(result.scores[i]);

            sensor_msgs::msg::RegionOfInterest roi;
            roi.x_offset = result.bboxes[i][0];
            roi.y_offset = result.bboxes[i][1];
            roi.width = result.bboxes[i][2] - result.bboxes[i][0];
            roi.height = result.bboxes[i][3] - result.bboxes[i][1];
            roi.do_rectify = false;
            output_msg.bboxes.push_back(roi);

            sensor_msgs::msg::Image::SharedPtr mask =
              cv_bridge::CvImage(std_msgs::msg::Header(), "32FC1", result.masks[i]).toImageMsg();
            output_msg.masks.push_back(*mask);
          }
          p3_pub->publish(output_msg);
        }

        break;
      }
  }

  // DEBUG
  std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
  auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);
  RCLCPP_INFO(this->get_logger(), "[-FPS-]= %f\n", 1000.0 / elapsedTime.count());
}

#endif  // EPD_UTILS_LIB__PROCESSOR_HPP_
