/*
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2013-, Open Perception, Inc.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided
 * with the distribution.
 * * Neither the name of the copyright holder(s) nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: Matteo Munaro [matteo.munaro@dei.unipd.it], Nicola Ristè
 *
 */

#include <open_ptrack/detection/ground_segmentation.h>

template <typename PointT>
open_ptrack::detection::GroundplaneEstimation<PointT>::GroundplaneEstimation (int ground_estimation_mode)
{
  ground_estimation_mode_ = ground_estimation_mode;

  if ((ground_estimation_mode > 3) || (ground_estimation_mode < 0))
  {
    ground_estimation_mode_ = 0;
    std::cout << "ERROR: invalid mode for groundplane segmentation. Manual mode is selected." << std::endl;
  }
}

template <typename PointT>
open_ptrack::detection::GroundplaneEstimation<PointT>::~GroundplaneEstimation()
{

}

template <typename PointT> void
open_ptrack::detection::GroundplaneEstimation<PointT>::setInputCloud (PointCloudPtr& cloud)
{
  cloud_ = cloud;
}

template <typename PointT> bool
open_ptrack::detection::GroundplaneEstimation<PointT>::tooManyNaN(PointCloudConstPtr cloud, float max_ratio)
{
  int nan_counter = 0;
  for(unsigned int i = 0; i < cloud->size(); i++)
  {
    // If the point has a non-valid coordinate:
    if(isnan(cloud->points[i].x) || isnan(cloud->points[i].y) || isnan(cloud->points[i].z) )
    { // update the counter:
      nan_counter++;
    }
  }

  // If the nan ratio is over max_ratio:
  if( (float) nan_counter/cloud->size() > max_ratio )
    return true;    // too many NaNs, frame invalid
  else
    return false;
}

