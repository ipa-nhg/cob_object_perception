#include <cob_object_recording/object_recording.h>
#include <boost/filesystem.hpp>
#include <fstream>
#include <pcl/conversions.h>
#include <pcl_conversions/pcl_conversions.h>

namespace fs = boost::filesystem;

//ObjectRecording::ObjectRecording()
//{
//	it_sub_ = 0;
//	sync_input_ = 0;
//}

ObjectRecording::ObjectRecording(ros::NodeHandle nh)
: node_handle_(nh)
{
	prev_marker_array_size_ = 0;
	camera_matrix_received_ = false;

#ifdef WITH_AUDIO_FEEDBACK
	// prepare sounds
	// for proximity
	sound_feedback_samples_proximity_.resize(441*50);
	for (unsigned int k=0; k<sound_feedback_samples_proximity_.size(); ++k)
		sound_feedback_samples_proximity_[k] = 32767*sin((double)k*0.005 * 2*CV_PI);
	sound_feedback_buffer_proximity_.LoadFromSamples(&sound_feedback_samples_proximity_[0], sound_feedback_samples_proximity_.size(), 2, 44100);
	//sound_feedback_buffer_proximity_.LoadFromFile("censor-beep-1.wav");
	sound_feedback_sound_proximity_.SetBuffer(sound_feedback_buffer_proximity_);
//	sound_feedback_sound_proximity_.Play();
	// for hit
	sound_feedback_samples_hit_.resize(441*20);
	for (unsigned int k=0; k<sound_feedback_samples_hit_.size(); ++k)
		sound_feedback_samples_hit_[k] = 32767*sin((double)k*0.01 * 2*CV_PI);
	sound_feedback_buffer_proximity_.LoadFromSamples(&sound_feedback_samples_hit_[0], sound_feedback_samples_hit_.size(), 2, 44100);
	sound_feedback_sound_hit_.SetBuffer(sound_feedback_buffer_proximity_);
	sound_feedback_sound_hit_.SetVolume(100.f);
#endif

	// subscribers
	input_marker_detection_sub_.subscribe(node_handle_, "input_marker_detections", 1);
	it_sub_ = boost::shared_ptr<image_transport::ImageTransport>(new image_transport::ImageTransport(node_handle_));
	color_image_sub_.subscribe(*it_sub_, "input_color_image", 1);
	//input_pointcloud_sub_ = node_handle_.subscribe("input_pointcloud_segments", 10, &ObjectRecording::inputCallback, this);
	input_pointcloud_sub_.subscribe(node_handle_, "input_pointcloud", 1);
	input_color_camera_info_sub_ = node_handle_.subscribe("input_color_camera_info", 1, &ObjectRecording::calibrationCallback, this);

	// publishers
	it_pub_ = boost::shared_ptr<image_transport::ImageTransport>(new image_transport::ImageTransport(node_handle_));
	display_image_pub_ = it_pub_->advertise("display_image", 1);
	recorded_color_image_pub_ = it_pub_->advertise("recorded_color_image", 1);
	recorded_depth_image_pub_ = it_pub_->advertise("recorded_depth_image", 1);

	// input synchronization
	sync_input_ = new message_filters::Synchronizer< message_filters::sync_policies::ApproximateTime<cob_object_detection_msgs::DetectionArray, sensor_msgs::PointCloud2, sensor_msgs::Image> >(10);
	sync_input_->connectInput(input_marker_detection_sub_, input_pointcloud_sub_, color_image_sub_);

	service_server_start_recording_ = node_handle_.advertiseService("start_recording", &ObjectRecording::startRecording, this);
	service_server_stop_recording_ = node_handle_.advertiseService("stop_recording", &ObjectRecording::stopRecording, this);
	service_server_reset_current_view_ = node_handle_.advertiseService("reset_current_view", &ObjectRecording::resetCurrentView, this);
	service_server_save_recorded_object_ = node_handle_.advertiseService("save_recorded_object", &ObjectRecording::saveRecordedObject, this);

	// dynamic reconfigure
	dynamic_reconfigure_server_.setCallback(boost::bind(&ObjectRecording::dynamicReconfigureCallback, this, _1, _2));

	recording_pose_marker_array_publisher_ = node_handle_.advertise<visualization_msgs::MarkerArray>("recording_pose_marker_array", 0);

	// now set by dynamic configure parameters
//	pan_divisions_ = 6;
//	tilt_divisions_ = 2;
//	preferred_recording_distance_ = 0.6;
//	distance_threshold_translation_ = 0.08;
//	distance_threshold_orientation_ = 8./180.*CV_PI;
//	sharpness_threshold_ = 0.8;
//	xyzr_recording_bounding_box_ = cv::Scalar(0.14, 0.065, 0.4, 0.01);
//	data_storage_path_ = std::string(getenv("HOME")) + "/.ros/";
}

ObjectRecording::~ObjectRecording()
{
	recording_pose_marker_array_publisher_.shutdown();

//	if (it_sub_ != 0) delete it_sub_;
//	if (sync_input_ != 0) delete sync_input_;
}


