/*
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2016 Case Western Reserve University
 *
 *	Ran Hao <rxh349@case.edu>
 *
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *	 notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *	 copyright notice, this list of conditions and the following
 *	 disclaimer in the documentation and/or other materials provided
 *	 with the distribution.
 *   * Neither the name of Case Western Reserve Univeristy, nor the names of its
 *	 contributors may be used to endorse or promote products derived
 *	 from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <tool_tracking/kalman_filter.h>  //everything inside here
#include <opencv2/calib3d/calib3d.hpp>
#include <cwru_davinci_interface/davinci_interface.h>

using namespace std;

KalmanFilter::KalmanFilter(ros::NodeHandle *nodehandle) :
        nh_(*nodehandle), Downsample_rate(0.02), toolSize(2), L(12) {

    ROS_INFO("Initializing UKF...");

    //initializeParticles(); Where we're going, we don't need particles.

    // initialization, just basic black image ??? how to get the size of the image
    toolImage_left_arm_1 = cv::Mat::zeros(475, 640, CV_8UC3);
    toolImage_right_arm_1 = cv::Mat::zeros(475, 640, CV_8UC3);

    toolImage_left_arm_2 = cv::Mat::zeros(475, 640, CV_8UC3);
    toolImage_right_arm_2 = cv::Mat::zeros(475, 640, CV_8UC3);

    //Set up forward kinematics.
    /***motion model params***/

    cmd_green.resize(L / 2);
    cmd_yellow.resize(L / 2);

    com_s1 = nh_.subscribe("/dvrk/PSM1/set_position_joint", 10, &KalmanFilter::newCommandCallback1, this);
    com_s2 = nh_.subscribe("/dvrk/PSM2/set_position_joint", 10, &KalmanFilter::newCommandCallback2, this);

    kinematics = Davinci_fwd_solver();

    //Pull in our first round of sensor data.
    davinci_interface::init_joint_feedback(nh_);
    std::vector<std::vector<double> > tmp;

    if(davinci_interface::get_fresh_robot_pos(tmp)){
        sensor_green = tmp[0];
        sensor_yellow = tmp[1];
    }

    Eigen::Affine3d green_pos = kinematics.fwd_kin_solve(Vectorq7x1(sensor_green.data()));
    Eigen::Vector3d green_trans = green_pos.translation();
    Eigen::Vector3d green_rpy = green_pos.rotation().eulerAngles(0, 1, 2);
    Eigen::Affine3d yellow_pos = kinematics.fwd_kin_solve(Vectorq7x1(sensor_yellow.data()));
    Eigen::Vector3d yellow_trans = yellow_pos.translation();
    Eigen::Vector3d yellow_rpy = yellow_pos.rotation().eulerAngles(0, 1, 2);

    kalman_mu = cv::Mat_<double>::zeros(12, 1);
	kalman_mu.at<double>(0, 1) = green_trans[0];
	kalman_mu.at<double>(1, 1) = green_trans[1];
	kalman_mu.at<double>(2, 1) = green_trans[2];
	kalman_mu.at<double>(3, 1) = green_rpy[0];
	kalman_mu.at<double>(4, 1) = green_rpy[1];
	kalman_mu.at<double>(5, 1) = green_rpy[2];
	kalman_mu.at<double>(6, 1) = yellow_trans[0];
	kalman_mu.at<double>(7, 1) = yellow_trans[1];
	kalman_mu.at<double>(8, 1) = yellow_trans[2];
	kalman_mu.at<double>(9, 1) = yellow_rpy[0];
	kalman_mu.at<double>(10, 1) = yellow_rpy[1];
	kalman_mu.at<double>(11, 1) = yellow_rpy[2];
    kalman_sigma = (cv::Mat_<double>::zeros(12, 12));

    //ROS_INFO("GREEN ARM AT (%f %f %f): %f %f %f", green_trans[0], green_trans[1], green_trans[2], green_rpy[0], green_rpy[1], green_rpy[2]);
    //ROS_INFO("YELLOW ARM AT (%f %f %f): %f %f %f", yellow_trans[0], yellow_trans[1], yellow_trans[2], yellow_rpy[0], yellow_rpy[1], yellow_rpy[2]);

    freshCameraInfo = false; //should be left and right
    projectionMat_subscriber_r = nh_.subscribe("/davinci_endo/right/camera_info", 1, &KalmanFilter::projectionRightCB, this);
    projectionMat_subscriber_l = nh_.subscribe("/davinci_endo/left/camera_info", 1, &KalmanFilter::projectionLeftCB, this);

    P_left = cv::Mat::zeros(3,4,CV_64FC1);
    P_right = cv::Mat::zeros(3,4,CV_64FC1);

    //Subscribe to the necessary transforms.
    tf::StampedTransform arm_l__cam_l_st;
    tf::StampedTransform arm_r__cam_l_st;
    tf::StampedTransform arm_l__cam_r_st;
    tf::StampedTransform arm_r__cam_r_st;
    try{
        tf::TransformListener l;
        while(!l.waitForTransform("/left_camera_optical_frame", "one_psm_base_link", ros::Time(0.0), ros::Duration(10.0)) && ros::ok()){}
        l.lookupTransform("/left_camera_optical_frame", "one_psm_base_link", ros::Time(0.0), arm_l__cam_l_st);
        while(!l.waitForTransform("/left_camera_optical_frame", "two_psm_base_link", ros::Time(0.0), ros::Duration(10.0)) && ros::ok()){}
        l.lookupTransform("/left_camera_optical_frame", "two_psm_base_link", ros::Time(0.0), arm_r__cam_l_st);
        while(!l.waitForTransform("/right_camera_optical_frame", "one_psm_base_link", ros::Time(0.0), ros::Duration(10.0)) && ros::ok()){}
        l.lookupTransform("/right_camera_optical_frame", "one_psm_base_link", ros::Time(0.0), arm_l__cam_r_st);
        while(!l.waitForTransform("/right_camera_optical_frame", "two_psm_base_link", ros::Time(0.0), ros::Duration(10.0)) && ros::ok()){}
        l.lookupTransform("/right_camera_optical_frame", "two_psm_base_link", ros::Time(0.0), arm_r__cam_r_st);
    }
    catch (tf::TransformException ex){
        ROS_ERROR("%s",ex.what());
        exit(1);
    }

    //Convert to Affine3ds for storage, which is the format they will be used in for rendering.
    XformUtils xfu;

    /////arm_l is arm_1???
    arm_l__cam_l = xfu.transformTFToAffine3d(arm_l__cam_l_st);
    arm_l__cam_r = xfu.transformTFToAffine3d(arm_l__cam_r_st);
    arm_r__cam_l = xfu.transformTFToAffine3d(arm_r__cam_l_st);
    arm_r__cam_r = xfu.transformTFToAffine3d(arm_r__cam_r_st);

    convertEigenToMat(arm_l__cam_l, Cam_left_arm_1);
    convertEigenToMat(arm_l__cam_r, Cam_right_arm_1);
    convertEigenToMat(arm_r__cam_l, Cam_left_arm_2);
    convertEigenToMat(arm_r__cam_r, Cam_right_arm_2);

    //DEBUG:
    ROS_INFO_STREAM("Cam_left_arm_1: " << Cam_left_arm_1);
    ROS_INFO_STREAM("Cam_right_arm_1: " << Cam_right_arm_1);
    ROS_INFO_STREAM("Cam_left_arm_2: " << Cam_left_arm_2);
    ROS_INFO_STREAM("Cam_right_arm_2: " << Cam_right_arm_2);

};

