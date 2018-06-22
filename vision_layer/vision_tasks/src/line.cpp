#include "ros/ros.h"
#include "sensor_msgs/Image.h"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/core/core.hpp"
#include "opencv2/imgproc/imgproc_c.h"
#include "opencv2/highgui/highgui.hpp"
#include <cv_bridge/cv_bridge.h>
#include <image_transport/image_transport.h>
#include <dynamic_reconfigure/server.h>
#include <geometry_msgs/PointStamped.h>
#include <sensor_msgs/image_encodings.h>
#include <geometry_msgs/Pose2D.h>
#include <bits/stdc++.h>
#include <stdlib.h>
#include <string>

#include <vision_tasks/lineRangeConfig.h>
#include <vision_commons/contour.h>
#include <vision_commons/morph.h>
#include <vision_commons/threshold.h>

int low_b = 0;
int low_g = 0;
int low_r = 0;
int high_b = 255;
int high_g = 255;
int high_r = 255;
int opening_closing_mat_point = 1;
int opening_iter = 1;
int closing_iter = 1;
image_transport::Publisher thresholded_pub;
image_transport::Publisher marked_pub;
ros::Publisher coordinates_pub;
std::string camera_frame = "auv-iitk";

void callback(vision_tasks::lineRangeConfig &config, double level){
	ROS_INFO("Reconfigure Request: (%d %d %d) - (%d %d %d): ", config.low_b, config.low_g, config.low_r, config.high_b, config.high_g, config.high_r);
	low_b = config.low_b;
	low_g = config.low_g;
	low_r = config.low_r;
	high_b = config.high_b;
	high_g = config.high_g;
	high_r = config.high_r;
	opening_closing_mat_point = config.opening_closing_mat_point;
	opening_iter = config.opening_iter;
	closing_iter = config.closing_iter;
}

void imageCallback(const sensor_msgs::Image::ConstPtr& msg){
	cv_bridge::CvImagePtr cv_img_ptr;
	try{
		cv_img_ptr = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::BGR8);
		cv::Mat image = cv_img_ptr->image;
		cv::Mat image_marked = cv_img_ptr->image;
		if(!image.empty()){
			cv::Mat image_hsv;
			cv::Mat image_thresholded;
			cv::cvtColor(image, image_hsv, cv::COLOR_BGR2HSV);
			if(!(high_b<=low_b || high_g<=low_g || high_r<=low_r)) {
				image_thresholded = vision_commons::Threshold::threshold(image, low_r, low_g, low_b, high_r, high_g, high_b);
				image_thresholded = vision_commons::Morph::open(image_thresholded, 2*opening_closing_mat_point+1, opening_closing_mat_point, opening_closing_mat_point, opening_iter);
				image_thresholded = vision_commons::Morph::close(image_thresholded, 2*opening_closing_mat_point+1, opening_closing_mat_point, opening_closing_mat_point, closing_iter);
				thresholded_pub.publish(cv_bridge::CvImage(msg->header, "mono8", image_thresholded).toImageMsg());
				std::vector<std::vector<cv::Point> > contours = vision_commons::Contour::getBestX(image_thresholded, 1);
				if (contours.size() != 0){
					std::vector<cv::Moments> mu(1);
					mu[0] = moments(contours[0], false);
					std::vector<cv::Point2f> mc(1);
					mc[0] = cv::Point2f( mu[0].m10/mu[0].m00 , mu[0].m01/mu[0].m00 );
					cv::RotatedRect bounding_rectangle = cv::minAreaRect(cv::Mat(contours[0]));
					geometry_msgs::Pose2D line_point_message;
					line_point_message.x = (image.size().height)/2 - bounding_rectangle.center.y;
					line_point_message.y = bounding_rectangle.center.x - (image.size().width)/2;
					line_point_message.theta = bounding_rectangle.angle;
					ROS_INFO("Line (x, y, theta) = (%.2f, %.2f, %.2f)", line_point_message.x, line_point_message.y, line_point_message.theta);
					coordinates_pub.publish(line_point_message);
					cv::circle(image_marked, cv::Point(bounding_rectangle.center.x, bounding_rectangle.center.y), 1, cv::Scalar(0, 255, 255), 8, 0);
					cv::circle(image_marked, cv::Point(image.size().width/2, image.size().height/2), 1, cv::Scalar(0, 255, 255), 8, 0);
					for( int i = 0; i< contours.size(); i++ ) {
						cv::Scalar color = cv::Scalar(255, 255, 100);
						cv::drawContours(image_marked, contours, i, color, 1, 8, std::vector<cv::Vec4i>(), 0, cv::Point());
					}
					cv::Point2f rect_points[4];
					bounding_rectangle.points(rect_points);
					for( int i = 0; i < 4; i++  )
						line(image_marked, rect_points[i], rect_points[(i+1)%4], cv::Scalar(0, 255, 255), 1, 8);
					marked_pub.publish(cv_bridge::CvImage(msg->header, "rgb8", image_marked).toImageMsg());
				}
			}
		}
	}
	catch(cv_bridge::Exception &e){
		ROS_ERROR("cv_bridge exception: %s", e.what());
	}
	catch(cv::Exception &e){
		ROS_ERROR("cv exception: %s", e.what());
	}
}

int main(int argc, char **argv){
	ros::init(argc, argv, "line_task");
	ros::NodeHandle nh;
	dynamic_reconfigure::Server<vision_tasks::lineRangeConfig> server;
	dynamic_reconfigure::Server<vision_tasks::lineRangeConfig>::CallbackType f;
	f = boost::bind(&callback, _1, _2);
	server.setCallback(f);
	image_transport::ImageTransport it(nh);
	thresholded_pub = it.advertise("/line_task/thresholded", 1);
	marked_pub = it.advertise("/line_task/marked", 1);
	coordinates_pub = nh.advertise<geometry_msgs::Pose2D>("/line_task/line_coordinates", 1000);
	image_transport::Subscriber image_raw_sub = it.subscribe("/bottom_camera/image_raw", 1, imageCallback);
	ros::spin();
	return 0;
}