bool ObjectRecording::startRecording(cob_object_detection_msgs::StartObjectRecording::Request &req, cob_object_detection_msgs::StartObjectRecording::Response &res)
{
	current_object_label_ = req.object_label;
	ROS_INFO("Request to start recording object '%s' received.", current_object_label_.c_str());

	// clear data container
	recording_data_.clear();

	// prepare data container
	int dataset_size = pan_divisions_ * tilt_divisions_ + 1;
	recording_data_.resize(dataset_size);
	double pan_step = 360./(180.*pan_divisions_) * CV_PI;
	double tilt_step = 90./(180.*tilt_divisions_) * CV_PI;
	double pan_max = 359.9/180.*CV_PI;
	double tilt_min = -89.9/180.*CV_PI;
	int index = 0;
	std::cout << "New perspectives added:\n";
	for (double tilt=-tilt_step/2; tilt>tilt_min; tilt-=tilt_step)
		for (double pan=0; pan<pan_max; pan+=pan_step, ++index)
		{
			std::cout << index+1 << ". tilt=" << tilt << " \t pan=" << pan << std::endl;
			computePerspective(pan, tilt, preferred_recording_distance_, recording_data_[index].pose_desired);
		}
	std::cout << index+1 << ". tilt=" << -90./180*CV_PI << " \t pan=0" << std::endl;
	computePerspective(0., -90./180*CV_PI, preferred_recording_distance_, recording_data_[index].pose_desired);

	// register callback function for data processing
	registered_callback_ = sync_input_->registerCallback(boost::bind(&ObjectRecording::inputCallback, this, _1, _2, _3));

	return true;
}

bool ObjectRecording::stopRecording(cob_object_detection_msgs::StopObjectRecording::Request &req, cob_object_detection_msgs::StopObjectRecording::Response &res)
{
	ROS_INFO("Request to stop recording received.");

	res.recording_stopped = true;
	if (req.stop_although_model_is_incomplete == false)
	{
		for (unsigned int i=0; i<recording_data_.size(); ++i)
			res.recording_stopped &= recording_data_[i].perspective_recorded;
		if (res.recording_stopped == false)
		{
			ROS_INFO("Recording not stopped since data collection is not yet complete.");
			return false;
		}
	}

	registered_callback_.disconnect();

	ROS_INFO("Stopped recording object '%s'.", current_object_label_.c_str());

	return true;
}

bool ObjectRecording::resetCurrentView(std_srvs::Empty::Request &req, std_srvs::Empty::Response &res)
{
	recording_data_[current_closest_pose_].perspective_recorded = false;
	recording_data_[current_closest_pose_].distance_to_desired_pose = 1e10;
	recording_data_[current_closest_pose_].sharpness_score = 0.;
	return true;
}

bool ObjectRecording::saveRecordedObject(cob_object_detection_msgs::SaveRecordedObject::Request &req, cob_object_detection_msgs::SaveRecordedObject::Response &res)
{
	// take data storage path from request message, if available
	std::string data_storage_path = data_storage_path_;
	std::string path_from_message = req.storage_location;
	if (path_from_message.compare("") != 0)
		data_storage_path = path_from_message;

	ROS_INFO("Request to save recorded data for object '%s' at path '%s' received.", current_object_label_.c_str(), data_storage_path.c_str());

	// create subfolder for object
	fs::path top_level_directory(data_storage_path);
	if (fs::is_directory(top_level_directory) == false)
	{
		std::cerr << "ERROR - ObjectRecording::saveRecordedObject:" << std::endl;
		std::cerr << "\t ... Path '" << top_level_directory.string() << "' is not a directory." << std::endl;
		return false;
	}

	fs::path package_subfolder = top_level_directory / fs::path("cob_object_recording/");
	if (fs::is_directory(package_subfolder) == false)
	{
		// create subfolder
		if (fs::create_directory(package_subfolder) == false)
		{
			std::cerr << "ERROR - ObjectRecording::saveRecordedObject:" << std::endl;
			std::cerr << "\t ... Could not create path '" << package_subfolder.string() << "'." << std::endl;
			return false;
		}
	}

	fs::path object_subfolder = package_subfolder / current_object_label_;
	if (fs::is_directory(object_subfolder) == false)
	{
		// create subfolder
		if (fs::create_directory(object_subfolder) == false)
		{
			std::cerr << "ERROR - ObjectRecording::saveRecordedObject:" << std::endl;
			std::cerr << "\t ... Could not create path '" << object_subfolder.string() << "'." << std::endl;
			return false;
		}
	}

	// save camera matrix
	fs::path calibration_file_name = object_subfolder / "camera_calibration.txt";
	std::ofstream calibration_file(calibration_file_name.string().c_str(), std::ios::out);
	if (calibration_file.is_open() == false)
	{
		std::cerr << "ERROR - ObjectRecording::saveRecordedObject:" << std::endl;
		std::cerr << "\t ... Could not create file '" << calibration_file_name.string() << "'." << std::endl;
		return false;
	}
	for (int v=0; v<color_camera_matrix_.rows; ++v)
	{
		for (int u=0; u<color_camera_matrix_.cols; ++u)
			calibration_file << color_camera_matrix_.at<double>(v,u) << "\t";
		calibration_file << std::endl;
	}
	calibration_file.close();

	// save all perspectives
	std::cout << std::endl;
	int saved_perspectives = 0;
	for (unsigned int i=0; i<recording_data_.size(); ++i)
	{
		if (recording_data_[i].perspective_recorded == false)
			continue;

		// construct filename
		std::stringstream ss;
		ss << i;
		fs::path file = object_subfolder / ss.str();

		// segment data from whole image
		cv::Mat image_segmented = recording_data_[i].image.clone();
		pcl::PointCloud<pcl::PointXYZRGB> pointcloud_segmented;
		pcl::copyPointCloud(recording_data_[i].pointcloud, pointcloud_segmented);
		cv::Scalar uv_learning_boundaries;
		ImageAndRangeSegmentation(image_segmented, pointcloud_segmented, recording_data_[i].pose_recorded, xyzr_recording_bounding_box_, uv_learning_boundaries);

		// save image
		std::string image_filename = file.string() + ".png";
		cv::imwrite(image_filename, image_segmented);

		// save pointcloud
		tf::Vector3& t = recording_data_[i].pose_recorded.getOrigin();
		pointcloud_segmented.sensor_origin_ = recording_data_[i].pointcloud.sensor_origin_ = Eigen::Vector4f(t.getX(), t.getY(), t.getZ(), 1.0);
		tf::Quaternion q = recording_data_[i].pose_recorded.getRotation();
		pointcloud_segmented.sensor_orientation_ = recording_data_[i].pointcloud.sensor_orientation_ = Eigen::Quaternionf(q.getW(), q.getX(), q.getY(), q.getZ());
		std::string pcd_filename = file.string() + ".pcd";
		pcl::io::savePCDFile(pcd_filename, pointcloud_segmented, false);

		++saved_perspectives;
		std::cout << "." << std::flush;
	}
	std::cout << std::endl;

	ROS_INFO("Saved %i perspectives (out of %i required) for object '%s'.", saved_perspectives, (int)recording_data_.size(), current_object_label_.c_str());

	return true;
}