void KalmanFilter::projectionRightCB(const sensor_msgs::CameraInfo::ConstPtr &projectionRight){

    P_right.at<double>(0,0) = projectionRight->P[0];
    P_right.at<double>(0,1) = projectionRight->P[1];
    P_right.at<double>(0,2) = projectionRight->P[2];
    P_right.at<double>(0,3) = projectionRight->P[3];

    P_right.at<double>(1,0) = projectionRight->P[4];
    P_right.at<double>(1,1) = projectionRight->P[5];
    P_right.at<double>(1,2) = projectionRight->P[6];
    P_right.at<double>(1,3) = projectionRight->P[7];

    P_right.at<double>(2,0) = projectionRight->P[8];
    P_right.at<double>(2,1) = projectionRight->P[9];
    P_right.at<double>(2,2) = projectionRight->P[10];
    P_right.at<double>(2,3) = projectionRight->P[11];

    //ROS_INFO_STREAM("right: " << P_right);
    freshCameraInfo = true;
};

void KalmanFilter::projectionLeftCB(const sensor_msgs::CameraInfo::ConstPtr &projectionLeft){

    P_left.at<double>(0,0) = projectionLeft->P[0];
    P_left.at<double>(0,1) = projectionLeft->P[1];
    P_left.at<double>(0,2) = projectionLeft->P[2];
    P_left.at<double>(0,3) = projectionLeft->P[3];

    P_left.at<double>(1,0) = projectionLeft->P[4];
    P_left.at<double>(1,1) = projectionLeft->P[5];
    P_left.at<double>(1,2) = projectionLeft->P[6];
    P_left.at<double>(1,3) = projectionLeft->P[7];

    P_left.at<double>(2,0) = projectionLeft->P[8];
    P_left.at<double>(2,1) = projectionLeft->P[9];
    P_left.at<double>(2,2) = projectionLeft->P[10];
    P_left.at<double>(2,3) = projectionLeft->P[11];

    //ROS_INFO_STREAM("left: " << P_left);
    freshCameraInfo = true;
};