template <typename PointT> Eigen::VectorXf
open_ptrack::detection::GroundplaneEstimation<PointT>::compute ()
{
  Eigen::VectorXf ground_coeffs;
  ground_coeffs.resize(4);

  // Manual mode:
  if (ground_estimation_mode_ == 0)
  {
    std::cout << "Manual mode for ground plane estimation." << std::endl;

    // Initialize viewer:
    pcl::visualization::PCLVisualizer viewer("Pick 3 points");
    pcl::visualization::PointCloudColorHandlerRGBField<PointT> rgb(cloud_);
    viewer.addPointCloud<PointT> (cloud_, rgb, "input_cloud");
    viewer.setCameraPosition(0,0,-2,0,-1,0,0);

    // Add point picking callback to viewer:
    struct callback_args cb_args;
    PointCloudPtr clicked_points_3d (new PointCloud);
    cb_args.clicked_points_3d = clicked_points_3d;
    cb_args.viewerPtr = &viewer;
    viewer.registerPointPickingCallback (GroundplaneEstimation::pp_callback, (void*)&cb_args);

    // Spin until 'Q' is pressed:
    viewer.spin();
    viewer.setSize(1,1);  // resize viewer in order to make it disappear
    viewer.spinOnce();
    viewer.close();       // close method does not work
    std::cout << "done." << std::endl;

    // Keep only the last three clicked points:
    while(clicked_points_3d->points.size()>3)
    {
      clicked_points_3d->points.erase(clicked_points_3d->points.begin());
    }

    // Ground plane estimation:
    std::vector<int> clicked_points_indices;
    for (unsigned int i = 0; i < clicked_points_3d->points.size(); i++)
      clicked_points_indices.push_back(i);
    pcl::SampleConsensusModelPlane<PointT> model_plane(clicked_points_3d);
    model_plane.computeModelCoefficients(clicked_points_indices,ground_coeffs);
    std::cout << "Ground plane coefficients: " << ground_coeffs(0) << ", " << ground_coeffs(1) << ", " << ground_coeffs(2) <<
        ", " << ground_coeffs(3) << "." << std::endl;
  }

  // Semi-automatic mode:
  if (ground_estimation_mode_ == 1)
  {
    std::cout << "Semi-automatic mode for ground plane estimation." << std::endl;

    // Normals computation:
    pcl::IntegralImageNormalEstimation<PointT, pcl::Normal> ne;
    ne.setNormalEstimationMethod (ne.COVARIANCE_MATRIX);
    ne.setMaxDepthChangeFactor (0.03f);
    ne.setNormalSmoothingSize (20.0f);
    pcl::PointCloud<pcl::Normal>::Ptr normal_cloud (new pcl::PointCloud<pcl::Normal>);
    ne.setInputCloud (cloud_);
    ne.compute (*normal_cloud);

    // Multi plane segmentation initialization:
    std::vector<pcl::PlanarRegion<PointT>, Eigen::aligned_allocator<pcl::PlanarRegion<PointT> > > regions;
    pcl::OrganizedMultiPlaneSegmentation<PointT, pcl::Normal, pcl::Label> mps;
    mps.setMinInliers (500);
    mps.setAngularThreshold (2.0 * M_PI / 180);
    mps.setDistanceThreshold (0.2);
    mps.setInputNormals (normal_cloud);
    mps.setInputCloud (cloud_);
    mps.segmentAndRefine (regions);

    std::cout << "Found " << regions.size() << " planar regions." << std::endl;

    // Color planar regions with different colors:
    PointCloudPtr colored_cloud (new PointCloud);
    colored_cloud = colorRegions(regions);
    if (regions.size()>0)
    {
      // Viewer initialization:
      pcl::visualization::PCLVisualizer viewer("PCL Viewer");
      pcl::visualization::PointCloudColorHandlerRGBField<PointT> rgb(colored_cloud);
      viewer.addPointCloud<PointT> (colored_cloud, rgb, "input_cloud");
      viewer.setCameraPosition(0,0,-2,0,-1,0,0);

      // Add point picking callback to viewer:
      struct callback_args cb_args;
      typename pcl::PointCloud<PointT>::Ptr clicked_points_3d (new pcl::PointCloud<PointT>);
      cb_args.clicked_points_3d = clicked_points_3d;
      cb_args.viewerPtr = &viewer;
      viewer.registerPointPickingCallback (GroundplaneEstimation::pp_callback, (void*)&cb_args);
      std::cout << "Shift+click on a floor point, then press 'Q'..." << std::endl;

      // Spin until 'Q' is pressed:
      viewer.spin();
      viewer.setSize(1,1);  // resize viewer in order to make it disappear
      viewer.spinOnce();
      viewer.close();       // close method does not work
      std::cout << "done." << std::endl;

      // Find plane closest to clicked point:
      unsigned int index = 0;
      float min_distance = FLT_MAX;
      float distance;

      float X = cb_args.clicked_points_3d->points[clicked_points_3d->points.size() - 1].x;
      float Y = cb_args.clicked_points_3d->points[clicked_points_3d->points.size() - 1].y;
      float Z = cb_args.clicked_points_3d->points[clicked_points_3d->points.size() - 1].z;

      for(unsigned int i = 0; i < regions.size(); i++)
      {
        float a = regions[i].getCoefficients()[0];
        float b = regions[i].getCoefficients()[1];
        float c = regions[i].getCoefficients()[2];
        float d = regions[i].getCoefficients()[3];

        distance = (float) (fabs((a*X + b*Y + c*Z + d)))/(sqrtf(a*a+b*b+c*c));

        if(distance < min_distance)
        {
          min_distance = distance;
          index = i;
        }
      }

      ground_coeffs[0] = regions[index].getCoefficients()[0];
      ground_coeffs[1] = regions[index].getCoefficients()[1];
      ground_coeffs[2] = regions[index].getCoefficients()[2];
      ground_coeffs[3] = regions[index].getCoefficients()[3];

      std::cout << "Ground plane coefficients: " << regions[index].getCoefficients()[0] << ", " << regions[index].getCoefficients()[1] << ", " <<
          regions[index].getCoefficients()[2] << ", " << regions[index].getCoefficients()[3] << "." << std::endl;
    }
  }

  // Automatic mode:
  if ((ground_estimation_mode_ == 2) || (ground_estimation_mode_ == 3))
  {
    std::cout << "Automatic mode for ground plane estimation." << std::endl;

    // Normals computation:
    pcl::IntegralImageNormalEstimation<PointT, pcl::Normal> ne;
    ne.setNormalEstimationMethod (ne.COVARIANCE_MATRIX);
    ne.setMaxDepthChangeFactor (0.03f);
    ne.setNormalSmoothingSize (20.0f);
    pcl::PointCloud<pcl::Normal>::Ptr normal_cloud (new pcl::PointCloud<pcl::Normal>);
    ne.setInputCloud (cloud_);
    ne.compute (*normal_cloud);

    // Multi plane segmentation initialization:
    std::vector<pcl::PlanarRegion<PointT>, Eigen::aligned_allocator<pcl::PlanarRegion<PointT> > > regions;
    pcl::OrganizedMultiPlaneSegmentation<PointT, pcl::Normal, pcl::Label> mps;
    mps.setMinInliers (500);
    mps.setAngularThreshold (2.0 * M_PI / 180);
    mps.setDistanceThreshold (0.2);
    mps.setInputNormals (normal_cloud);
    mps.setInputCloud (cloud_);
    mps.segmentAndRefine (regions);

//    std::cout << "Found " << regions.size() << " planar regions." << std::endl;

    // Removing planes not compatible with camera roll ~= 0:
    unsigned int i = 0;
    while(i < regions.size())
    { // Check on the normal to the plane:
      if(fabs(regions[i].getCoefficients()[1]) < 0.70)
      {
        regions.erase(regions.begin()+i);
      }
      else
        ++i;
    }

    // Order planar regions according to height (y coordinate):
    std::sort(regions.begin(), regions.end(), GroundplaneEstimation::planeHeightComparator);

    // Color selected planar region in red:
    PointCloudPtr colored_cloud (new PointCloud);
    colored_cloud = colorRegions(regions, 0);

    // If at least a valid plane remained:
    if (regions.size()>0)
    {
      ground_coeffs[0] = regions[0].getCoefficients()[0];
      ground_coeffs[1] = regions[0].getCoefficients()[1];
      ground_coeffs[2] = regions[0].getCoefficients()[2];
      ground_coeffs[3] = regions[0].getCoefficients()[3];

      std::cout << "Ground plane coefficients: " << regions[0].getCoefficients()[0] << ", " << regions[0].getCoefficients()[1] << ", " <<
          regions[0].getCoefficients()[2] << ", " << regions[0].getCoefficients()[3] << "." << std::endl;

      // Result visualization:
      if (ground_estimation_mode_ == 2)
      {
        // Viewer initialization:
        pcl::visualization::PCLVisualizer viewer("PCL Viewer");
        pcl::visualization::PointCloudColorHandlerRGBField<PointT> rgb(colored_cloud);
        viewer.addPointCloud<PointT> (colored_cloud, rgb, "input_cloud");
        viewer.setCameraPosition(0,0,-2,0,-1,0,0);

        // Spin until 'Q' is pressed:
        viewer.spin();
        viewer.setSize(1,1);  // resize viewer in order to make it disappear
        viewer.spinOnce();
        viewer.close();       // close method does not work
      }
    }
    else
    {
      std::cout << "ERROR: no valid ground plane found!" << std::endl;
    }
  }

  return ground_coeffs;
}