/// callback for the incoming pointcloud data stream
void ObjectRecording::inputCallback(const cob_object_detection_msgs::DetectionArray::ConstPtr& input_marker_detections_msg, const sensor_msgs::PointCloud2::ConstPtr& input_pointcloud_msg, const sensor_msgs::Image::ConstPtr& input_image_msg)
{
	//std::cout << "Recording data..." << std::endl;

	if (input_marker_detections_msg->detections.size() == 0)
	{
		ROS_INFO("ObjectRecording::inputCallback: No markers detected.\n");
		return;
	}

	// convert color image to cv::Mat
	cv_bridge::CvImageConstPtr color_image_ptr;
	cv::Mat color_image;
	if (convertColorImageMessageToMat(input_image_msg, color_image_ptr, color_image) == false)
		return;

	// convert point cloud 2 message to pointcloud
	typedef pcl::PointXYZRGB PointType;
	pcl::PointCloud<PointType> input_pointcloud;

	pcl::PCLPointCloud2 pcl_pc;
	pcl_conversions::toPCL(*input_pointcloud_msg, pcl_pc);
	pcl::fromPCLPointCloud2(pcl_pc, input_pointcloud);
//	pcl::fromROSMsg(*input_pointcloud_msg, input_pointcloud);

	// compute mean coordinate system if multiple markers detected
	tf::Transform fiducial_pose = computeMarkerPose(input_marker_detections_msg);
	tf::Transform pose_recorded = fiducial_pose.inverse();

	// compute the closest pose to the camera
	double closest_pose_distance = 1e20;
	double closest_translation_distance = 1e20;
	double closest_orientation_distance = 1e20;
	unsigned int closest_pose = 0;
	bool playing_hit_sound = false;
	for (unsigned int i=0; i<recording_data_.size(); ++i)
	{
		double distance_translation = recording_data_[i].pose_desired.getOrigin().distance(pose_recorded.getOrigin());
		double distance_orientation = recording_data_[i].pose_desired.getRotation().angleShortestPath(pose_recorded.getRotation());
		double distance_pose = distance_translation + distance_orientation;

//		std::cout << "  distance=" << distance_translation << "\t angle=" << distance_orientation << " (t=" << distance_threshold_orientation_ << ")" << std::endl;
//		std::cout << "recording_data_[i].pose_desired: XYZ=(" << recording_data_[i].pose_desired.getOrigin().getX() << ", " << recording_data_[i].pose_desired.getOrigin().getY() << ", " << recording_data_[i].pose_desired.getOrigin().getZ() << "), WABC=(" << recording_data_[i].pose_desired.getRotation().getW() << ", " << recording_data_[i].pose_desired.getRotation().getX() << ", " << recording_data_[i].pose_desired.getRotation().getY() << ", " << recording_data_[i].pose_desired.getRotation().getZ() << "\n";
//		std::cout << "                  pose_recorded: XYZ=(" << pose_recorded.getOrigin().getX() << ", " << pose_recorded.getOrigin().getY() << ", " << pose_recorded.getOrigin().getZ() << "), WABC=(" << pose_recorded.getRotation().getW() << ", " << pose_recorded.getRotation().getX() << ", " << pose_recorded.getRotation().getY() << ", " << pose_recorded.getRotation().getZ() << "\n";

		if (distance_pose < closest_pose_distance)
		{
			closest_pose = i;
			closest_pose_distance = distance_pose;
			closest_translation_distance = distance_translation;
			closest_orientation_distance = distance_orientation;
		}
	}

	// check image quality (sharpness)
	double avg_sharpness = 0.;
	for (unsigned int i=0; i<input_marker_detections_msg->detections.size(); ++i)
		avg_sharpness += input_marker_detections_msg->detections[i].score;
	avg_sharpness /= (double)input_marker_detections_msg->detections.size();

	if (avg_sharpness < sharpness_threshold_)
	{
		ROS_WARN("ObjectRecording::inputCallback: Image quality too low. Discarding image with sharpness %.3f (threshold = %.3f)", avg_sharpness, sharpness_threshold_);
	}
	else
	{
		// compute whether the camera is close enough to one of the target perspectives
		if (closest_translation_distance <= distance_threshold_translation_ &&	// check translational distance to camera
			closest_orientation_distance <= distance_threshold_orientation_ && 	// check rotational distance to camera frame
			closest_pose_distance <= recording_data_[closest_pose].distance_to_desired_pose &&	// check that pose distance is at least as close as last time
			avg_sharpness >= recording_data_[closest_pose].sharpness_score)		// check that sharpness score is at least as good as last time
		{
			// save data
			recording_data_[closest_pose].image = color_image;
			recording_data_[closest_pose].pointcloud = input_pointcloud;
			recording_data_[closest_pose].pose_recorded = pose_recorded;
			recording_data_[closest_pose].distance_to_desired_pose = closest_pose_distance;
			recording_data_[closest_pose].sharpness_score = avg_sharpness;
			recording_data_[closest_pose].perspective_recorded = true;

#ifdef WITH_AUDIO_FEEDBACK
			// play hit sound
			sound_feedback_sound_hit_.Play();
			playing_hit_sound = true;
#endif
		}
	}
	current_closest_pose_ = closest_pose;

	// pan/tilt
	tf::Vector3& t = recording_data_[closest_pose].pose_recorded.getOrigin();
	double pan = atan2(t.getY(), t.getX());
	double tilt = asin(t.getZ()/t.length());
	std::stringstream ss;
	ss << "pan=" << pan*180/CV_PI << " tilt=" << tilt*180/CV_PI;

//	// play proximity sound w.r.t. to target pose distance
//	if (playing_hit_sound == false && closest_translation_distance < 2.*distance_threshold_translation_ && closest_orientation_distance < 2.*distance_threshold_orientation_);
//	{
//		sound_feedback_sound_proximity_.SetVolume(std::max(0., 100. - 50.*closest_translation_distance/(2.*distance_threshold_translation_) - 50.*closest_orientation_distance/(2.*distance_threshold_orientation_)));
//		sound_feedback_sound_proximity_.Play();
//	}

	// display direction to closest position
	cv::Mat display_image = color_image.clone();
	tf::Vector3 translation_diff_C = fiducial_pose * recording_data_[closest_pose].pose_desired.getOrigin();
	tf::Vector3 x_rotation_C = fiducial_pose * recording_data_[closest_pose].pose_desired * tf::Vector3(1., 0., 0.);
	double cos_x_angle = x_rotation_C.x()/sqrt(x_rotation_C.x()*x_rotation_C.x()+x_rotation_C.y()*x_rotation_C.y());	// normalize without z-coordinate to suppress camera tilt influences
	double sin_x_angle = sin(acos(cos_x_angle)) * (x_rotation_C.y()<0 ? 1. : -1.);
	int du = 40 * translation_diff_C.getX()*5;	//0.20;
	int dv = 40 * translation_diff_C.getY()*5.;	//0.20;
	double dz = pow(2., -translation_diff_C.getZ()*5.);	//0.20);
	cv::circle(display_image, cv::Point(display_image.cols/2 + du, display_image.rows/2 + dv), 10*dz, cv::Scalar(255,0,0,128), 2);
	cv::line(display_image, cv::Point(display_image.cols/2+du-20*cos_x_angle, display_image.rows/2+dv+20*sin_x_angle), cv::Point(display_image.cols/2+du+20*cos_x_angle, display_image.rows/2+dv-20*sin_x_angle), cv::Scalar(255,0,0,128), 2);
	cv::circle(display_image, cv::Point(display_image.cols/2, display_image.rows/2), 10, cv::Scalar(0,255,0,128), 2);
	cv::line(display_image, cv::Point(display_image.cols/2-20, display_image.rows/2), cv::Point(display_image.cols/2+20, display_image.rows/2), cv::Scalar(0,255,0,128), 2);
	cv::putText(display_image, ss.str().c_str(), cv::Point(20,20), cv::FONT_HERSHEY_PLAIN, 2.0, CV_RGB(0, 255, 0), 2);
	cv_bridge::CvImage cv_ptr;
	cv_ptr.image = display_image;
	cv_ptr.encoding = "bgr8";
	display_image_pub_.publish(cv_ptr.toImageMsg());
//	cv::imshow("object recording", display_image);
//	cv::waitKey(10);

	// display the markers indicating the already recorded perspectives and the missing
	publishRecordingPoseMarkers(input_marker_detections_msg, fiducial_pose);

	// publish recorded color and depth image for this pose (to check the quality)
	cv_ptr.image = recording_data_[closest_pose].image;
	cv::putText(cv_ptr.image, ss.str().c_str(), cv::Point(20,20), cv::FONT_HERSHEY_PLAIN, 2.0, CV_RGB(0, 255, 0), 2);
	cv_ptr.encoding = "bgr8";
	recorded_color_image_pub_.publish(cv_ptr.toImageMsg());
	cv::Mat depth_image = cv::Mat::ones(recording_data_[closest_pose].pointcloud.height, recording_data_[closest_pose].pointcloud.width, CV_8UC1) * 255;
	double factor = 255./(preferred_recording_distance_+ 1.0);
	for (unsigned int v=0; v<recording_data_[closest_pose].pointcloud.height; ++v)
		for (unsigned int u=0; u<recording_data_[closest_pose].pointcloud.width; ++u)
			depth_image.at<uchar>(v,u) = std::min((uchar)255, (uchar)(factor * recording_data_[closest_pose].pointcloud.at(u,v).z));
	cv_ptr.image = depth_image;
	cv::putText(cv_ptr.image, ss.str().c_str(), cv::Point(20,20), cv::FONT_HERSHEY_PLAIN, 2.0, CV_RGB(255, 255, 255), 2);
	cv_ptr.encoding = "mono8";
	recorded_depth_image_pub_.publish(cv_ptr.toImageMsg());
}