KalmanFilter::~KalmanFilter() {

};

void KalmanFilter::newCommandCallback1(const sensor_msgs::JointState::ConstPtr& incoming){
    std::vector<double> positions = incoming->position;
    for(int i = 0; i < L; i++){
        cmd_green[i] = positions[i];
    }
};

void KalmanFilter::newCommandCallback2(const sensor_msgs::JointState::ConstPtr& incoming){
    std::vector<double> positions = incoming->position;
    for(int j = 0; j < L; j++){
        cmd_yellow[j] = positions[j];
    }
};

//Deprecated by KalmanFilter::update(). Archival code only.
double KalmanFilter::measureFunc(
	cv::Mat & toolImage_left,
	cv::Mat & toolImage_right,
	ToolModel::toolModel &toolPose,
	const cv::Mat &segmented_left,
	const cv::Mat &segmented_right,
	cv::Mat &Cam_left,
	cv::Mat &Cam_right
) {

	ROS_INFO("IN MF");

    //Looks mostly IP-related; need to resturcture to not be dependant on particles and instead use sigma-points.
    toolImage_left.setTo(0);
    toolImage_right.setTo(0);
    
    ROS_INFO("Set images.");

    /***do the sampling and get the matching score***/
    //first get the rendered image using 3d model of the tool
    newToolModel.renderTool(toolImage_left, toolPose, Cam_left, P_left);
    
    ROS_INFO("Rendered tool 1.");
    
    double left = newToolModel.calculateMatchingScore(toolImage_left, segmented_left);  //get the matching score

    newToolModel.renderTool(toolImage_right, toolPose, Cam_right, P_right);
    double right = newToolModel.calculateMatchingScore(toolImage_right, segmented_right);

    double matchingScore = sqrt(pow(left, 2) + pow(right, 2));

    return matchingScore;

};

