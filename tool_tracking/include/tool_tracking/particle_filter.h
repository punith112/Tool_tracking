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

#ifndef PARTICLEFILTER_H
#define PARTICLEFILTER_H

#include <vector>
#include <stdio.h>
#include <iostream>

#include <opencv2/core/core.hpp>
#include <stdlib.h>
#include <math.h>

#include <string>
#include <cstring>

#include <tool_model_lib/tool_model.h>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <sensor_msgs/image_encodings.h>
#include <ros/ros.h>
#include <cv_bridge/cv_bridge.h>

#include <vesselness_image_filter_cpu/vesselness_lib.h>
#include <boost/random/normal_distribution.hpp>

#include <geometry_msgs/Transform.h>
#include <cwru_davinci_interface/davinci_interface.h>

#include <tf/transform_listener.h>
#include <cwru_davinci_interface/davinci_interface.h>
#include <cwru_davinci_kinematics/davinci_kinematics.h>

#include <cwru_xform_utils/xform_utils.h>
//#include <xform_utils/xform_utils.h>

class ParticleFilter {

private:
	cv::Mat Cam_left;
	cv::Mat Cam_right;

	ros::NodeHandle node_handle;

	ToolModel newToolModel;
	/**
	 * @brief predicted_real_pose is used to get the predicted real tool pose from using the forward kinematics
	 */
	ToolModel::toolModel predicted_real_pose;

	unsigned int numParticles; //total number of particles

	cv::Mat toolImage_left_arm_1; //left rendered Image for ARM 1
	cv::Mat toolImage_right_arm_1; //right rendered Image for ARM 1

	cv::Mat toolImage_left_arm_2; //left rendered Image for ARM 2
	cv::Mat toolImage_right_arm_2; //right rendered Image for ARM 2

	cv::Mat toolImage_left_temp; //left rendered Image
	cv::Mat toolImage_right_temp; //right rendered Image

	std::vector<double> matchingScores_arm_1; // particle scores (matching scores)
	std::vector<double> matchingScores_arm_2; // particle scores (matching scores)

	std::vector<std::vector <double> > particles_arm_1; // particles
	std::vector<double> particleWeights_arm_1; // particle weights calculated from matching scores

	std::vector<ToolModel::toolModel> particles_arm_2; // particles
	std::vector<double> particleWeights_arm_2; // particle weights calculated from matching scores

    cv::Mat Cam_left_arm_1;
//    cv::Mat Cam_right_arm_1;
//    cv::Mat Cam_left_arm_2;
//    cv::Mat Cam_right_arm_2;

    Davinci_fwd_solver kinematics;

    Eigen::Affine3d arm_1__cam_l;
    Eigen::Affine3d arm_2__cam_l;
    Eigen::Affine3d arm_1__cam_r;
    Eigen::Affine3d arm_2__cam_r;

    std::vector<double> sensor_1;
    std::vector<double> sensor_2;

	void projectionRightCB(const sensor_msgs::CameraInfo::ConstPtr &projectionRight);
	void projectionLeftCB(const sensor_msgs::CameraInfo::ConstPtr &projectionLeft);

	ros::Subscriber projectionMat_subscriber_r;
	ros::Subscriber projectionMat_subscriber_l;
	bool freshCameraInfo;

	double t_step;
	double t_1_step;

    double down_sample_joint;
	double down_sample_cam;

	int L;
    double error;

public:

	/*
	 * for comparing and testing
	 */
	cv::Mat raw_image_left; //left rendered Image
	cv::Mat raw_image_right; //right rendered Image

	cv::Mat P_left;
	cv::Mat P_right;

	/*
	* The default constructor
	*/
	ParticleFilter(ros::NodeHandle *nodehandle);

	/*
	 * The deconstructor 
	 */
	~ParticleFilter();

	/*
	 * The initializeParticles initializes the particles by setting the total number of particles, initial
	 * guess and randomly generating the particles around the initial guess.
	 */
	void initializeParticles();

/**
 * @brief get a coarse initialzation using forward kinematics
 */
    void getCoarseGuess();

/**
 * @brief Main tracking function
 * @param segmented_left : segmented image for left camera
 * @param segmented_right : segmented image for right camera
 * @return
 */
	void trackingTool(const cv::Mat &segmented_left, const cv::Mat &segmented_right);
/**
 * @brief low variance resampling
 * @param sampleModel : input particles
 * @param particleWeight : input normalized weights
 * @param update_particles : output particles
 */
	void resamplingParticles(const std::vector< std::vector<double> > &sampleModel,
							 const std::vector<double> &particleWeight,
							 std::vector<std::vector<double> > &update_particles);
/**
 * @brief get the p(z_t|x_t), compute the matching score based on the camera view image and rendered image
 * @param toolImage_left
 * @param toolImage_right
 * @param toolPose
 * @param segmented_left
 * @param segmented_right
 * @param Cam_left
 * @param Cam_right
 * @return matching score using matching functions: chamfer matching
 */
	double measureFuncSameCam(cv::Mat & toolImage_left, cv::Mat & toolImage_right, ToolModel::toolModel &toolPose,
							  const cv::Mat &segmented_left, const cv::Mat &segmented_right, cv::Mat &Cam_left, cv::Mat &Cam_right);
/**
 * @brief Motion model, propagte the particles using velocity computed from joint sensors
 * @param best_particle_last: last time step best particle, used to compute the nominal velocity
 * @param updatedParticles : input and output particles
 */
	void updateParticles(std::vector <double> &best_particle_last, double &maxScore, std::vector<std::vector <double> > &updatedParticles, ToolModel::toolModel & predicted_real_pose);

	void computeNoisedParticles(std::vector <double> & inputParticle, std::vector< std::vector <double> > & noisedParticles);

	void convertToolModeltoMatrix(const ToolModel::toolModel &inputToolModel, cv::Mat &toolMatrix);
	void computeRodriguesVec(const Eigen::Affine3d & trans, cv::Mat rot_vec);
	void convertEigenToMat(const Eigen::Affine3d & trans, cv::Mat & outputMatrix);
};

#endif