/// Converts a color image message to cv::Mat format.
bool ObjectRecording::convertColorImageMessageToMat(const sensor_msgs::Image::ConstPtr& image_msg, cv_bridge::CvImageConstPtr& image_ptr, cv::Mat& image)
{
	try
	{
		image_ptr = cv_bridge::toCvShare(image_msg, sensor_msgs::image_encodings::BGR8);
	}
	catch (cv_bridge::Exception& e)
	{
		ROS_ERROR("ObjectCategorization: cv_bridge exception: %s", e.what());
		return false;
	}
	image = image_ptr->image;

	return true;
}

tf::Transform ObjectRecording::computeMarkerPose(const cob_object_detection_msgs::DetectionArray::ConstPtr& input_marker_detections_msg)
{
	tf::Vector3 mean_translation;
	tf::Quaternion mean_orientation; mean_orientation.setEuler(0.,0.,0.);
	for (unsigned int i=0; i<input_marker_detections_msg->detections.size(); ++i)
	{
		tf::Point translation;
		tf::pointMsgToTF(input_marker_detections_msg->detections[i].pose.pose.position, translation);
		tf::Quaternion orientation;
		tf::quaternionMsgToTF(input_marker_detections_msg->detections[i].pose.pose.orientation, orientation);

		if (i==0)
		{
			mean_translation = translation;
			mean_orientation = orientation;
		}
		else
		{
			mean_translation += translation;
			mean_orientation += orientation;
		}
	}
	mean_translation /= (double)input_marker_detections_msg->detections.size();
	mean_orientation /= (double)input_marker_detections_msg->detections.size();
	mean_orientation.normalize();
	return tf::Transform(mean_orientation, mean_translation);
}