void KalmanFilter::update(const cv::Mat &segmented_left, const cv::Mat &segmented_right){	
	/******Find and convert our various params and inputs******/

	//Get sensor update.
	std::vector<std::vector<double> > tmp;
	if(davinci_interface::get_fresh_robot_pos(tmp)){
		sensor_green = tmp[0];
		sensor_yellow = tmp[1];
	}
	
	//Convert into proper format
	Eigen::Affine3d green_pos = kinematics.fwd_kin_solve(Vectorq7x1(sensor_green.data()));
	Eigen::Vector3d green_trans = green_pos.translation();
	Eigen::Vector3d green_rpy = green_pos.rotation().eulerAngles(0, 1, 2);
	Eigen::Affine3d yellow_pos = kinematics.fwd_kin_solve(Vectorq7x1(sensor_yellow.data()));
	Eigen::Vector3d yellow_trans = yellow_pos.translation();
	Eigen::Vector3d yellow_rpy = yellow_pos.rotation().eulerAngles(0, 1, 2);
	cv::Mat zt = cv::Mat_<double>(12, 1);
	zt.at<double>(0, 1) = green_trans[0];
	zt.at<double>(1, 1) = green_trans[1];
	zt.at<double>(2, 1) = green_trans[2];
	zt.at<double>(3, 1) = green_rpy[0];
	zt.at<double>(4, 1) = green_rpy[1];
	zt.at<double>(5, 1) = green_rpy[2];
	zt.at<double>(6, 1) = yellow_trans[0];
	zt.at<double>(7, 1) = yellow_trans[1];
	zt.at<double>(8, 1) = yellow_trans[2];
	zt.at<double>(9, 1) = yellow_rpy[0];
	zt.at<double>(10, 1) = yellow_rpy[1];
	zt.at<double>(11, 1) = yellow_rpy[2];
	
	ROS_INFO("GREEN ARM AT (%f %f %f): %f %f %f", green_trans[0], green_trans[1], green_trans[2], green_rpy[0], green_rpy[1], green_rpy[2]);
	ROS_INFO("YELLOW ARM AT (%f %f %f): %f %f %f", yellow_trans[0], yellow_trans[1], yellow_trans[2], yellow_rpy[0], yellow_rpy[1], yellow_rpy[2]);
	
	//Get command update.
	//TODO: At the moment, the command is just the sensor update. We will need to get and preprocess the desired positions.
	cv::Mat ut = zt.clone();
	//ROS_ERROR("%f %f %f %f %f %f", zt.at<double>(0, 1),zt.at<double>(1, 1),zt.at<double>(2, 1),zt.at<double>(3, 1),zt.at<double>(4, 1),zt.at<double>(5, 1));
	
	cv::Mat sigma_t_last = kalman_sigma.clone();
	cv::Mat mu_t_last = kalman_mu.clone();
	
	//****Generate the sigma points.****
	double lambda = alpha * alpha * (L + k) - L;
	double gamma = sqrt(L + lambda);

	///get the square root for sigma point generation using SVD decomposition
	cv::Mat root_sigma_t_last = cv::Mat_<double>::zeros(L, 1);

	cv::Mat s = cv::Mat_<double>::zeros(L, 1);  //allocate space for SVD
	cv::Mat vt = cv::Mat_<double>::zeros(L, L);  //allocate space for SVD
	cv::Mat u = cv::Mat_<double>::zeros(L, L);  //allocate space for SVD

	cv::SVD::compute(kalman_sigma, s, u, vt);//The actual square root gets saved into s

	root_sigma_t_last = s.clone(); //store that back into the designated square root term

	//Populate the sigma points:
	std::vector<cv::Mat_<double> > sigma_pts_last;
	sigma_pts_last.resize(2*L + 1);
	
	sigma_pts_last[0] = mu_t_last.clone();//X_0
	for (int i = 1; i <= L; i++) {
		sigma_pts_last[i] = sigma_pts_last[0] + (gamma * root_sigma_t_last);
		sigma_pts_last[i + L] = sigma_pts_last[0] - (gamma * root_sigma_t_last);
	}
	
	//TODO: Incorporate a second set of weights based on the matching score, to influence the effect of the sigma points.
	
	//Compute their weights:
	std::vector<double> w_m;
	w_m.resize(2*L + 1);
	std::vector<double> w_c;
	w_c.resize(2*L + 1);
	w_m[0] = lambda / (L + lambda);
	w_c[0] = lambda / (L + lambda) + (1.0 - (alpha * alpha) + beta);
	for(int i = 1; i < 2 * L + 1; i++){
		w_m[i] = 1.0 / (2.0 * (L + lambda));
		w_c[i] = 1.0 / (2.0 * (L + lambda));
	}
	
	/*****Update sigma points based on motion model******/
	std::vector<cv::Mat_<double> > sigma_pts_bar;
	sigma_pts_bar.resize(2*L + 1);
	for(int i = 0; i < 2 * L + 1; i++){
		g(sigma_pts_bar[i], sigma_pts_last[i], ut);
		//ROS_ERROR("%f %f %f %f %f %f", sigma_pts_bar[i].at<double>(1, 1),sigma_pts_bar[i].at<double>(2, 1),sigma_pts_bar[i].at<double>(3, 1),sigma_pts_bar[i].at<double>(4, 1),sigma_pts_bar[i].at<double>(5, 1),sigma_pts_bar[i].at<double>(6, 1));
	}
	
	/*****Render each sigma point and compute its matching score.*****/
	std::vector<double> mscores;
	mscores.resize(2*L + 1);
	computeSigmaMeasures(mscores, sigma_pts_bar, segmented_left, segmented_right);
	
	//TODO: Calculate a second set of weights based on the matching score, to influence the effect of the sigma points.
	
	/*****Create the predicted mus and sigmas.*****/
	cv::Mat mu_bar = cv::Mat_<double>::zeros(12, 1);
	for(int i = 0; i < 2 * L + 1; i++){
		mu_bar = mu_bar + w_m[i] * sigma_pts_bar[i];
	}
	cv::Mat sigma_bar = cv::Mat_<double>::zeros(12, 12);
	for(int i = 0; i < 2 * L + 1; i++){
		sigma_bar = sigma_bar + w_c[i] * (sigma_pts_bar[i] - mu_bar) * ((sigma_pts_bar[i] - mu_bar).t());
	}
	
	/*****Correction Step: Move the sigma points through the measurement function.*****/
	std::vector<cv::Mat_<double> > Z_bar;
	Z_bar.resize(2 * L + 1);
	for(int i = 0; i < 2 * L + 1; i++){
		h(Z_bar[i], sigma_pts_bar[i]);
	}
	
	/*****Calculate derived variance statistics.*****/
	cv::Mat z_caret = cv::Mat_<double>::zeros(12, 1);
	for(int i = 0; i < 2 * L + 1; i++){
		z_caret = z_caret + w_m[i] * Z_bar[i];
	}
	
	cv::Mat S = cv::Mat_<double>::zeros(12, 12);
	for(int i = 0; i < 2 * L + 1; i++){
		S = S + w_c[i] * (Z_bar[i] - z_caret) * ((Z_bar[i] - z_caret).t());
	}
	
	cv::Mat sigma_xz = cv::Mat_<double>::zeros(12, 12);
	for(int i = 0; i < 2 * L + 1; i++){
		sigma_xz = w_c[i] * (sigma_pts_bar[i] - mu_bar) * ((Z_bar[i] - z_caret).t());
	}
	cv::Mat K = sigma_xz * S.inv();
	
	/*****Update our mu and sigma.*****/
	kalman_mu = mu_bar + K * (zt - z_caret);
	kalman_sigma = sigma_bar - K * S * K.t();
	ROS_WARN("GREEN ARM AT (%f %f %f): %f %f %f", kalman_mu.at<double>(0, 1), kalman_mu.at<double>(1, 1),kalman_mu.at<double>(2, 1),kalman_mu.at<double>(3, 1),kalman_mu.at<double>(4, 1), kalman_mu.at<double>(5, 1));
	ROS_WARN("YELLOW ARM AT (%f %f %f): %f %f %f", kalman_mu.at<double>(6, 1), kalman_mu.at<double>(7, 1),kalman_mu.at<double>(8, 1),kalman_mu.at<double>(9, 1),kalman_mu.at<double>(10, 1), kalman_mu.at<double>(11, 1));
};

