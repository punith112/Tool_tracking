/*
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2016 Case Western Reserve University
 *
 *	 Ran Hao <rxh349@case.edu>
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
 *   * Neither the name of Case Western Reserve University, nor the names of its
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
 */

#ifndef KALMANFILTER_H
#define KALMANFILTER_H

#include <vector>

#include <iostream>

#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <string>
#include <cstring>

#include <tool_model_lib/tool_model.h>

#include <ros/ros.h>
#include <cv_bridge/cv_bridge.h>

#include <image_transport/image_transport.h>

#include <vesselness_image_filter_cpu/vesselness_lib.h>
#include <boost/random/normal_distribution.hpp>

#include <geometry_msgs/Transform.h>
#include <tf/transform_listener.h>
#include <cwru_davinci_interface/davinci_interface.h>
#include <cwru_davinci_kinematics/davinci_kinematics.h>
#include <sensor_msgs/image_encodings.h>
#include <cwru_opencv_common/projective_geometry.h>

#include <cwru_xform_utils/xform_utils.h>
//#include <xform_utils/xform_utils.h>

class KalmanFilter {

private:
    ros::NodeHandle nh_;  //may need this

    ToolModel ukfToolModel;  /// it should be set up the first time, probably need updates of the camera poses

    ToolModel::toolModel initial; //initial configuration

    cv::Mat toolImage_left_arm_1; //left rendered Image for ARM 1
    cv::Mat toolImage_right_arm_1; //right rendered Image for ARM 1

    cv::Mat toolImage_left_arm_2; //left rendered Image for ARM 2
    cv::Mat toolImage_right_arm_2; //right rendered Image for ARM 2

	cv::Mat toolImage_cam_left;
	cv::Mat toolImage_cam_right;

	cv::Mat Cam_left_arm_1;
    cv::Mat Cam_right_arm_1;
    cv::Mat Cam_left_arm_2;
    cv::Mat Cam_right_arm_2;

    int L;  ///DOF for both arms.

    const static double alpha = 0.005;
    const static double k = 0.0; //TODO: how much?
    const static double beta = 2;

    ros::Subscriber com_s1;
    ros::Subscriber com_s2;

    void newCommandCallback1(const sensor_msgs::JointState::ConstPtr &incoming);
    void newCommandCallback2(const sensor_msgs::JointState::ConstPtr &incoming);

	double last_update;

    cv::Mat cmd_1;
    cv::Mat cmd_2;

    double cmd_time_1;
    double cmd_time_2;
    cv::Mat cmd_1_old;
    cv::Mat cmd_2_old;
    double cmd_time_1_old;
    double cmd_time_2_old;

	bool fvc_1;
	bool fvc_2;

	/************using variables*************/
    std::vector<double> sensor_1;
    std::vector<double> sensor_2;

    cv::Mat kalman_mu_arm1;
    cv::Mat kalman_sigma_arm1;

	cv::Mat kalman_mu_arm2;
	cv::Mat kalman_sigma_arm2;

    Davinci_fwd_solver kinematics;

    Eigen::Affine3d arm_1__cam_l;
    Eigen::Affine3d arm_2__cam_l;
    Eigen::Affine3d arm_1__cam_r;
    Eigen::Affine3d arm_2__cam_r;

    ros::Subscriber projectionMat_subscriber_r;
    ros::Subscriber projectionMat_subscriber_l;

    void projectionRightCB(const sensor_msgs::CameraInfo::ConstPtr &projectionRight);
    void projectionLeftCB(const sensor_msgs::CameraInfo::ConstPtr &projectionLeft);

    cv::Mat P_left;
    cv::Mat P_right;

    double matching_score(const cv::Mat & stat,cv::Mat &left_image,cv::Mat &right_image,
						  cv::Mat &cam_left, cv::Mat &cam_right);
	
	void g(cv::Mat & sigma_point_out, const cv::Mat & sigma_point_in, const cv::Mat & zt);
	void h(cv::Mat & sigma_point_out, const cv::Mat & sigma_point_in);
    void computeSigmaMeasures(std::vector<double> & measureWeights, const std::vector<cv::Mat_<double> > & sigma_point_in,
							  cv::Mat &left_image,cv::Mat &right_image,
							  cv::Mat &cam_left, cv::Mat &cam_right);


	/*****image subscribing part*****/
	cv::Mat seg_left;
	cv::Mat seg_right;

	cv::Mat segmentation(cv::Mat &InputImg);

	bool freshSegImage;
	bool freshCameraInfo;

public:

	cv::Mat tool_rawImg_left;
	cv::Mat tool_rawImg_right;

    /*
    * The default constructor
    */
    KalmanFilter(ros::NodeHandle *nodehandle);

    /*
     * The deconstructor
     */
    ~KalmanFilter();

    /***consider getting a timer to debug***/
    // void timerCallback(const ros::TimerEvent&);

    /*
     * This is the main function for tracking the needle. This function is called and it syncs all of the functions
    */

    void print_affine(Eigen::Affine3d &affine);

	void UKF_double_arm();
	void getSquareRootCov(cv::Mat &sigma_cov, cv::Mat &square_root);

    void update(std::vector <double> &sensor_data, cv::Mat & kalman_mu, cv::Mat & kalman_sigma,
				cv::Mat &left_image,cv::Mat &right_image,
				cv::Mat &cam_left, cv::Mat &cam_right);

    void convertToolModel(const cv::Mat & trans, ToolModel::toolModel &toolModel, double ja1, double ja2, double ja3);
    /*
     * Unscented Kalman filter update
     */
    void UnscentedKalmanFilter(
            const cv::Mat &mu,
            const cv::Mat &sigma,
            cv::Mat &update_mu,
            cv::Mat &update_sigma,
            const cv::Mat &zt
    );
    double measureFunc(cv::Mat & toolImage_left, cv::Mat & toolImage_right, ToolModel::toolModel &toolPose, cv::Mat &Cam_left, cv::Mat &Cam_right, cv::Mat & rawImage_left,
					   cv::Mat & rawImage_right);
	double measureFuncSameCam(cv::Mat & toolImage_cam, ToolModel::toolModel &toolPose_left, ToolModel::toolModel &toolPose_right,
											const cv::Mat &segmented_cam, const cv::Mat & Projection_mat, cv::Mat &raw_img, cv::Mat &Cam_matrix_tool_left, cv::Mat &Cam_matrix_tool_right);
	void computeRodriguesVec(const Eigen::Affine3d & trans, cv::Mat rot_vec);
    void convertEigenToMat(const Eigen::Affine3d & trans, cv::Mat & outputMatrix);

};
#endif