void ObjectRecording::publishRecordingPoseMarkers(const cob_object_detection_msgs::DetectionArray::ConstPtr& input_marker_detections_msg, tf::Transform fiducial_pose)
{
	// 3 arrows for each coordinate system of each detected fiducial
	unsigned int marker_array_size = 3*recording_data_.size();
	if (marker_array_size >= prev_marker_array_size_)
		marker_array_msg_.markers.resize(marker_array_size);

	// Publish marker array
	for (unsigned int i=0; i<recording_data_.size(); ++i)
	{
		// publish a coordinate system from arrow markers for each object
		tf::Transform pose = fiducial_pose * recording_data_[i].pose_desired;

		for (unsigned int j=0; j<3; ++j)
		{
			unsigned int idx = 3*i+j;
			marker_array_msg_.markers[idx].header = input_marker_detections_msg->header;
			marker_array_msg_.markers[idx].ns = "object_recording";
			marker_array_msg_.markers[idx].id =  idx;
			marker_array_msg_.markers[idx].type = visualization_msgs::Marker::ARROW;
			marker_array_msg_.markers[idx].action = visualization_msgs::Marker::ADD;
			marker_array_msg_.markers[idx].color.a = (recording_data_[i].perspective_recorded==false ? 0.85 : 0.15);
			marker_array_msg_.markers[idx].color.r = 0;
			marker_array_msg_.markers[idx].color.g = 0;
			marker_array_msg_.markers[idx].color.b = 0;

			marker_array_msg_.markers[idx].points.resize(2);
			marker_array_msg_.markers[idx].points[0].x = 0.0;
			marker_array_msg_.markers[idx].points[0].y = 0.0;
			marker_array_msg_.markers[idx].points[0].z = 0.0;
			marker_array_msg_.markers[idx].points[1].x = 0.0;
			marker_array_msg_.markers[idx].points[1].y = 0.0;
			marker_array_msg_.markers[idx].points[1].z = 0.0;

			if (j==0)
			{
				marker_array_msg_.markers[idx].points[1].x = 0.2;
				marker_array_msg_.markers[idx].color.r = 255;
			}
			else if (j==1)
			{
				marker_array_msg_.markers[idx].points[1].y = 0.2;
				marker_array_msg_.markers[idx].color.g = 255;
			}
			else if (j==2)
			{
				marker_array_msg_.markers[idx].points[1].z = 0.2;
				marker_array_msg_.markers[idx].color.b = 255;
			}

			marker_array_msg_.markers[idx].pose.position.x = pose.getOrigin().getX();
			marker_array_msg_.markers[idx].pose.position.y = pose.getOrigin().getY();
			marker_array_msg_.markers[idx].pose.position.z = pose.getOrigin().getZ();
			tf::quaternionTFToMsg(pose.getRotation(), marker_array_msg_.markers[idx].pose.orientation);

			marker_array_msg_.markers[idx].lifetime = ros::Duration(10);
			marker_array_msg_.markers[idx].scale.x = 0.01; // shaft diameter
			marker_array_msg_.markers[idx].scale.y = 0.015; // head diameter
			marker_array_msg_.markers[idx].scale.z = 0.0; // head length 0=default
		}
	}

	if (prev_marker_array_size_ > marker_array_size)
	{
		for (unsigned int i = marker_array_size; i < prev_marker_array_size_; ++i)
		{
			marker_array_msg_.markers[i].action = visualization_msgs::Marker::DELETE;
		}
	}
	prev_marker_array_size_ = marker_array_size;

	recording_pose_marker_array_publisher_.publish(marker_array_msg_);
}