void KalmanFilter::g(cv::Mat & sigma_point_out, const cv::Mat & sigma_point_in, const cv::Mat & u){
    //TODO: Very stupid placeholder model that squashes all of the sigma points into the last recorded position with zero variance.
    sigma_point_out = u.clone();
};

/***this function should compute the matching score for each sigma points: for future use****/
void KalmanFilter::computeSigmaMeasures(std::vector<double> & measureWeights, const std::vector<cv::Mat_<double> > & sigma_point_in, const cv::Mat &segmented_left, const cv::Mat &segmented_right){
	ROS_ERROR("IN CSM FUNC: %lu, %lu", measureWeights.size(), sigma_point_in.size());
    //TODO:
    double total = 0.0;
    for (int i = 0; i < sigma_point_in.size() ; i++) {
        measureWeights[i] = matching_score(sigma_point_in[i], segmented_left, segmented_right );
        total += measureWeights[i];
    }
    for (int j = 0; j < sigma_point_in.size(); j++) {
        measureWeights[j] = measureWeights[j] / total;
    }

};

double KalmanFilter::matching_score(const cv::Mat & stat, const cv::Mat &segmented_left, const cv::Mat &segmented_right) {
ROS_ERROR("IN MSC");
cout<< stat;
    //Convert our state into Eigen::Affine3ds; one for each arm
    Eigen::Affine3d arm1 =
            Eigen::Translation3d(stat.at<double>(1, 1), stat.at<double>(2, 1), stat.at<double>(3, 1))
            *
            Eigen::AngleAxisd(stat.at<double>(4, 1), Eigen::Vector3d::UnitX())
            *
            Eigen::AngleAxisd(stat.at<double>(5, 1), Eigen::Vector3d::UnitY())
            *
            Eigen::AngleAxisd(stat.at<double>(6, 1), Eigen::Vector3d::UnitZ())
    ;
    Eigen::Affine3d arm2 =
            Eigen::Translation3d(stat.at<double>(7, 1), stat.at<double>(8, 1), stat.at<double>(9, 1))
            *
            Eigen::AngleAxisd(stat.at<double>(10, 1), Eigen::Vector3d::UnitX())
            *
            Eigen::AngleAxisd(stat.at<double>(11, 1), Eigen::Vector3d::UnitY())
            *
            Eigen::AngleAxisd(stat.at<double>(12, 1), Eigen::Vector3d::UnitZ())
    ;
            
    ROS_ERROR("Past the conversion.");      

    //Convert them into tool models
    ToolModel::toolModel arm_1;
    ToolModel::toolModel arm_2;
    convertToolModel(arm1, arm_1);
    convertToolModel(arm2, arm_2);
    
    ROS_ERROR("Past the next conversion.");

    //Render the tools and compute the matching score
    double matchingScore_arm_1 = measureFunc(toolImage_left_arm_1, toolImage_right_arm_1, arm_1, segmented_left, segmented_right, Cam_left_arm_1, Cam_right_arm_1);
    double matchingScore_arm_2 = measureFunc(toolImage_left_arm_2, toolImage_right_arm_2, arm_2, segmented_left, segmented_right, Cam_left_arm_2, Cam_right_arm_2);

    cv::imshow("Render arm 1 Left cam" ,toolImage_left_arm_1 );
    cv::imshow("Render arm 1 Right cam" ,toolImage_right_arm_1 );
    cv::imshow("Render arm 2 Left cam" ,toolImage_left_arm_2 );
    cv::imshow("Render arm 2 Right cam" ,toolImage_right_arm_2 );
    cv::waitKey(10);

    double result = (matchingScore_arm_1 + matchingScore_arm_2) / 2;

    return result;
};