template <typename PointT> void
open_ptrack::detection::GroundplaneEstimation<PointT>::pp_callback (const pcl::visualization::PointPickingEvent& event, void* args)
{
  struct callback_args* data = (struct callback_args *)args;
  if (event.getPointIndex () == -1)
    return;
  PointT current_point;
  event.getPoint(current_point.x, current_point.y, current_point.z);
  data->clicked_points_3d->points.push_back(current_point);
  // Draw clicked points in red:
  pcl::visualization::PointCloudColorHandlerCustom<PointT> red (data->clicked_points_3d, 255, 0, 0);
  data->viewerPtr->removePointCloud("clicked_points");
  data->viewerPtr->addPointCloud(data->clicked_points_3d, red, "clicked_points");
  data->viewerPtr->setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 10, "clicked_points");
  std::cout << current_point.x << " " << current_point.y << " " << current_point.z << std::endl;
}

template <typename PointT> bool
open_ptrack::detection::GroundplaneEstimation<PointT>::planeHeightComparator (pcl::PlanarRegion<PointT> region1, pcl::PlanarRegion<PointT> region2)
{
  return region1.getCentroid()[1] > region2.getCentroid()[1];
}

template <typename PointT> typename open_ptrack::detection::GroundplaneEstimation<PointT>::PointCloudPtr
open_ptrack::detection::GroundplaneEstimation<PointT>::colorRegions (std::vector<pcl::PlanarRegion<PointT>, Eigen::aligned_allocator<pcl::PlanarRegion<PointT> > > regions, int index)
{
  // Color different planes with different colors:
  PointCloudPtr colored_cloud (new PointCloud);
  pcl::copyPointCloud(*cloud_, *colored_cloud);
  float voxel_size = 0.06;

  for (size_t i = 0; i < regions.size (); i++)
  {
    Eigen::Vector3f centroid = regions[i].getCentroid ();
    Eigen::Vector4f model = regions[i].getCoefficients ();

    pcl::IndicesPtr inliers(new std::vector<int>);
    boost::shared_ptr<pcl::SampleConsensusModelPlane<PointT> > ground_model(new pcl::SampleConsensusModelPlane<PointT>(cloud_));
    ground_model->selectWithinDistance(model, voxel_size, *inliers);

    int r = (rand() % 256);
    int g = (rand() % 256);
    int b = (rand() % 256);

    for(unsigned int j = 0; j < inliers->size(); j++)
    {
      colored_cloud->points.at(inliers->at(j)).r = r;
      colored_cloud->points.at(inliers->at(j)).g = g;
      colored_cloud->points.at(inliers->at(j)).b = b;
    }
  }

  // If index is passed, color index-th region in red:
  if (index >= 0 && !regions.empty())
  {
    Eigen::Vector3f centroid = regions[index].getCentroid ();
    Eigen::Vector4f model = regions[index].getCoefficients ();

    pcl::IndicesPtr inliers(new std::vector<int>);
    boost::shared_ptr<pcl::SampleConsensusModelPlane<PointT> > ground_model(new pcl::SampleConsensusModelPlane<PointT>(cloud_));
    ground_model->selectWithinDistance(model, voxel_size, *inliers);

    for(unsigned int j = 0; j < inliers->size(); j++)
    {
      colored_cloud->points.at(inliers->at(j)).r = 255;
      colored_cloud->points.at(inliers->at(j)).g = 0;
      colored_cloud->points.at(inliers->at(j)).b = 0;
    }
  }

  return colored_cloud;
}