void ObjectRecording::computePerspective(const double& pan, const double& tilt, const double& preferred_recording_distance, tf::Transform& perspective_pose)
{
	tf::Transform pose1, pose2, pose3;
	tf::Quaternion q_temp;
	pose1.setOrigin(tf::Vector3(preferred_recording_distance, 0., 0.));			// recording distance
	q_temp.setEuler(-90./180.*CV_PI, 0., 90./180.*CV_PI); pose1.setRotation(q_temp);		// rotation in camera direction
	pose2.setOrigin(tf::Vector3(0.,0.,0.));
	q_temp.setEuler(tilt, 0., 0.); pose2.setRotation(q_temp);		// orientation in tilt direction
	pose3.setOrigin(tf::Vector3(0.,0.,0.));
	q_temp.setEuler(0., 0., pan); pose3.setRotation(q_temp);			// orientation in pan direction
	perspective_pose = pose3 * pose2 * pose1;
}


unsigned long ObjectRecording::ImageAndRangeSegmentation(cv::Mat& color_image, pcl::PointCloud<pcl::PointXYZRGB>& pointcloud, tf::Transform& pose_OfromC, cv::Scalar& xyzr_learning_coordinates, cv::Scalar& uv_learning_boundaries)
{
	if (camera_matrix_received_ == false)
	{
		std::cerr << "ERROR - ObjectRecording::DoRangeSegmentation:" << std::endl;
		std::cerr << "\t ...  camera matrix is not set.\n";
		return 1;
	}

	if (pointcloud.empty() || color_image.empty())
	{
		std::cerr << "ERROR - ObjectRecording::DoRangeSegmentation:" << std::endl;
		std::cerr << "\t ... pointcloud or color image is NULL\n";
		return 1;
	}

	tf::Transform pose_CfromO = pose_OfromC.inverse();

//	// determine min_z coordinate of floor plane
//	double ground_plane_min_z = 1e10;
//	bool found_fitting_plane = false;
//
//	double start_dx[4] = {0.02, -0.115, 0.035, -0.10};
//	double end_dx[4] = {0.10, -0.035, 0.115, -0.02};
//	double start_dy[4] = {0.10, 0.085, -0.165, -0.18};
//	double end_dy[4] = {0.18, 0.165, -0.085, -0.10};
//	for (int i=0; i<4; ++i)
//	{
//		double mean_z = 0.;
//		if (FitGroundPlane(pointcloud, pose_CfromO, start_dx[i], end_dx[i], start_dy[i], end_dy[i], mean_z) == true)
//		{
//			found_fitting_plane = true;
//			if (mean_z < ground_plane_min_z)
//				ground_plane_min_z = mean_z;
//		}
//	}
//	if (found_fitting_plane == false)
//		ground_plane_min_z = 0.;
//
////	std::cout << "ground_plane_min_z=" << ground_plane_min_z << std::endl;
//
//	tf::Transform pointcloud_correction_PCfromO;
//	pointcloud_correction_PCfromO.setIdentity();
//	if (ground_plane_min_z != 0.)
//	{
////		std::cout << "before: pose_CfromO (x,y,z): (" << pose_CfromO.getOrigin().getX() << ", " << pose_CfromO.getOrigin().getY() << ", " << pose_CfromO.getOrigin().getZ() << ")" << std::endl;
//
//		tf::Vector3 z_offset_O(0., 0., ground_plane_min_z);
//		tf::Transform temp;
//		temp.setIdentity();
//		temp.setOrigin(z_offset_O);
//		pointcloud_correction_PCfromO = temp.inverse();
//
////		std::cout << "after: pose_CfromO (x,y,z): (" << (pose_CfromO*pointcloud_correction_PCfromO.inverse()).getOrigin().getX() << ", " << (pose_CfromO*pointcloud_correction_PCfromO.inverse()).getOrigin().getY() << ", " << (pose_CfromO*pointcloud_correction_PCfromO.inverse()).getOrigin().getZ() << ")" << std::endl;
//	}


	// remove ground plane points
	pcl::ModelCoefficients plane_model, avg_plane_model;
	int number_found_planes = 0;
	double start_dx[4] = {0.02, -0.115, 0.035, -0.10};
	double end_dx[4] = {0.10, -0.035, 0.115, -0.02};
	double start_dy[4] = {0.10, 0.085, -0.165, -0.18};
	double end_dy[4] = {0.18, 0.165, -0.085, -0.10};
	for (int i=0; i<4; ++i)
	{
		if (FitGroundPlane(pointcloud, pose_CfromO, start_dx[i], end_dx[i], start_dy[i], end_dy[i], plane_model) == true)
		{
			if (number_found_planes == 0)
				avg_plane_model = plane_model;
			else
			{
				for (int i=0; i<4; ++i)
					avg_plane_model.values[i] += plane_model.values[i];
			}
			++number_found_planes;
		}
	}
	if (number_found_planes > 0)
	{
		for (int i=0; i<4; ++i)
			avg_plane_model.values[i] /= (double)number_found_planes;

		for (unsigned int i=0; i<pointcloud.points.size(); ++i)
		{
			pcl::PointXYZRGB& point = pointcloud.points[i];
			if (abs(point.x*avg_plane_model.values[0]+point.y*avg_plane_model.values[1]+point.z*avg_plane_model.values[2]+avg_plane_model.values[3]) < 0.01)
				point.x = point.y = point.z = 0.;
		}
	}

	int minU = std::numeric_limits<int>::max();
	int maxU = -std::numeric_limits<int>::max();
	int minV = std::numeric_limits<int>::max();
	int maxV = -std::numeric_limits<int>::max();

	int u = 0;
	int v = 0;

	// Compute minimal and maximal image coordinates based on all 8 corners of the
	// segmentation cube
	tf::Vector3 pt_cube_O;
	pt_cube_O.setX(xyzr_learning_coordinates.val[0]);
	for (int i = 0; i < 2; i++)
	{
		pt_cube_O.setY(xyzr_learning_coordinates.val[1]);
		for (int j = 0; j < 2; j++)
		{
			pt_cube_O.setZ(0);
			for (int k = 0; k < 2; k++)
			{
				tf::Vector3 pt_cube_C = pose_CfromO*pt_cube_O;
				ProjectXYZ(pt_cube_C.getX(), pt_cube_C.getY(), pt_cube_C.getZ(), u, v);

				minU = std::min(minU, u);
				maxU = std::max(maxU, u);
				minV = std::min(minV, v);
				maxV = std::max(maxV, v);

				pt_cube_O.setZ(xyzr_learning_coordinates.val[2]);
			}
			pt_cube_O.setY(-pt_cube_O.getY());
		}
		pt_cube_O.setX(-pt_cube_O.getX());
	}

	uv_learning_boundaries = cv::Scalar(minU, maxU, minV, maxV);

	// write all image pixels and 3D points within the 2D and 3D bounding boxes, respectively
	tf::Vector3 pt_in_C;
	for (unsigned j = 0; j < pointcloud.height; j++)
	{
		//float* f_coord_ptr = xyzImage.ptr<float>(j);
		unsigned char* c_shared_ptr = color_image.ptr<unsigned char>(j);

		for (unsigned int i = 0; i < pointcloud.width; i++)
		{
			// crop image to object
			bool maskColorValues = true;
			if (j > uv_learning_boundaries.val[2] && j < uv_learning_boundaries.val[3] && i > uv_learning_boundaries.val[0] && i < uv_learning_boundaries.val[1])
				maskColorValues = false;

			// crop point cloud to bounding box
			bool maskDepthValues = true;
			pcl::PointXYZRGB& point = pointcloud.at(i, j);

			if (point.x == point.x && point.y == point.y && point.z == point.z)		// test for NaN
			{
				tf::Vector3 pt_in_C(point.x, point.y, point.z);
				tf::Vector3 pt_in_O = /*pointcloud_correction_PCfromO * */ pose_OfromC * pt_in_C;

				if (std::abs<double>(pt_in_O.getX()) <= xyzr_learning_coordinates.val[0] && std::abs<double>(pt_in_O.getY()) <= xyzr_learning_coordinates.val[1] &&
					pt_in_O.getZ() >= xyzr_learning_coordinates.val[3] && pt_in_O.getZ() <= xyzr_learning_coordinates.val[2])
					maskDepthValues = false;
			}

			if (maskColorValues)
			{
				/// Set color of shared image to black
				int iTimes3 = i * 3;
				c_shared_ptr[iTimes3] = 0;
				c_shared_ptr[iTimes3 + 1] = 0;
				c_shared_ptr[iTimes3 + 2] = 0;
			}
			if (maskDepthValues)
			{
				/// Set corresponding depth value to 0
				point.x = 0;
				point.y = 0;
				point.z = 0;
			}
		}
	}

	return 0;
}