void KalmanFilter::h(cv::Mat & sigma_point_out, const cv::Mat & sigma_point_in){
	//TODO: Very stupid placeholder model that assumes an absolutely perfect sensor.
	sigma_point_out = sigma_point_in.clone();
};

/******from eigen to opencv matrix****/
void KalmanFilter::convertEigenToMat(const Eigen::Affine3d & trans, cv::Mat & outputMatrix){

    outputMatrix = cv::Mat::eye(4,4,CV_64FC1);

    Eigen::Vector3d pos = trans.translation();
    Eigen::Matrix3d rot = trans.rotation();

    //this is the last col, translation
    outputMatrix.at<double>(0,3) = pos(0);
    outputMatrix.at<double>(1,3) = pos(1);
    outputMatrix.at<double>(2,3) = pos(2);

    Eigen::Vector3d col_0, col_1, col_2;
    //this is the first col, rotation x
    col_0 = rot.col(0);
    outputMatrix.at<double>(0,0) = col_0(0);
    outputMatrix.at<double>(1,0) = col_0(1);
    outputMatrix.at<double>(2,0) = col_0(2);

    //this is the second col, rotation y
    col_1 = rot.col(1);
    outputMatrix.at<double>(0,1) = col_0(0);
    outputMatrix.at<double>(1,1) = col_0(1);
    outputMatrix.at<double>(2,1) = col_0(2);

    //this is the third col, rotation z
    col_2 = rot.col(2);
    outputMatrix.at<double>(0,2) = col_0(0);
    outputMatrix.at<double>(1,2) = col_0(1);
    outputMatrix.at<double>(2,2) = col_0(2);

};

