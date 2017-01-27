/*
 * object_categorization.h
 *
 *  Created on: 16.10.2012
 *      Author: rbormann
 */

#ifndef OBJECT_CATEGORIZATION_H_
#define OBJECT_CATEGORIZATION_H_

// ROS includes
#include <ros/ros.h>
#include <ros/package.h>
#include <tf/tf.h>
#include <tf/transform_broadcaster.h>

// ROS message includes
#include <sensor_msgs/Image.h>
#include <sensor_msgs/CameraInfo.h>
#include <sensor_msgs/PointCloud2.h>
#include <cob_perception_msgs/PointCloud2Array.h>
#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>
#include <image_transport/image_transport.h>
#include <image_transport/subscriber_filter.h>

// actions
#include <actionlib/server/simple_action_server.h>
#include <cob_object_detection_msgs/DetectObjectsAction.h> // here you have to include the header file with exactly the same name as your message in the /action folder (the Message.h is automatically generated from your Message.action file during compilation)

// boost
#include <boost/bind.hpp>

// PCL
#include <pcl/ModelCoefficients.h>
#include <pcl/point_types.h>
#include <pcl/io/pcd_io.h>
#include <pcl/features/normal_3d.h>
#include <pcl/filters/extract_indices.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/kdtree/kdtree.h>
#include <pcl/sample_consensus/method_types.h>
#include <pcl/sample_consensus/model_types.h>
#include <pcl/segmentation/sac_segmentation.h>
#include <pcl/segmentation/extract_clusters.h>

// opencv
#include <opencv/cv.h>
#include <opencv/highgui.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/image_encodings.h>

#include <object_categorization/ObjectClassifier.h>


// this typedef just establishes the abbreviation SquareActionServer for the long data type
typedef actionlib::SimpleActionServer<cob_object_detection_msgs::DetectObjectsAction> DetectObjectsActionServer;

class ObjectCategorization
{
public:

	ObjectCategorization(ros::NodeHandle nh);

	~ObjectCategorization();

	void Training();

protected:
	/// callback for the incoming pointcloud data stream
	void inputCallback(const cob_perception_msgs::PointCloud2Array::ConstPtr& input_pointcloud_segments_msg, const sensor_msgs::Image::ConstPtr& input_image_msg);

	void drawObjectCoordinateSystem(const tf::Transform& object_pose, cv::Mat& display_image);
	cv::Point projectVector3ToUV(tf::Vector3 point_C);

	void detectObjectsCallback(const cob_object_detection_msgs::DetectObjectsGoalConstPtr& goal);

	/// Converts a color image message to cv::Mat format.
	unsigned long convertColorImageMessageToMat(const sensor_msgs::Image::ConstPtr& image_msg, cv_bridge::CvImageConstPtr& image_ptr, cv::Mat& image);

	void calibrationCallback(const sensor_msgs::CameraInfo::ConstPtr& calibration_msg);

//	ros::Subscriber input_pointcloud_sub_;	///< incoming point cloud topic
	message_filters::Subscriber<cob_perception_msgs::PointCloud2Array> input_pointcloud_sub_;	///< incoming point cloud topic
	ros::Subscriber input_pointcloud_camera_info_sub_;	///< camera calibration of incoming data
	image_transport::ImageTransport* it_;
	image_transport::SubscriberFilter color_image_sub_; ///< color camera image topic
	message_filters::Synchronizer< message_filters::sync_policies::ApproximateTime<cob_perception_msgs::PointCloud2Array, sensor_msgs::Image> >* sync_input_;

	tf::TransformBroadcaster transform_broadcaster_;	///< publish transforms
	DetectObjectsActionServer detect_objects_action_server_; ///< Action server which accepts requests for detecting objects
	cob_object_detection_msgs::Detection latest_object_detection_;		///< pose of the last detected object
	std::string hermes_object_name_;

	ros::NodeHandle node_handle_;			///< ROS node handle

	unsigned int pointcloud_width_;			///< width of the received point cloud
	unsigned int pointcloud_height_;			///< height of the received point cloud
	cv::Mat projection_matrix_;	///< projection matrix of the calibrated camera that transforms points from 3D to image plane in homogeneous coordinates: [u,v,w]=P*[X,Y,Z,1]

	ObjectClassifier object_classifier_;
	ObjectClassifier::GlobalFeatureParams global_feature_params_;

	int mode_of_operation_;		///< 1=normal, 2=hermes recognize 3=hermes train
};

#endif /* OBJECT_CATEGORIZATION_H_ */