bool ObjectRecording::FitGroundPlane(const pcl::PointCloud<pcl::PointXYZRGB>& pointcloud, const tf::Transform& pose_CfromO, double start_dx, double end_dx, double start_dy, double end_dy, pcl::ModelCoefficients& coefficients/*, double& mean_z*/)
{
	pcl::PointCloud<pcl::PointXYZ> plane_points;
	for (double dx = start_dx; dx < end_dx; dx += 0.01)
	{
		for (double dy = start_dy; dy < end_dy; dy += 0.01)
		{
			tf::Vector3 point_O(dx, dy, 0.);
			tf::Vector3 point_C = pose_CfromO * point_O;
			int u=0, v=0;
			ProjectXYZ(point_C.getX(), point_C.getY(), point_C.getZ(), u, v);
			pcl::PointXYZRGB point = pointcloud.at(u, v);
			plane_points.push_back(pcl::PointXYZ(point.x, point.y, point.z));
		}
	}
	pcl::PointIndices::Ptr inliers(new pcl::PointIndices);
	// Create the segmentation object
	pcl::SACSegmentation<pcl::PointXYZ> seg;
	// Optional
	seg.setOptimizeCoefficients(true);
	// Mandatory
	seg.setModelType(pcl::SACMODEL_PLANE);
	seg.setMethodType(pcl::SAC_RANSAC);
	seg.setDistanceThreshold(0.015);
	seg.setInputCloud(plane_points.makeShared());
	seg.segment(*inliers, coefficients);
	if (inliers->indices.size() < 4)
		return false;

	// check direction of plane normal vector
	tf::Transform pose_OfromC = pose_CfromO.inverse();
	tf::Vector3 z_vector_object_O(0., 0., 1.);
	tf::Vector3 z_vector_plane_C(coefficients.values[0], coefficients.values[1], coefficients.values[2]);
	tf::Vector3 z_vector_plane_O = pose_OfromC.getBasis() * z_vector_plane_C;
//	std::cout << "z_vector_plane_C     : (" << z_vector_plane_C.getX() << ", " << z_vector_plane_C.getY() << ", " << z_vector_plane_C.getZ() << ")" << std::endl;
//	std::cout << "z_vector_plane_O     : (" << z_vector_plane_O.getX() << ", " << z_vector_plane_O.getY() << ", " << z_vector_plane_O.getZ() << ")" << std::endl;
	z_vector_plane_O.normalize();
	//(z_vector_object * z_vector_plane)/norm(z_vector_plane);
	if (abs(z_vector_plane_O.getZ()) > 0.98)	// todo: might be a parameter
	{
//		// compute mean z value
//		mean_z = 0.;
//		for (unsigned int i=0; i<inliers->indices.size(); ++i)
//		{
//			pcl::PointXYZ& point_C_pcl = plane_points.points[inliers->indices[i]];
//			tf::Vector3 point_C(point_C_pcl.x, point_C_pcl.y, point_C_pcl.z);
//			tf::Vector3 point_O = pose_OfromC * point_C;
//			mean_z += point_O.getZ();
//		}
//		mean_z /= (double)inliers->indices.size();

		return true;
	}
	else
		return false;
}