void KalmanFilter::convertToolModel(const Eigen::Affine3d & trans, ToolModel::toolModel &toolModel){
    Eigen::Vector3d pos = trans.translation();
    Eigen::Vector3d rpy = trans.rotation().eulerAngles(0, 1, 2);

    //TODO: need to add the joint angles
    toolModel.tvec_elp(0) = pos[0];
    toolModel.tvec_elp(1) = pos[1];
    toolModel.tvec_elp(2) = pos[2];
    toolModel.rvec_elp(0) = rpy[0];
    toolModel.rvec_elp(1) = rpy[1];
    toolModel.rvec_elp(2) = rpy[2];
};


//This has been retained for archival purposes.
//In the new paradigm, it will probably need to be split into two functions/steps. One that computes the sigma points, and one that takes those points and the image data and computes error.
//For our immediate purposes, a magical function that updates a mu and sigma. He kills aliens and doesn't afraid of anything.
void KalmanFilter::UnscentedKalmanFilter(const cv::Mat &mu, const cv::Mat &sigma, cv::Mat &update_mu, cv::Mat &update_sigma, const cv::Mat &zt, const cv::Mat &ut){

    //L is the dimension of the joint space for single arm

    double lambda = alpha * alpha * (L + k) - L;

    double gamma = L + lambda;
    gamma = pow(gamma, 0.5);

    ///get the square root for sigma
    cv::Mat square_sigma = cv::Mat::zeros(L, 1, CV_64FC1);

    cv::Mat s = cv::Mat(L, 1, CV_64FC1);  //need the square root for sigma
    cv::Mat vt = cv::Mat(L, L, CV_64FC1);  //need the square root for sigma
    cv::Mat u = cv::Mat(L, L, CV_64FC1);  //need the square root for sigma

    cv::SVD::compute(sigma, s, u, vt);  //s is supposed to be the one we are asking for, the sigular values

    square_sigma = s.clone(); //safe way to pass values to a cv Mat

    std::vector<cv::Mat> state_vecs;
    state_vecs.resize(2*L); ///size is 2L

    state_vecs[0] = mu.clone();   //X_nod
    for (int i = 1; i < L; ++i) {
        state_vecs[i] = state_vecs[0] + gamma * square_sigma;
        state_vecs[i + L] = state_vecs[0] - gamma * square_sigma;
    }

    double weight_mean = lambda / (L + lambda);
    double weight_covariance = weight_mean + 1-alpha * alpha + beta;

    std::vector<double> weight_vec_c;
    std::vector<double> weight_vec_m;
    weight_vec_c.resize(2*L);
    weight_vec_m.resize(2*L);

    weight_vec_c[0] = weight_mean;
    weight_vec_m[0] = weight_covariance;

    for (int l = 1; l < 2*L; ++l) {
        weight_vec_c[l] = 1/(2 * (L + lambda ));
        weight_vec_m[l] = 1/(2 * (L + lambda ));
    }

    /***get the prediction***/
    cv::Mat current_mu = cv::Mat::zeros(L,1,CV_64FC1);
    cv::Mat current_sigma = cv::Mat::zeros(L,L, CV_64FC1);

    std::vector<cv::Mat> currentState_vec;
    currentState_vec.resize(2*L);

    //TODO: missing function to get update state space vector?

    cv::Mat R = cv::Mat::eye(L,L,CV_64FC1);
    R = R * 0.037;

    for (int m = 0; m < 2*L; ++m) {
        cv::Mat temp = weight_vec_m[m] * currentState_vec[m];
        current_mu = current_mu + temp;
    }

    for (int n = 0; n < 2*L; ++n) {
        cv::Mat var_mat = currentState_vec[n] - current_mu;

        cv::Mat temp_mat = weight_vec_c[n] * var_mat * var_mat.t();
        current_sigma = current_sigma + temp_mat;
    }

    current_sigma = current_sigma + R;

    /*****get measurement****/
    std::vector<cv::Mat> updateState_vec;
    updateState_vec.resize(2*L);

    //compute new square root for current sigma
    cv::SVD::compute(current_sigma, s, u, vt);  //s is supposed to be the one we are asking for, the sigular values

    square_sigma = s.clone(); //safe way to pass values to a cv Mat

    updateState_vec[0] = current_mu.clone();   //X_nod
    for (int i = 1; i < L; ++i) {
        updateState_vec[i] = updateState_vec[0] + gamma * square_sigma;
        updateState_vec[i + L] = updateState_vec[0] - gamma * square_sigma;
    }

    std::vector<cv::Mat> current_z_vec;
    current_z_vec.resize(2*L);

    //TODO: get measurement function

    cv::Mat weighted_z = cv::Mat::zeros(2*L,1,CV_64FC1);

    for (int j = 0; j < 2*L; ++j) {
        cv::Mat temp = weight_vec_m[j] * current_z_vec[j];
        weighted_z = weighted_z + temp;
    }

    cv::Mat S_t = cv::Mat::zeros(L, L, CV_64FC1);

    for (int i1 = 0; i1 < 2*L; ++i1) {
        cv::Mat var_z = current_z_vec[i1] - weighted_z;

        cv::Mat temp_mat = weight_vec_c[i1] * var_z * var_z.t();
        S_t = S_t + temp_mat;
    }

    cv::Mat Q = cv::Mat::eye(L,L,CV_64FC1);
    Q = Q * 0.00038;

    S_t = S_t + Q;
    //get cross-covariance
    cv::Mat omega_x_z  = cv::Mat::zeros(2*L, 1, CV_64FC1);

    for (int k1 = 0; k1 < 2*L; ++k1) {
        cv::Mat var_x = updateState_vec[k1] - current_mu;

        cv::Mat var_z = current_z_vec[k1] - weighted_z;

        cv::Mat temp_mat = weight_vec_c[k1] * var_x * var_z.t();

        omega_x_z = omega_x_z + temp_mat;
    }

    ///get Kalman factor
    cv::Mat K_t = cv::Mat::zeros(L,L,CV_64FC1);
    K_t = omega_x_z * S_t.inv();

    //update mu and sigma
    update_mu = cv::Mat::zeros(L,1,CV_64FC1);  ////just in case the dimension is not match
    update_sigma = cv::Mat::zeros(L,L,CV_64FC1);

    update_mu = current_mu + K_t * (zt - weighted_z);
    update_sigma = current_sigma - K_t * S_t * K_t.t();

};