unsigned long ObjectRecording::ProjectXYZ(double x, double y, double z, int& u, int& v)
{
	cv::Mat XYZ(4, 1, CV_64FC1);
	cv::Mat UVW(3, 1, CV_64FC1);

	x *= 1000;
	y *= 1000;
	z *= 1000;

	double* d_ptr = XYZ.ptr<double>(0);
	d_ptr[0] = x;
	d_ptr[1] = y;
	d_ptr[2] = z;
	d_ptr[3] = 1.;

	UVW = color_camera_matrix_ * XYZ;

	d_ptr = UVW.ptr<double>(0);
	double du = d_ptr[0];
	double dv = d_ptr[1];
	double dw = d_ptr[2];

	u = cvRound(du/dw);
	v = cvRound(dv/dw);

	return 1;
}

void ObjectRecording::calibrationCallback(const sensor_msgs::CameraInfo::ConstPtr& calibration_msg)
{
	if (camera_matrix_received_ == false)
	{
	//	pointcloud_height_ = calibration_msg->height;
	//	pointcloud_width_ = calibration_msg->width;
		cv::Mat temp(3,4,CV_64FC1);
		for (int i=0; i<12; i++)
			temp.at<double>(i/4,i%4) = calibration_msg->P.at(i);
	//		std::cout << "projection_matrix: [";
	//		for (int v=0; v<3; v++)
	//			for (int u=0; u<4; u++)
	//				std::cout << temp.at<double>(v,u) << " ";
	//		std::cout << "]" << std::endl;
		color_camera_matrix_ = temp;
		camera_matrix_received_ = true;
	}
}


void ObjectRecording::dynamicReconfigureCallback(cob_object_recording::ObjectRecordingConfig &config, uint32_t level)
{
	pan_divisions_ = config.pan_divisions;
	tilt_divisions_ = config.tilt_divisions;
	preferred_recording_distance_ = config.preferred_recording_distance;
	distance_threshold_translation_ = config.distance_threshold_translation;
	distance_threshold_orientation_ = config.distance_threshold_orientation/180.*CV_PI;
	sharpness_threshold_ = config.sharpness_threshold;
	xyzr_recording_bounding_box_ = cv::Scalar(config.xyzr_recording_bounding_box_x, config.xyzr_recording_bounding_box_y, config.xyzr_recording_bounding_box_z, config.xyzr_recording_bounding_box_r);
	if (config.data_storage_path.compare("") == 0)
		data_storage_path_ = std::string(getenv("HOME")) + "/.ros/";
	else
		data_storage_path_ = config.data_storage_path;

	std::cout << "Reconfigure request with\n"
			<< "pan_divisions=" << pan_divisions_ << "\n"
			<< "tilt_divisions=" << tilt_divisions_ << "\n"
			<< "preferred_recording_distance=" << preferred_recording_distance_ << "\n"
			<< "distance_threshold_translation=" << distance_threshold_translation_ << "\n"
			<< "distance_threshold_orientation=" << distance_threshold_orientation_ << "\n"
			<< "sharpness_threshold=" << sharpness_threshold_ << "\n"
			<< "xyzr_recording_bounding_box=(" << xyzr_recording_bounding_box_.val[0] << ", " << xyzr_recording_bounding_box_.val[1] << ", " << xyzr_recording_bounding_box_.val[2] << ", " << xyzr_recording_bounding_box_.val[3] << ")\n"
			<< "data_storage_path=" << data_storage_path_ << "\n";
}


int main (int argc, char** argv)
{
	// Initialize ROS, specify name of node
	ros::init(argc, argv, "object_recording");

	// Create a handle for this node, initialize node
	ros::NodeHandle nh;

	// Create and initialize an instance of ObjectRecording
	ObjectRecording objectRecording(nh);

	ros::spin();

	return (0);
}
