// This is the Lidar Odometry And Mapping (LOAM) for solid-state lidar (for example: livox lidar),
// which suffer form motion blur due the continously scan pattern and low range of fov.

// Developer: Lin Jiarong  ziv.lin.ljr@gmail.com

//   J. Zhang and S. Singh. LOAM: Lidar Odometry and Mapping in Real-time.
//     Robotics: Science and Systems Conference (RSS). Berkeley, CA, July 2014.

// Copyright 2013, Ji Zhang, Carnegie Mellon University
// Further contributions copyright (c) 2016, Southwest Research Institute
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from this
//    software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifndef LASER_MAPPING_HPP
#define LASER_MAPPING_HPP

#include <ceres/ceres.h>
#include <eigen3/Eigen/Dense>
#include <geometry_msgs/PoseStamped.h>
#include <iostream>
#include <math.h>
#include <mutex>
#include <nav_msgs/Odometry.h>
#include <nav_msgs/Path.h>
#include <pcl/filters/statistical_outlier_removal.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/keypoints/uniform_sampling.h>
#include <pcl/kdtree/kdtree_flann.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl_conversions/pcl_conversions.h>
#include <queue>
#include <ros/ros.h>
#include <sensor_msgs/Imu.h>
#include <sensor_msgs/PointCloud2.h>
#include <string>
#include <tf/transform_broadcaster.h>
#include <tf/transform_datatypes.h>
#include <thread>
#include <vector>

#include "ceres_icp.hpp"
#include "tools/common.h"
#include "tools/logger.hpp"
#include "tools/pcl_tools.hpp"

#define PUB_SURROUND_PTS 1
#define PCD_SAVE_RAW 1
#define PUB_DEBUG_INFO 1

int slover_type = 1; // if 0, using solver in laserFactor.hpp

int g_if_undistore = 0;

#define CORNER_MIN_MAP_NUM 0
#define SURFACE_MIN_MAP_NUM 50

#define ICP_PLANE 1
#define ICP_LINE 1
int MOTION_DEBLUR = 0;
#define CUBE_W 50.0 // 10
#define CUBE_H 50.0 // 10
#define CUBE_D 50.0 // 5

#define BLUR_SCALE 1.0

int line_search_num = 5;
int IF_LINE_FEATURE_CHECK = 1;
int plane_search_num = 5;
int IF_PLANE_FEATURE_CHECK = 0;

using namespace PCL_TOOLS;
using namespace Common_tools;

struct Data_pair
{
    sensor_msgs::PointCloud2ConstPtr m_pc_corner;
    sensor_msgs::PointCloud2ConstPtr m_pc_full;
    sensor_msgs::PointCloud2ConstPtr m_pc_plane;
    bool                             m_has_pc_corner = 0;
    bool                             m_has_pc_full = 0;
    bool                             m_has_pc_plane = 0;

    void add_pc_corner( sensor_msgs::PointCloud2ConstPtr ros_pc )
    {
        m_pc_corner = ros_pc;
        m_has_pc_corner = true;
    }

    void add_pc_plane( sensor_msgs::PointCloud2ConstPtr ros_pc )
    {
        m_pc_plane = ros_pc;
        m_has_pc_plane = true;
    }

    void add_pc_full( sensor_msgs::PointCloud2ConstPtr ros_pc )
    {
        m_pc_full = ros_pc;
        m_has_pc_full = true;
    }

    bool is_completed()
    {
        return ( m_has_pc_corner & m_has_pc_full & m_has_pc_plane );
    }
};

class Laser_mapping
{
  public:
    int frameCount = 0;
    int m_para_min_match_blur = 0.0;
    int m_para_max_match_blur = 0.3;
    //int   m_max_buffer_size = 5;
    int   m_max_buffer_size = 50000000;
    int   m_para_icp_max_iterations = 20;
    int   m_para_cere_max_iterations = 100;
    float m_para_max_angular_rate = 200.0 / 50.0; // max angular rate = 90.0 /50.0 deg/s
    float m_para_max_speed = 100.0 / 50.0;        // max speed = 10 m/s
    float m_max_final_cost = 100.0;
    int   m_mapping_init_accumulate_frames = 100;
    int   m_kmean_filter_count = 3;
    int   m_kmean_filter_threshold = 2.0;

    int m_para_laser_cloud_center_width = CUBE_W;
    int m_para_laser_cloud_center_height = CUBE_H;
    int m_para_laser_cloud_center_depth = CUBE_D;
    int m_para_laser_cloud_width = CUBE_W * 2 + 1;
    int m_para_laser_cloud_height = CUBE_H * 2 + 1;
    int m_para_laser_cloud_depth = CUBE_D * 2 + 1;

    int m_laser_cloud_num = m_para_laser_cloud_width * m_para_laser_cloud_height * m_para_laser_cloud_depth;

    double m_time_pc_corner_past = 0;
    double m_time_pc_surface_past = 0;
    double m_time_pc_full = 0;
    double m_time_odom = 0;
    float  m_last_time_stamp = 0;
    float  m_minimum_pt_time_stamp = 0;
    float  m_maximum_pt_time_stamp = 1.0;
    float  m_last_max_blur = 0.0;

    double m_map_downsample_para = 0.5;

    double m_interpolatation_theta;
    Eigen::Matrix<double, 3, 1> m_interpolatation_omega;
    Eigen::Matrix<double, 3, 3> m_interpolatation_omega_hat;
    Eigen::Matrix<double, 3, 3> m_interpolatation_omega_hat_sq2;

    // points in every cube
    pcl::PointCloud<PointType>::Ptr *m_laser_cloud_corner_array;
    pcl::PointCloud<PointType>::Ptr *m_laser_cloud_surface_array;

    // ouput: all visualble cube points
    pcl::PointCloud<PointType>::Ptr m_laser_cloud_surround;

    // surround points in map to build tree
    pcl::PointCloud<PointType>::Ptr m_laser_cloud_corner_from_map;
    pcl::PointCloud<PointType>::Ptr m_laser_cloud_surf_from_map;

    //input & output: points in one frame. local --> global
    pcl::PointCloud<PointType>::Ptr m_laser_cloud_full_res;

    // input: from odom
    pcl::PointCloud<PointType>::Ptr m_laser_cloud_corner_last;
    pcl::PointCloud<PointType>::Ptr m_laser_cloud_surf_last;

    //kd-tree
    pcl::KdTreeFLANN<PointType>::Ptr m_kdtree_corner_from_map;
    pcl::KdTreeFLANN<PointType>::Ptr m_kdtree_surf_from_map;

    int m_laser_cloud_valid_Idx[ 1024 ];
    int m_laser_cloud_surround_Idx[ 1024 ];

    double m_para_buffer_RT[ 7 ] = { 0, 0, 0, 1, 0, 0, 0 };
    double m_para_buffer_RT_last[ 7 ] = { 0, 0, 0, 1, 0, 0, 0 };
    double m_para_buffer_incremental[ 7 ] = { 0, 0, 0, 1, 0, 0, 0 };

    const Eigen::Quaterniond m_q_I = Eigen::Quaterniond( 1, 0, 0, 0 );

    Eigen::Map<Eigen::Quaterniond> m_q_w_curr = Eigen::Map<Eigen::Quaterniond>( m_para_buffer_RT );
    Eigen::Map<Eigen::Vector3d>    m_t_w_curr = Eigen::Map<Eigen::Vector3d>( m_para_buffer_RT + 4 );

    Eigen::Map<Eigen::Quaterniond> m_q_w_last = Eigen::Map<Eigen::Quaterniond>( m_para_buffer_RT_last );
    Eigen::Map<Eigen::Vector3d>    m_t_w_last = Eigen::Map<Eigen::Vector3d>( m_para_buffer_RT_last + 4 );

    Eigen::Map<Eigen::Quaterniond> m_q_w_incre = Eigen::Map<Eigen::Quaterniond>( m_para_buffer_incremental );
    Eigen::Map<Eigen::Vector3d>    m_t_w_incre = Eigen::Map<Eigen::Vector3d>( m_para_buffer_incremental + 4 );

    std::map<double, Data_pair *> m_map_data_pair;
    std::queue<Data_pair *> m_queue_avail_data;

    std::queue<nav_msgs::Odometry::ConstPtr> m_odom_que;
    std::mutex                               m_mutex_buf;

    pcl::VoxelGrid<PointType>                 m_down_sample_filter_corner;
    pcl::VoxelGrid<PointType>                 m_down_sample_filter_surface;
    pcl::StatisticalOutlierRemoval<PointType> m_filter_k_means;

    std::vector<int>   m_point_search_Idx;
    std::vector<float> m_point_search_sq_dis;

    nav_msgs::Path m_laser_after_mapped_path;

    int       m_if_save_to_pcd_files = 1;
    PCL_tools m_pcl_tools_aftmap;
    PCL_tools m_pcl_tools_raw;

    File_logger m_file_logger;

    ros::Publisher  m_pub_laser_cloud_surround, m_pub_laser_cloud_map, m_pub_laser_cloud_full_res, m_pub_odom_aft_mapped, m_pub_odom_aft_mapped_hight_frec, m_pub_laser_aft_mapped_path;
    ros::NodeHandle m_ros_node_handle;
    ros::Subscriber m_sub_laser_cloud_corner_last, m_sub_laser_cloud_surf_last, m_sub_laser_odom, m_sub_laser_cloud_full_res;
#if PUB_DEBUG_INFO
    ros::Publisher m_pub_last_corner_pts, m_pub_last_surface_pts;
#endif

    Laser_mapping()
    {
        m_laser_cloud_corner_array = new pcl::PointCloud<PointType>::Ptr[ m_laser_cloud_num ];
        m_laser_cloud_surface_array = new pcl::PointCloud<PointType>::Ptr[ m_laser_cloud_num ];

        m_laser_cloud_corner_last = pcl::PointCloud<PointType>::Ptr( new pcl::PointCloud<PointType>() );
        m_laser_cloud_surf_last = pcl::PointCloud<PointType>::Ptr( new pcl::PointCloud<PointType>() );
        m_laser_cloud_surround = pcl::PointCloud<PointType>::Ptr( new pcl::PointCloud<PointType>() );

        m_laser_cloud_corner_from_map = pcl::PointCloud<PointType>::Ptr( new pcl::PointCloud<PointType>() );
        m_laser_cloud_surf_from_map = pcl::PointCloud<PointType>::Ptr( new pcl::PointCloud<PointType>() );
        m_laser_cloud_full_res = pcl::PointCloud<PointType>::Ptr( new pcl::PointCloud<PointType>() );

        m_kdtree_corner_from_map = pcl::KdTreeFLANN<PointType>::Ptr( new pcl::KdTreeFLANN<PointType>() );
        m_kdtree_surf_from_map = pcl::KdTreeFLANN<PointType>::Ptr( new pcl::KdTreeFLANN<PointType>() );

        for ( int i = 0; i < m_laser_cloud_num; i++ )
        {
            m_laser_cloud_corner_array[ i ].reset( new pcl::PointCloud<PointType>() );
            m_laser_cloud_surface_array[ i ].reset( new pcl::PointCloud<PointType>() );
        }

        init_parameters( m_ros_node_handle );

        //livox_corners
        m_sub_laser_cloud_corner_last = m_ros_node_handle.subscribe<sensor_msgs::PointCloud2>( "/pc2_corners", 10000, &Laser_mapping::laserCloudCornerLastHandler, this );
        m_sub_laser_cloud_surf_last = m_ros_node_handle.subscribe<sensor_msgs::PointCloud2>( "/pc2_surface", 10000, &Laser_mapping::laserCloudSurfLastHandler, this );
        m_sub_laser_cloud_full_res = m_ros_node_handle.subscribe<sensor_msgs::PointCloud2>( "/pc2_full", 10000, &Laser_mapping::laserCloudFullResHandler, this );

        m_pub_laser_cloud_surround = m_ros_node_handle.advertise<sensor_msgs::PointCloud2>( "/laser_cloud_surround", 10000 );
#if PUB_DEBUG_INFO
        m_pub_last_corner_pts = m_ros_node_handle.advertise<sensor_msgs::PointCloud2>( "/features_corners", 10000 );
        m_pub_last_surface_pts = m_ros_node_handle.advertise<sensor_msgs::PointCloud2>( "/features_surface", 10000 );
#endif
        m_pub_laser_cloud_map = m_ros_node_handle.advertise<sensor_msgs::PointCloud2>( "/laser_cloud_map", 10000 );
        m_pub_laser_cloud_full_res = m_ros_node_handle.advertise<sensor_msgs::PointCloud2>( "/velodyne_cloud_registered", 10000 );
        m_pub_odom_aft_mapped = m_ros_node_handle.advertise<nav_msgs::Odometry>( "/aft_mapped_to_init", 10000 );
        m_pub_odom_aft_mapped_hight_frec = m_ros_node_handle.advertise<nav_msgs::Odometry>( "/aft_mapped_to_init_high_frec", 10000 );
        m_pub_laser_aft_mapped_path = m_ros_node_handle.advertise<nav_msgs::Path>( "/aft_mapped_path", 10000 );

        cout << "Laser_mapping init OK" << endl;
    };

    ~Laser_mapping(){};

    void compute_interpolatation_rodrigue( const Eigen::Quaterniond &q_in, Eigen::Matrix<double, 3, 1> &angle_axis, double &angle_theta, Eigen::Matrix<double, 3, 3> &hat )
    {
        Eigen::AngleAxisd newAngleAxis( q_in );
        angle_axis = newAngleAxis.axis();
        angle_axis = angle_axis / angle_axis.norm();
        angle_theta = newAngleAxis.angle();
        hat.setZero();
        hat( 0, 1 ) = -angle_axis( 2 );
        hat( 1, 0 ) = angle_axis( 2 );
        hat( 0, 2 ) = angle_axis( 1 );
        hat( 2, 0 ) = -angle_axis( 1 );
        hat( 1, 2 ) = -angle_axis( 0 );
        hat( 2, 1 ) = angle_axis( 0 );
    }

    Data_pair *get_data_pair( const double &time_stamp )
    {
        std::map<double, Data_pair *>::iterator it = m_map_data_pair.find( time_stamp );
        if ( it == m_map_data_pair.end() )
        {
            Data_pair *date_pair_ptr = new Data_pair();
            m_map_data_pair.insert( std::make_pair( time_stamp, date_pair_ptr ) );
            return date_pair_ptr;
        }
        else
        {
            return it->second;
        }
    }

    void init_parameters( ros::NodeHandle &nh )
    {

        float lineRes = 0;
        float planeRes = 0;

        nh.param<float>( "mapping_line_resolution", lineRes, 0.4 );
        nh.param<float>( "mapping_plane_resolution", planeRes, 0.8 );
        nh.param<int>( "icp_maximum_iteration", m_para_icp_max_iterations, 20 );
        nh.param<int>( "ceres_maximum_iteration", m_para_cere_max_iterations, 20 );
        nh.param<int>( "if_motion_deblur", MOTION_DEBLUR, 1 );

        //MOTION_DEBLUR = 1;
        nh.param<float>( "max_allow_incre_R", m_para_max_angular_rate, 200.0 / 50.0 );
        nh.param<float>( "max_allow_incre_T", m_para_max_speed, 100.0 / 50.0 );
        nh.param<float>( "max_allow_final_cost", m_max_final_cost, 1.0 );
        nh.param<int>( "maximum_mapping_buffer", m_max_buffer_size, 5 );
        nh.param<int>( "mapping_init_accumulate_frames", m_mapping_init_accumulate_frames, 50 );//old is 50
        nh.param<double>( "mapping_downsample_para", m_map_downsample_para, 0.5 );//old is 50

        string pcd_save_dir_name;
        nh.param<int>( "if_save_to_pcd_files", m_if_save_to_pcd_files, 0 );

        string log_save_dir_name;
        nh.param<std::string>( "log_save_dir", log_save_dir_name, "../" );
        m_file_logger.set_log_dir( log_save_dir_name );
        m_file_logger.init( "mapping.log" );

        if ( m_if_save_to_pcd_files )
        {
            nh.param<std::string>( "pcd_save_dir", pcd_save_dir_name, std::string( "./" ) );
            m_pcl_tools_aftmap.set_save_dir_name( pcd_save_dir_name );
            m_pcl_tools_raw.set_save_dir_name( pcd_save_dir_name );
        }

        LOG_FILE_LINE( m_file_logger );
        *m_file_logger.get_ostream() << m_file_logger.version();

        printf( "line resolution %f plane resolution %f \n", lineRes, planeRes );
        m_file_logger.printf( "line resolution %f plane resolution %f \n", lineRes, planeRes );
        m_down_sample_filter_corner.setLeafSize( lineRes, lineRes, lineRes );
        m_down_sample_filter_surface.setLeafSize( planeRes, planeRes, planeRes );

        m_filter_k_means.setMeanK( m_kmean_filter_count );
        m_filter_k_means.setStddevMulThresh( m_kmean_filter_threshold );
    }

    void set_ceres_solver_bound( ceres::Problem &problem )
    {
        for ( unsigned int i = 0; i < 3; i++ )
        {

            problem.SetParameterLowerBound( m_para_buffer_incremental + 4, i, -m_para_max_speed );
            problem.SetParameterUpperBound( m_para_buffer_incremental + 4, i, +m_para_max_speed );
        }
    }

    void pointAssociateToMap( PointType const *const pi, PointType *const po, double interpolate_s = 1.0, int if_undistore = 0 )
    {
        Eigen::Vector3d point_curr( pi->x, pi->y, pi->z );
        Eigen::Vector3d point_w;

        //if_undistore = 0;
        if ( MOTION_DEBLUR == 0 || if_undistore == 0 || interpolate_s == 1.0 )
        {
            //printf("deb %d %d %f\n", MOTION_DEBLUR, if_undistore, interpolate_s);
            point_w = m_q_w_curr * point_curr + m_t_w_curr;
        }
        else
        {

            if ( interpolate_s > 1.0 || interpolate_s < 0.0 )
            {
                ROS_WARN( "Input interpolate_s = %.5f\r\n", interpolate_s );
                //assert( interpolate_s <= 1.0 && interpolate_s >= 0.0 );
            }

            if ( 1 ) // Using rodrigues for fast compute.
            {
                //https://en.wikipedia.org/wiki/Rodrigues%27_rotation_formula
                Eigen::Vector3d interpolate_T = m_t_w_incre * ( interpolate_s * BLUR_SCALE );//平移分量
                double          interpolate_R_theta = m_interpolatation_theta * interpolate_s;//旋转分量 的 Theta
                Eigen::Matrix<double, 3, 3> interpolate_R_mat;

                // R(Omg)(Theta) = I + Omg * sin(Theta) + Omg*Omg*(1- cos(Theta))
                interpolate_R_mat = Eigen::Matrix3d::Identity() + sin( interpolate_R_theta ) * m_interpolatation_omega_hat + ( 1 - cos( interpolate_R_theta ) ) * m_interpolatation_omega_hat_sq2;
                point_w = m_q_w_last * ( interpolate_R_mat * point_curr + interpolate_T ) + m_t_w_last;
            }
            else
            {
                Eigen::Quaterniond interpolate_q = m_q_I.slerp( interpolate_s * BLUR_SCALE, m_q_w_incre );
                Eigen::Vector3d    interpolate_T = m_t_w_incre * ( interpolate_s * BLUR_SCALE );
                point_w = m_q_w_last * ( interpolate_q * point_curr + interpolate_T ) + m_t_w_last;
            }
        }

        po->x = point_w.x();
        po->y = point_w.y();
        po->z = point_w.z();
        po->intensity = pi->intensity;
        //po->intensity = 1.0;
    }

    void pointAssociateTobeMapped( PointType const *const pi, PointType *const po )
    {
        Eigen::Vector3d point_w( pi->x, pi->y, pi->z );
        Eigen::Vector3d point_curr = m_q_w_curr.inverse() * ( point_w - m_t_w_curr );
        po->x = point_curr.x();
        po->y = point_curr.y();
        po->z = point_curr.z();
        po->intensity = pi->intensity;
    }

    unsigned int pointcloudAssociateToMap( pcl::PointCloud<PointType> const &pc_in, pcl::PointCloud<PointType> &pt_out, int if_undistore = 0 )
    {
        unsigned int points_size = pc_in.points.size();
        pt_out.points.resize( points_size );

        for ( unsigned int i = 0; i < points_size; i++ )
        {
            pointAssociateToMap( &pc_in.points[ i ], &pt_out.points[ i ], pc_in.points[ i ].intensity, if_undistore );
        }

        return points_size;
    }

    unsigned int pointcloudAssociateTbeoMapped( pcl::PointCloud<PointType> const &pc_in, pcl::PointCloud<PointType> &pt_out )
    {
        unsigned int points_size = pc_in.points.size();
        pt_out.points.resize( points_size );

        for ( unsigned int i = 0; i < points_size; i++ )
        {
            pointAssociateTobeMapped( &pc_in.points[ i ], &pt_out.points[ i ] );
        }

        return points_size;
    }

    void laserCloudCornerLastHandler( const sensor_msgs::PointCloud2ConstPtr &laserCloudCornerLast2 )
    {
        std::unique_lock<std::mutex> lock( m_mutex_buf );
        Data_pair *                  data_pair = get_data_pair( laserCloudCornerLast2->header.stamp.toSec() );
        data_pair->add_pc_corner( laserCloudCornerLast2 );
        if ( data_pair->is_completed() )
        {
            m_queue_avail_data.push( data_pair );
        }
    }

    void laserCloudSurfLastHandler( const sensor_msgs::PointCloud2ConstPtr &laserCloudSurfLast2 )
    {
        std::unique_lock<std::mutex> lock( m_mutex_buf );
        Data_pair *                  data_pair = get_data_pair( laserCloudSurfLast2->header.stamp.toSec() );
        data_pair->add_pc_plane( laserCloudSurfLast2 );
        if ( data_pair->is_completed() )
        {
            m_queue_avail_data.push( data_pair );
        }
    }

    void laserCloudFullResHandler( const sensor_msgs::PointCloud2ConstPtr &laserCloudFullRes2 )
    {
        std::unique_lock<std::mutex> lock( m_mutex_buf );
        Data_pair *                  data_pair = get_data_pair( laserCloudFullRes2->header.stamp.toSec() );
        data_pair->add_pc_full( laserCloudFullRes2 );
        if ( data_pair->is_completed() )
        {
            m_queue_avail_data.push( data_pair );
        }
    }

    Eigen::Matrix<double, 3, 1> pcl_pt_to_eigend( PointType &pt )
    {
        return Eigen::Matrix<double, 3, 1>( pt.x, pt.y, pt.z );
    }

    //receive odomtry
    void laserOdometryHandler( const nav_msgs::Odometry::ConstPtr &laserOdometry )
    {
        m_mutex_buf.lock();
        m_odom_que.push( laserOdometry );
        m_mutex_buf.unlock();

        // high frequence publish
        Eigen::Quaterniond q_wodom_curr;
        Eigen::Vector3d    t_wodom_curr;
        q_wodom_curr.x() = laserOdometry->pose.pose.orientation.x;
        q_wodom_curr.y() = laserOdometry->pose.pose.orientation.y;
        q_wodom_curr.z() = laserOdometry->pose.pose.orientation.z;
        q_wodom_curr.w() = laserOdometry->pose.pose.orientation.w;
        t_wodom_curr.x() = laserOdometry->pose.pose.position.x;
        t_wodom_curr.y() = laserOdometry->pose.pose.position.y;
        t_wodom_curr.z() = laserOdometry->pose.pose.position.z;

        Eigen::Quaterniond q_w_curr = Eigen::Quaterniond( 1, 0, 0, 0 );
        Eigen::Vector3d    t_w_curr = Eigen::Vector3d::Zero();

        nav_msgs::Odometry odomAftMapped;
        odomAftMapped.header.frame_id = "/camera_init";
        odomAftMapped.child_frame_id = "/aft_mapped";
        odomAftMapped.header.stamp = laserOdometry->header.stamp;
        odomAftMapped.pose.pose.orientation.x = q_w_curr.x();
        odomAftMapped.pose.pose.orientation.y = q_w_curr.y();
        odomAftMapped.pose.pose.orientation.z = q_w_curr.z();
        odomAftMapped.pose.pose.orientation.w = q_w_curr.w();
        odomAftMapped.pose.pose.position.x = t_w_curr.x();
        odomAftMapped.pose.pose.position.y = t_w_curr.y();
        odomAftMapped.pose.pose.position.z = t_w_curr.z();
        m_pub_odom_aft_mapped_hight_frec.publish( odomAftMapped );
    }

    void find_min_max_intensity( const pcl::PointCloud<PointType>::Ptr pc_ptr, float &min_I, float &max_I )
    {
        int pt_size = pc_ptr->size();
        min_I = 10000;
        max_I = -min_I;
        for ( int i = 0; i < pt_size; i++ )
        {
            min_I = std::min( pc_ptr->points[ i ].intensity, min_I );
            max_I = std::max( pc_ptr->points[ i ].intensity, max_I );
        }
    }

    float refine_blur( float in_blur, const float &min_blur, const float &max_blur )
    {
        return ( in_blur - min_blur ) / ( max_blur - min_blur );
    }

    void reset_incremtal_parameter()
    {
        for ( size_t i = 0; i < 7; i++ )
        {
            m_para_buffer_incremental[ i ] = 0;
        }
        m_para_buffer_incremental[ 3 ] = 1.0;
        m_t_w_incre = m_t_w_incre * 0;
        m_q_w_incre = Eigen::Map<Eigen::Quaterniond>( m_para_buffer_incremental );

        m_interpolatation_theta = 0;
        m_interpolatation_omega_hat.setZero();
        m_interpolatation_omega_hat_sq2.setZero();
    }

    float compute_fov_angle( const PointType &pt )
    {
        float sq_xy = sqrt( std::pow( pt.y / pt.x, 2 ) + std::pow( pt.z / pt.x, 2 ) );
        return atan( sq_xy ) * 57.3;
    }

    void process()
    {
        double first_time_stamp = -1;
        m_last_max_blur = 0.0;
        while ( 1 )
        {
            {

                m_file_logger.printf( "------------------\r\n" );

                while ( m_queue_avail_data.empty() )
                {
                    sleep( 0.0001 );
                }
                m_mutex_buf.lock();
                while ( m_queue_avail_data.size() >= ( unsigned int ) m_max_buffer_size )
                {
                    ROS_WARN( "Drop lidar frame in mapping for real time performance !!!" );
                    ( *m_file_logger.get_ostream() ) << "Drop lidar frame in mapping for real time performance !!!" << endl;
                    m_queue_avail_data.pop();
                }
                Data_pair *current_data_pair = m_queue_avail_data.front();
                m_queue_avail_data.pop();
                m_mutex_buf.unlock();

                m_time_pc_corner_past = current_data_pair->m_pc_corner->header.stamp.toSec();

                if ( first_time_stamp < 0 )
                {
                    first_time_stamp = m_time_pc_corner_past;
                }

                double begin_time_f = ros::Time::now().toSec();

                ( *m_file_logger.get_ostream() ) << "Messgage time stamp = " << m_time_pc_corner_past - first_time_stamp << endl;

                m_laser_cloud_corner_last->clear();
                pcl::fromROSMsg( *( current_data_pair->m_pc_corner ), *m_laser_cloud_corner_last );

                m_laser_cloud_surf_last->clear();
                pcl::fromROSMsg( *( current_data_pair->m_pc_plane ), *m_laser_cloud_surf_last );

                m_laser_cloud_full_res->clear();
                pcl::fromROSMsg( *( current_data_pair->m_pc_full ), *m_laser_cloud_full_res );

                delete current_data_pair;
                float min_t, max_t;
                find_min_max_intensity( m_laser_cloud_full_res, min_t, max_t );
                if ( m_if_save_to_pcd_files && PCD_SAVE_RAW )
                {
                    m_pcl_tools_raw.save_to_pcd_files( "raw", *m_laser_cloud_full_res, 1 );
                }
                m_q_w_last = m_q_w_curr;
                m_t_w_last = m_t_w_curr;
                m_minimum_pt_time_stamp = m_last_time_stamp;
                m_maximum_pt_time_stamp = max_t;
                m_last_time_stamp = max_t;
                reset_incremtal_parameter();//m_para_buffer_incremental， m_q_w_incre ， m_t_w_incre初始化为 0

                //100 * 100 * 100的 CUBE， 每个CUBE长宽高都是 50 米
                //为什么要 加上 m_para_laser_cloud_center_width 这个数值,这是因为计算索引都是正整数，需要统一向右平移50个 CUBE，也即2500米
                int centerCubeI = int( ( m_t_w_curr.x() + CUBE_W / 2 ) / CUBE_W ) + m_para_laser_cloud_center_width;
                int centerCubeJ = int( ( m_t_w_curr.y() + CUBE_H / 2 ) / CUBE_H ) + m_para_laser_cloud_center_height;
                int centerCubeK = int( ( m_t_w_curr.z() + CUBE_D / 2 ) / CUBE_D ) + m_para_laser_cloud_center_depth;

                if ( m_t_w_curr.x() + CUBE_W / 2 < 0 )
                    centerCubeI--;

                if ( m_t_w_curr.y() + CUBE_H / 2 < 0 )
                    centerCubeJ--;

                if ( m_t_w_curr.z() + CUBE_D / 2 < 0 )
                    centerCubeK--;

                //printf( "****** min max timestamp = [%.6f, %.6f] ****** \r\n", m_minimum_pt_time_stamp, m_maximum_pt_time_stamp );
                printf( "****** min max timestamp = [%.6f, %.6f] [%d %d %d]****** \r\n", m_minimum_pt_time_stamp, m_maximum_pt_time_stamp,centerCubeI, centerCubeJ, centerCubeK);

                //这几个循环语句 是作为调整CUBE中心用的， 如果地图增的太大，超出了100 * 100 * 100 CUBE 的范围，那么需要我们将整体CUBE的中心移动一下，删除太老的区域， 添加新的空区域，同时还保证了总体数据量不变
                while ( centerCubeI < 3 )//如果左下角不够用了，需要删除右上角的区域，给左下角用
                {
                    for ( int j = 0; j < m_para_laser_cloud_height; j++ )
                    {
                        for ( int k = 0; k < m_para_laser_cloud_depth; k++ )
                        {
                            int                             i = m_para_laser_cloud_width - 1;
                            pcl::PointCloud<PointType>::Ptr laserCloudCubeCornerPointer =
                                m_laser_cloud_corner_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ];
                            pcl::PointCloud<PointType>::Ptr laserCloudCubeSurfPointer =
                                m_laser_cloud_surface_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ];

                            for ( ; i >= 1; i-- )
                            {
                                m_laser_cloud_corner_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ] =
                                    m_laser_cloud_corner_array[ i - 1 + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ];
                                m_laser_cloud_surface_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ] =
                                    m_laser_cloud_surface_array[ i - 1 + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ];
                            }

                            m_laser_cloud_corner_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ] =
                                laserCloudCubeCornerPointer;
                            m_laser_cloud_surface_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ] =
                                laserCloudCubeSurfPointer;
                            laserCloudCubeCornerPointer->clear();
                            laserCloudCubeSurfPointer->clear();
                        }
                    }

                    centerCubeI++;
                    m_para_laser_cloud_center_width++;
                }

                while ( centerCubeI >= m_para_laser_cloud_width - 3 )//如果右上角不够用了，需要删除左下角的区域，给右上角用
                {
                    for ( int j = 0; j < m_para_laser_cloud_height; j++ )
                    {
                        for ( int k = 0; k < m_para_laser_cloud_depth; k++ )
                        {
                            int                             i = 0;
                            pcl::PointCloud<PointType>::Ptr laserCloudCubeCornerPointer =
                                m_laser_cloud_corner_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ];
                            pcl::PointCloud<PointType>::Ptr laserCloudCubeSurfPointer =
                                m_laser_cloud_surface_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ];

                            for ( ; i < m_para_laser_cloud_width - 1; i++ )
                            {
                                m_laser_cloud_corner_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ] =
                                    m_laser_cloud_corner_array[ i + 1 + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ];
                                m_laser_cloud_surface_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ] =
                                    m_laser_cloud_surface_array[ i + 1 + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ];
                            }

                            m_laser_cloud_corner_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ] =
                                laserCloudCubeCornerPointer;
                            m_laser_cloud_surface_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ] =
                                laserCloudCubeSurfPointer;
                            laserCloudCubeCornerPointer->clear();
                            laserCloudCubeSurfPointer->clear();
                        }
                    }

                    centerCubeI--;
                    m_para_laser_cloud_center_width--;
                }

                while ( centerCubeJ < 3 )//如果Y负向不够用了，需要删除Y正向的区域，给Y负向用
                {
                    for ( int i = 0; i < m_para_laser_cloud_width; i++ )
                    {
                        for ( int k = 0; k < m_para_laser_cloud_depth; k++ )
                        {
                            int                             j = m_para_laser_cloud_height - 1;
                            pcl::PointCloud<PointType>::Ptr laserCloudCubeCornerPointer =
                                m_laser_cloud_corner_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ];
                            pcl::PointCloud<PointType>::Ptr laserCloudCubeSurfPointer =
                                m_laser_cloud_surface_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ];

                            for ( ; j >= 1; j-- )
                            {
                                m_laser_cloud_corner_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ] =
                                    m_laser_cloud_corner_array[ i + m_para_laser_cloud_width * ( j - 1 ) + m_para_laser_cloud_width * m_para_laser_cloud_height * k ];
                                m_laser_cloud_surface_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ] =
                                    m_laser_cloud_surface_array[ i + m_para_laser_cloud_width * ( j - 1 ) + m_para_laser_cloud_width * m_para_laser_cloud_height * k ];
                            }

                            m_laser_cloud_corner_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ] =
                                laserCloudCubeCornerPointer;
                            m_laser_cloud_surface_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ] =
                                laserCloudCubeSurfPointer;
                            laserCloudCubeCornerPointer->clear();
                            laserCloudCubeSurfPointer->clear();
                        }
                    }

                    centerCubeJ++;
                    m_para_laser_cloud_center_height++;
                }

                while ( centerCubeJ >= m_para_laser_cloud_height - 3 )//如果Y正向不够用了，需要删除Y负向的区域，给Y正向用
                {
                    for ( int i = 0; i < m_para_laser_cloud_width; i++ )
                    {
                        for ( int k = 0; k < m_para_laser_cloud_depth; k++ )
                        {
                            int                             j = 0;
                            pcl::PointCloud<PointType>::Ptr laserCloudCubeCornerPointer =
                                m_laser_cloud_corner_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ];
                            pcl::PointCloud<PointType>::Ptr laserCloudCubeSurfPointer =
                                m_laser_cloud_surface_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ];

                            for ( ; j < m_para_laser_cloud_height - 1; j++ )
                            {
                                m_laser_cloud_corner_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ] =
                                    m_laser_cloud_corner_array[ i + m_para_laser_cloud_width * ( j + 1 ) + m_para_laser_cloud_width * m_para_laser_cloud_height * k ];
                                m_laser_cloud_surface_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ] =
                                    m_laser_cloud_surface_array[ i + m_para_laser_cloud_width * ( j + 1 ) + m_para_laser_cloud_width * m_para_laser_cloud_height * k ];
                            }

                            m_laser_cloud_corner_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ] =
                                laserCloudCubeCornerPointer;
                            m_laser_cloud_surface_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ] =
                                laserCloudCubeSurfPointer;
                            laserCloudCubeCornerPointer->clear();
                            laserCloudCubeSurfPointer->clear();
                        }
                    }

                    centerCubeJ--;
                    m_para_laser_cloud_center_height--;
                }

                while ( centerCubeK < 3 )//如果Z负向不够用了，需要删除Z正向的区域，给Z负向用
                {
                    for ( int i = 0; i < m_para_laser_cloud_width; i++ )
                    {
                        for ( int j = 0; j < m_para_laser_cloud_height; j++ )
                        {
                            int                             k = m_para_laser_cloud_depth - 1;
                            pcl::PointCloud<PointType>::Ptr laserCloudCubeCornerPointer =
                                m_laser_cloud_corner_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ];
                            pcl::PointCloud<PointType>::Ptr laserCloudCubeSurfPointer =
                                m_laser_cloud_surface_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ];

                            for ( ; k >= 1; k-- )
                            {
                                m_laser_cloud_corner_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ] =
                                    m_laser_cloud_corner_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * ( k - 1 ) ];
                                m_laser_cloud_surface_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ] =
                                    m_laser_cloud_surface_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * ( k - 1 ) ];
                            }

                            m_laser_cloud_corner_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ] =
                                laserCloudCubeCornerPointer;
                            m_laser_cloud_surface_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ] =
                                laserCloudCubeSurfPointer;
                            laserCloudCubeCornerPointer->clear();
                            laserCloudCubeSurfPointer->clear();
                        }
                    }

                    centerCubeK++;
                    m_para_laser_cloud_center_depth++;
                }

                while ( centerCubeK >= m_para_laser_cloud_depth - 3 )//如果Z正向不够用了，需要删除Z负向的区域，给Z正向用
                {
                    for ( int i = 0; i < m_para_laser_cloud_width; i++ )
                    {
                        for ( int j = 0; j < m_para_laser_cloud_height; j++ )
                        {
                            int                             k = 0;
                            pcl::PointCloud<PointType>::Ptr laserCloudCubeCornerPointer =
                                m_laser_cloud_corner_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ];
                            pcl::PointCloud<PointType>::Ptr laserCloudCubeSurfPointer =
                                m_laser_cloud_surface_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ];

                            for ( ; k < m_para_laser_cloud_depth - 1; k++ )
                            {
                                m_laser_cloud_corner_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ] =
                                    m_laser_cloud_corner_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * ( k + 1 ) ];
                                m_laser_cloud_surface_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ] =
                                    m_laser_cloud_surface_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * ( k + 1 ) ];
                            }

                            m_laser_cloud_corner_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ] =
                                laserCloudCubeCornerPointer;
                            m_laser_cloud_surface_array[ i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k ] =
                                laserCloudCubeSurfPointer;
                            laserCloudCubeCornerPointer->clear();
                            laserCloudCubeSurfPointer->clear();
                        }
                    }

                    centerCubeK--;
                    m_para_laser_cloud_center_depth--;
                }

                int laserCloudValidNum = 0;
                int laserCloudSurroundNum = 0;

                // CUBE(I,J,K)周围 5 x 5 x 3范围的为相邻CUBE
                for ( int i = centerCubeI - 2; i <= centerCubeI + 2; i++ )
                {
                    for ( int j = centerCubeJ - 2; j <= centerCubeJ + 2; j++ )
                    {
                        for ( int k = centerCubeK - 1; k <= centerCubeK + 1; k++ )
                        {
                            if ( i >= 0 && i < m_para_laser_cloud_width &&
                                 j >= 0 && j < m_para_laser_cloud_height &&
                                 k >= 0 && k < m_para_laser_cloud_depth )
                            {
                                m_laser_cloud_valid_Idx[ laserCloudValidNum ] = i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k;
                                laserCloudValidNum++;
                                m_laser_cloud_surround_Idx[ laserCloudSurroundNum ] = i + m_para_laser_cloud_width * j + m_para_laser_cloud_width * m_para_laser_cloud_height * k;
                                laserCloudSurroundNum++;
                            }
                        }
                    }
                }

                //MAP中的角点和平面点,从相邻的cube中取出所有的角点和面点，认为是MAP点
                m_laser_cloud_corner_from_map->clear();
                m_laser_cloud_surf_from_map->clear();

                for ( int i = 0; i < laserCloudValidNum; i++ )
                {
                    *m_laser_cloud_corner_from_map += *m_laser_cloud_corner_array[ m_laser_cloud_valid_Idx[ i ] ];
                    *m_laser_cloud_surf_from_map += *m_laser_cloud_surface_array[ m_laser_cloud_valid_Idx[ i ] ];
                }

                int laserCloudCornerFromMapNum = m_laser_cloud_corner_from_map->points.size();
                int laserCloudSurfFromMapNum = m_laser_cloud_surf_from_map->points.size();

                //对最新数据帧的角点 滤波
                pcl::PointCloud<PointType>::Ptr laserCloudCornerStack( new pcl::PointCloud<PointType>() );
                m_down_sample_filter_corner.setInputCloud( m_laser_cloud_corner_last );
                m_down_sample_filter_corner.filter( *laserCloudCornerStack );
                //laserCloudCornerStack = m_laser_cloud_corner_last;
                int laser_corner_pt_num = laserCloudCornerStack->points.size();


                //对最新数据帧中的平面点 滤波
                pcl::PointCloud<PointType>::Ptr laserCloudSurfStack( new pcl::PointCloud<PointType>() );
                m_down_sample_filter_surface.setInputCloud( m_laser_cloud_surf_last );
                m_down_sample_filter_surface.filter( *laserCloudSurfStack );
                //laserCloudSurfStack = m_laser_cloud_surf_last;
                int laser_surface_pt_num = laserCloudSurfStack->points.size();

                printf( "map corner num %d  surf num %d \n", laserCloudCornerFromMapNum, laserCloudSurfFromMapNum );

                int                    surf_avail_num = 0;
                int                    corner_avail_num = 0;
                ceres::Solver::Summary summary;
                float                  angular_diff = 0;
                float                  t_diff = 0;
                float                  minimize_cost = summary.final_cost;
                PointType              pointOri, pointSel;
                int                    corner_rejection_num = 0;
                int                    surface_rejecetion_num = 0;
                int                    if_undistore_in_matching = 1;


                double map_get_time_f = ros::Time::now().toSec();

                //局部MAP中的角点和平面点数量满足阈值时，计算
                if ( laserCloudCornerFromMapNum > CORNER_MIN_MAP_NUM && laserCloudSurfFromMapNum > SURFACE_MIN_MAP_NUM && frameCount > m_mapping_init_accumulate_frames )
                {
                    m_kdtree_corner_from_map->setInputCloud( m_laser_cloud_corner_from_map );
                    m_kdtree_surf_from_map->setInputCloud( m_laser_cloud_surf_from_map );

                    //ICP最大迭代次数
                    for ( int iterCount = 0; iterCount < m_para_icp_max_iterations; iterCount++ )
                    {
                        corner_avail_num = 0;
                        surf_avail_num = 0;
                        corner_rejection_num = 0;
                        surface_rejecetion_num = 0;

                        ceres::LossFunction *               loss_function = new ceres::HuberLoss( 0.1 );//Huber Loss 是一个用于回归问题的带参损失函数, 优点是能增强平方误差损失函数(MSE, mean square error)对离群点的鲁棒性。
                        ceres::LocalParameterization *      q_parameterization = new ceres::EigenQuaternionParameterization();
                        ceres::Problem::Options             problem_options;
                        ceres::ResidualBlockId              block_id;
                        ceres::Problem                      problem( problem_options );
                        std::vector<ceres::ResidualBlockId> residual_block_ids;

                        problem.AddParameterBlock( m_para_buffer_incremental, 4, q_parameterization );//前四个参数为旋转四元数(R)
                        problem.AddParameterBlock( m_para_buffer_incremental + 4, 3 );//后三个参数为平移参数(T)

                        //计算角点残茶
                        for ( int i = 0; i < laser_corner_pt_num; i++ )
                        {
                            pointOri = laserCloudCornerStack->points[ i ];
                            //通过平移旋转消除 运动失真
                            pointAssociateToMap( &pointOri, &pointSel, pointOri.intensity, 0/*if_undistore_in_matching*/ );//last parameter allways 1

                            #if 1//点的顺序没错，因此时间戳在整帧中归一化之后还是对的
                            static bool printflag_ = true;
                            if(printflag_ && (i == 674))
                            {
                                printf("cornerid:%d ath:%f ele:%f int:%f total:%d\n", i, atan2(pointOri.y, pointOri.x)/3.1416 * 180, atan2(pointOri.z, sqrt(pointOri.x * pointOri.x + pointOri.y * pointOri.y))/3.1416 * 180,
                                       pointOri.intensity, laser_corner_pt_num);
                            }
                            if(i == (laser_corner_pt_num - 1))
                            {
                                printflag_ = false;
                            }
                            #endif
                            //在MAP中寻找5个最近邻点
                            m_kdtree_corner_from_map->nearestKSearch( pointSel, line_search_num, m_point_search_Idx, m_point_search_sq_dis );

                            //最近邻点的距离平方要求小于2
                            if ( m_point_search_sq_dis[ line_search_num - 1 ] < 2.0 )
                            {
                                bool                         line_is_avail = true;
                                std::vector<Eigen::Vector3d> nearCorners;
                                Eigen::Vector3d              center( 0, 0, 0 );
                                if ( /*IF_LINE_FEATURE_CHECK*/ 1 )//根据5个邻近点的特征值判断 这五个近邻点首否近似一条直线
                                {
                                    for ( int j = 0; j < line_search_num; j++ )
                                    {
                                        Eigen::Vector3d tmp( m_laser_cloud_corner_from_map->points[ m_point_search_Idx[ j ] ].x,
                                                             m_laser_cloud_corner_from_map->points[ m_point_search_Idx[ j ] ].y,
                                                             m_laser_cloud_corner_from_map->points[ m_point_search_Idx[ j ] ].z );
                                        center = center + tmp;
                                        nearCorners.push_back( tmp );
                                    }

                                    center = center / ( ( float ) line_search_num );//五个邻近点的重心

                                    Eigen::Matrix3d covMat = Eigen::Matrix3d::Zero();

                                    for ( int j = 0; j < line_search_num; j++ )
                                    {
                                        Eigen::Matrix<double, 3, 1> tmpZeroMean = nearCorners[ j ] - center;//五个邻近点的重心和邻近点组成的向量
                                        covMat = covMat + tmpZeroMean * tmpZeroMean.transpose();//五个向量的协方差矩阵的和
                                    }

                                    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> saes( covMat );//特征值

                                    // if is indeed line feature
                                    // note Eigen library sort eigenvalues in increasing order

                                    if ( saes.eigenvalues()[ 2 ] > 3 * saes.eigenvalues()[ 1 ] )//最大特征值 大于 次大特征值的3倍 则认为是线条
                                    {
                                        line_is_avail = true;
                                    }
                                    else
                                    {
                                        line_is_avail = false;
                                    }
                                }

                                Eigen::Vector3d curr_point( pointOri.x, pointOri.y, pointOri.z );

                                if ( line_is_avail )//近邻点组成了直线
                                {
                                    if ( ICP_LINE )// 1
                                    {
                                        ceres::CostFunction *cost_function;
                                        if ( MOTION_DEBLUR )// 1, 是否运动补偿
                                        {
                                            cost_function = ceres_icp_point2line<double>::Create( curr_point,//原始激光雷达坐标系中的点
                                                                                                  pcl_pt_to_eigend( m_laser_cloud_corner_from_map->points[ m_point_search_Idx[ 0 ] ] ),
                                                                                                  pcl_pt_to_eigend( m_laser_cloud_corner_from_map->points[ m_point_search_Idx[ 1 ] ] ),
                                                                                                  1.0, //pointOri.intensity * 1.0,
                                                                                                  Eigen::Matrix<double, 4, 1>( m_q_w_last.w(), m_q_w_last.x(), m_q_w_last.y(), m_q_w_last.z() ),
                                                                                                  m_t_w_last ); //pointOri.intensity );
                                        }
                                        else
                                        {
                                            cost_function = ceres_icp_point2line<double>::Create( curr_point,
                                                                                                  pcl_pt_to_eigend( m_laser_cloud_corner_from_map->points[ m_point_search_Idx[ 0 ] ] ),
                                                                                                  pcl_pt_to_eigend( m_laser_cloud_corner_from_map->points[ m_point_search_Idx[ 1 ] ] ),
                                                                                                  1.0,
                                                                                                  Eigen::Matrix<double, 4, 1>( m_q_w_last.w(), m_q_w_last.x(), m_q_w_last.y(), m_q_w_last.z() ),
                                                                                                  m_t_w_last );
                                        }
                                        block_id = problem.AddResidualBlock( cost_function, loss_function, m_para_buffer_incremental, m_para_buffer_incremental + 4 );//cost, loss, 初始旋转参数， 初始平移参数
                                        residual_block_ids.push_back( block_id );
                                    }
                                    corner_avail_num++;
                                }
                                else
                                {
                                    corner_rejection_num++;
                                }
                            }
                        }

                        //计算平面点残茶
                        for ( int i = 0; i < laser_surface_pt_num; i++ )
                        {

                            pointOri = laserCloudSurfStack->points[ i ];
                            int planeValid = true;
                            pointAssociateToMap( &pointOri, &pointSel, pointOri.intensity, 0/*if_undistore_in_matching*/ );//last parameter allways 1

                            //5个最近邻平面点
                            m_kdtree_surf_from_map->nearestKSearch( pointSel, plane_search_num, m_point_search_Idx, m_point_search_sq_dis );
                            //最近邻平面点距离平方的阈值为 10m
                            if ( m_point_search_sq_dis[ plane_search_num - 1 ] < 10.0 )
                            {
                                std::vector<Eigen::Vector3d> nearCorners;
                                Eigen::Vector3d              center( 0, 0, 0 );
                                if ( IF_PLANE_FEATURE_CHECK )// 0
                                {
                                    for ( int j = 0; j < plane_search_num; j++ )
                                    {
                                        Eigen::Vector3d tmp( m_laser_cloud_corner_from_map->points[ m_point_search_Idx[ j ] ].x,
                                                             m_laser_cloud_corner_from_map->points[ m_point_search_Idx[ j ] ].y,
                                                             m_laser_cloud_corner_from_map->points[ m_point_search_Idx[ j ] ].z );
                                        center = center + tmp;
                                        nearCorners.push_back( tmp );
                                    }

                                    center = center / ( float ) ( plane_search_num );

                                    Eigen::Matrix3d covMat = Eigen::Matrix3d::Zero();

                                    for ( int j = 0; j < plane_search_num; j++ )
                                    {
                                        Eigen::Matrix<double, 3, 1> tmpZeroMean = nearCorners[ j ] - center;
                                        covMat = covMat + tmpZeroMean * tmpZeroMean.transpose();//协方差矩阵之和
                                    }

                                    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> saes( covMat );

                                    if ( ( saes.eigenvalues()[ 2 ] > 3 * saes.eigenvalues()[ 0 ] ) &&//最大特征值 是 最小特征值的3倍， 并且最大特征值 小于次大特征值的 10 倍
                                         ( saes.eigenvalues()[ 2 ] < 10 * saes.eigenvalues()[ 1 ] ) )
                                    {
                                        planeValid = true;
                                    }
                                    else
                                    {
                                        planeValid = false;
                                    }
                                }

                                Eigen::Vector3d curr_point( pointOri.x, pointOri.y, pointOri.z );

                                if ( planeValid )// 1
                                {
                                    if ( ICP_PLANE )// 1
                                    {
                                        ceres::CostFunction *cost_function;

                                        if ( MOTION_DEBLUR )//1
                                        {
                                            cost_function = ceres_icp_point2plane<double>::Create(
                                                curr_point,
                                                pcl_pt_to_eigend( m_laser_cloud_surf_from_map->points[ m_point_search_Idx[ 0 ] ] ),
                                                pcl_pt_to_eigend( m_laser_cloud_surf_from_map->points[ m_point_search_Idx[ plane_search_num / 2 ] ] ),
                                                pcl_pt_to_eigend( m_laser_cloud_surf_from_map->points[ m_point_search_Idx[ plane_search_num - 1 ] ] ),
                                                1.0, //pointOri.intensity * BLUR_SCALE,
                                                Eigen::Matrix<double, 4, 1>( m_q_w_last.w(), m_q_w_last.x(), m_q_w_last.y(), m_q_w_last.z() ),
                                                m_t_w_last ); //pointOri.intensity );
                                        }
                                        else
                                        {
                                            cost_function = ceres_icp_point2plane<double>::Create(
                                                curr_point,
                                                pcl_pt_to_eigend( m_laser_cloud_surf_from_map->points[ m_point_search_Idx[ 0 ] ] ),
                                                pcl_pt_to_eigend( m_laser_cloud_surf_from_map->points[ m_point_search_Idx[ plane_search_num / 2 ] ] ),
                                                pcl_pt_to_eigend( m_laser_cloud_surf_from_map->points[ m_point_search_Idx[ plane_search_num - 1 ] ] ),
                                                1.0,
                                                Eigen::Matrix<double, 4, 1>( m_q_w_last.w(), m_q_w_last.x(), m_q_w_last.y(), m_q_w_last.z() ),
                                                m_t_w_last );
                                        }
                                        block_id = problem.AddResidualBlock( cost_function, loss_function, m_para_buffer_incremental, m_para_buffer_incremental + 4 );
                                        residual_block_ids.push_back( block_id );
                                    }
                                    surf_avail_num++;
                                }
                                else
                                {
                                    surface_rejecetion_num++;
                                }
                            }
                        }

                        ceres::Solver::Options options;

                        std::vector<ceres::ResidualBlockId> residual_block_ids_bak;
                        residual_block_ids_bak = residual_block_ids;
                        for ( size_t ii = 0; ii < 1; ii++ )
                        {
                            options.linear_solver_type = ceres::DENSE_QR;
                            options.max_num_iterations = m_para_cere_max_iterations;
                            options.max_num_iterations = 5;
                            options.minimizer_progress_to_stdout = false;
                            options.check_gradients = false;
                            //options.gradient_check_relative_precision = 1e-10;
                            //options.function_tolerance = 1e-100; // default 1e-6

                            if ( 0 )
                            {
                                // NOTE Optimize T first and than R
                                if ( iterCount < ( m_para_icp_max_iterations - 2 ) / 2 )
                                    problem.SetParameterBlockConstant( m_para_buffer_incremental + 4 );
                                else if ( iterCount < m_para_icp_max_iterations - 2 )
                                    problem.SetParameterBlockConstant( m_para_buffer_incremental );
                            }

                            set_ceres_solver_bound( problem );//平移限制在相邻两帧数据不超过0.2米(10m/s  /  50Hz)

                            //double bef_solver = ros::Time::now().toSec();
                            ceres::Solve( options, &problem, &summary );
                            //printf("sol1[%f]", ros::Time::now().toSec() - bef_solver);

                            // Remove outliers
                            residual_block_ids_bak.clear();

                            //if ( summary.final_cost > m_max_final_cost * 0.001 )
                            //评估残差点， 删除残差较大的点（移除残差绝对值之和 大于 std::min( 0.1, 10 * avr_cost ) 的点）
                            if ( 1 )
                            {
                                ceres::Problem::EvaluateOptions eval_options;
                                eval_options.residual_blocks = residual_block_ids;
                                double         total_cost = 0.0;
                                double         avr_cost;
                                vector<double> residuals;
                                problem.Evaluate( eval_options, &total_cost, &residuals, nullptr, nullptr );
                                avr_cost = total_cost / residual_block_ids.size();//平均cost值

                                for ( unsigned int i = 0; i < residual_block_ids.size(); i++ )
                                {
                                    if ( ( fabs( residuals[ 3 * i + 0 ] ) + fabs( residuals[ 3 * i + 1 ] ) + fabs( residuals[ 3 * i + 2 ] ) ) > std::min( 0.1, 10 * avr_cost ) ) // std::min( 1.0, 10 * avr_cost )
                                    {
                                        problem.RemoveResidualBlock( residual_block_ids[ i ] );
                                    }
                                    else
                                    {
                                        residual_block_ids_bak.push_back( residual_block_ids[ i ] );
                                    }
                                }
                            }

                            residual_block_ids = residual_block_ids_bak;
                        }
                        options.max_num_iterations = m_para_cere_max_iterations;//5
                        set_ceres_solver_bound( problem );// 平移限制在相邻两帧数据不超过0.2米(10m/s  /  50Hz)

                        //double bef_solver_2 = ros::Time::now().toSec();
                        ceres::Solve( options, &problem, &summary );
                        //printf("sol2[%f]", ros::Time::now().toSec() - bef_solver_2);

                        if ( MOTION_DEBLUR )
                        {
                            //compute_interpolatation_rodrigue( m_q_w_incre, m_interpolatation_omega, m_interpolatation_theta, m_interpolatation_omega_hat );
                            //m_interpolatation_omega_hat_sq2 = m_interpolatation_omega_hat * m_interpolatation_omega_hat;
                        }
                        m_t_w_curr = m_q_w_last * m_t_w_incre + m_t_w_last;//MAP坐标系中的平移, m_q_w_las将在下一帧数据开始迭代之前置为 m_q_w_curr
                        m_q_w_curr = m_q_w_last * m_q_w_incre;//MAP坐标系中的旋转，m_t_w_last 将在下一帧数据开始迭代之前置为 m_t_w_curr

                        // *( g_file_logger.get_ostream() ) << "Res: " << summary.BriefReport() << endl;

                        angular_diff = ( float ) m_q_w_curr.angularDistance( m_q_w_last ) * 57.3;//57.3 is degree/rad
                        t_diff = ( m_t_w_curr - m_t_w_last ).norm();
                        minimize_cost = summary.final_cost;
                    }

                    printf( "===== corner factor num %d , surf factor num %d=====\n", corner_avail_num, surf_avail_num );

                    if ( laser_corner_pt_num != 0 && laser_surface_pt_num != 0 )
                    {
                        m_file_logger.printf( "Corner  total num %d |  use %d | rate = %d \% \r\n", laser_corner_pt_num, corner_avail_num, ( corner_avail_num ) *100 / laser_corner_pt_num );
                        m_file_logger.printf( "Surface total num %d |  use %d | rate = %d \% \r\n", laser_surface_pt_num, surf_avail_num, ( surf_avail_num ) *100 / laser_surface_pt_num );
                    }

                    *( m_file_logger.get_ostream() ) << summary.BriefReport() << endl;
                    //*( m_file_logger.get_ostream() ) << m_q_w_incre.toRotationMatrix().eulerAngles( 0, 1, 2 ).transpose() * 57.3 << endl;
                    //*( m_file_logger.get_ostream() ) << m_t_w_incre.transpose() << endl;
                    *( m_file_logger.get_ostream() ) << "Last R:" << m_q_w_last.toRotationMatrix().eulerAngles( 0, 1, 2 ).transpose() * 57.3 << " ,T = " << m_t_w_last.transpose() << endl;
                    *( m_file_logger.get_ostream() ) << "Curr R:" << m_q_w_curr.toRotationMatrix().eulerAngles( 0, 1, 2 ).transpose() * 57.3 << " ,T = " << m_t_w_curr.transpose() << endl;
                    //*(g_file_logger.get_ostream()) << summary.FullReport() << endl;
                    *( m_file_logger.get_ostream() ) << "Full pointcloud size: " << m_laser_cloud_full_res->points.size() << endl;

                    m_file_logger.printf( "Motion blur = %d | ", MOTION_DEBLUR );
                    m_file_logger.printf( "Cost = %.2f| blk_size = %d | corner_num = %d | surf_num = %d | angle dis = %.2f | T dis = %.2f \r\n",
                                          minimize_cost, summary.num_residual_blocks, corner_avail_num, surf_avail_num, angular_diff, t_diff );

                    //计算值不合理，不采用
                    if ( angular_diff > m_para_max_angular_rate || minimize_cost > m_max_final_cost )
                    {
                        *( m_file_logger.get_ostream() ) << "**** Reject update **** " << endl;
                        *( m_file_logger.get_ostream() ) << summary.FullReport() << endl;
                        for ( int i = 0; i < 7; i++ )
                        {
                            m_para_buffer_RT[ i ] = m_para_buffer_RT_last[ i ];
                        }

                        m_last_time_stamp = m_minimum_pt_time_stamp;
                        m_q_w_curr = m_q_w_last;
                        m_t_w_curr = m_t_w_last;
                        continue;
                    }
                }
                else
                {
                    ROS_WARN( "time Map corner and surf num are not enough" );
                }

                double iterator_end_time_f = ros::Time::now().toSec();

                if ( 1/*!PUB_DEBUG_INFO*/ )
                {
                    pcl::PointCloud<PointType> pc_feature_pub_corners, pc_feature_pub_surface;
                    sensor_msgs::PointCloud2   laserCloudMsg;

                    pointcloudAssociateToMap( *m_laser_cloud_surf_last, pc_feature_pub_surface, 0/*g_if_undistore*/ );
                    pcl::toROSMsg( pc_feature_pub_surface, laserCloudMsg );
                    laserCloudMsg.header.stamp = ros::Time().fromSec( m_time_odom );
                    laserCloudMsg.header.frame_id = "/camera_init";
                    m_pub_last_surface_pts.publish( laserCloudMsg );
                    pointcloudAssociateToMap( *m_laser_cloud_corner_last, pc_feature_pub_corners, 0/*g_if_undistore*/ );
                    pcl::toROSMsg( pc_feature_pub_corners, laserCloudMsg );
                    laserCloudMsg.header.stamp = ros::Time().fromSec( m_time_odom );
                    laserCloudMsg.header.frame_id = "/camera_init";
                    m_pub_last_corner_pts.publish( laserCloudMsg );//feature corners
                }

                //对每个角点计算点的cube 编号，然后将点放入 cube中
                for ( int i = 0; i < laser_corner_pt_num; i++ )
                {
                    //if ( MOTION_DEBLUR && ( laserCloudSurfStack->points[ i ].intensity < m_para_min_match_blur ) )
                    //*( m_file_logger.get_ostream() ) << __FILE__ << " --- " << __LINE__ << endl;
                    pointAssociateToMap( &laserCloudCornerStack->points[ i ], &pointSel, laserCloudCornerStack->points[ i ].intensity, 0/*g_if_undistore*/ );

                    int cubeI = int( ( pointSel.x + CUBE_W / 2 ) / CUBE_W ) + m_para_laser_cloud_center_width;
                    int cubeJ = int( ( pointSel.y + CUBE_H / 2 ) / CUBE_H ) + m_para_laser_cloud_center_height;
                    int cubeK = int( ( pointSel.z + CUBE_D / 2 ) / CUBE_D ) + m_para_laser_cloud_center_depth;

                    if ( pointSel.x + CUBE_W / 2 < 0 )
                        cubeI--;

                    if ( pointSel.y + CUBE_H / 2 < 0 )
                        cubeJ--;

                    if ( pointSel.z + CUBE_D / 2 < 0 )
                        cubeK--;

                    if ( cubeI >= 0 && cubeI < m_para_laser_cloud_width &&
                         cubeJ >= 0 && cubeJ < m_para_laser_cloud_height &&
                         cubeK >= 0 && cubeK < m_para_laser_cloud_depth )
                    {
                        int cubeInd = cubeI + m_para_laser_cloud_width * cubeJ + m_para_laser_cloud_width * m_para_laser_cloud_height * cubeK;
                        m_laser_cloud_corner_array[ cubeInd ]->push_back( pointSel );
                    }
                }

                //对每个平面点计算点的cube 编号，然后将点放入 cube中
                for ( int i = 0; i < laser_surface_pt_num; i++ )
                {
                    //*( m_file_logger.get_ostream() ) << __FILE__ << " --- " << __LINE__ << endl;
                    pointAssociateToMap( &laserCloudSurfStack->points[ i ], &pointSel, laserCloudSurfStack->points[ i ].intensity, 0/*g_if_undistore*/);

                    int cubeI = int( ( pointSel.x + CUBE_W / 2 ) / CUBE_W ) + m_para_laser_cloud_center_width;
                    int cubeJ = int( ( pointSel.y + CUBE_H / 2 ) / CUBE_H ) + m_para_laser_cloud_center_height;
                    int cubeK = int( ( pointSel.z + CUBE_D / 2 ) / CUBE_D ) + m_para_laser_cloud_center_depth;

                    if ( pointSel.x + CUBE_W / 2 < 0 )
                        cubeI--;

                    if ( pointSel.y + CUBE_H / 2 < 0 )
                        cubeJ--;

                    if ( pointSel.z + CUBE_D / 2 < 0 )
                        cubeK--;

                    if ( cubeI >= 0 && cubeI < m_para_laser_cloud_width &&
                         cubeJ >= 0 && cubeJ < m_para_laser_cloud_height &&
                         cubeK >= 0 && cubeK < m_para_laser_cloud_depth )
                    {
                        int cubeInd = cubeI + m_para_laser_cloud_width * cubeJ + m_para_laser_cloud_width * m_para_laser_cloud_height * cubeK;
                        m_laser_cloud_surface_array[ cubeInd ]->push_back( pointSel );
                    }
                }

                //对每一个邻近点 cube 降采样
                for ( int i = 0; i < laserCloudValidNum; i++ )
                {
                    int ind = m_laser_cloud_valid_Idx[ i ];

                    pcl::PointCloud<PointType>::Ptr tmpCorner( new pcl::PointCloud<PointType>() );
                    // m_filter_k_means.setInputCloud( m_laser_cloud_corner_array[ ind ]);
                    // m_filter_k_means.filter( *tmpCorner);
                    // m_down_sample_filter_corner.setInputCloud( tmpCorner );
                    m_down_sample_filter_corner.setInputCloud( m_laser_cloud_corner_array[ ind ] );
                    m_down_sample_filter_corner.filter( *tmpCorner );
                    m_laser_cloud_corner_array[ ind ] = tmpCorner;

                    pcl::PointCloud<PointType>::Ptr tmpSurf( new pcl::PointCloud<PointType>() );
                    // m_filter_k_means.setInputCloud( m_laser_cloud_surface_array[ ind ] );
                    // m_filter_k_means.filter( *tmpSurf);
                    // m_down_sample_filter_surface.setInputCloud(tmpSurf );
                    m_down_sample_filter_surface.setInputCloud( m_laser_cloud_surface_array[ ind ] );
                    m_down_sample_filter_surface.filter( *tmpSurf );
                    m_laser_cloud_surface_array[ ind ] = tmpSurf;
                }

                double coner_surface_tomap_time_f = ros::Time::now().toSec();

                //publish surround map for every 5 frame
                if ( /*PUB_SURROUND_PTS*/1 )
                {
                    if ( frameCount % 500 == 0 )
                    {
                        m_laser_cloud_surround->clear();

                        for ( int i = 0; i < laserCloudSurroundNum; i++ )
                        {
                            int ind = m_laser_cloud_surround_Idx[ i ];
                            *m_laser_cloud_surround += *m_laser_cloud_corner_array[ ind ];
                            *m_laser_cloud_surround += *m_laser_cloud_surface_array[ ind ];
                        }

                        sensor_msgs::PointCloud2 laserCloudSurround3;
                        pcl::toROSMsg( *m_laser_cloud_surround, laserCloudSurround3 );
                        laserCloudSurround3.header.stamp = ros::Time().fromSec( m_time_odom );
                        laserCloudSurround3.header.frame_id = "/camera_init";
                        m_pub_laser_cloud_surround.publish( laserCloudSurround3 );

                        if ( m_if_save_to_pcd_files )
                        {
                            m_pcl_tools_aftmap.save_to_pcd_files( "surround", *m_laser_cloud_surround );
                        }
                    }

                    if ( frameCount % 20 == 0 )
                    {
                        pcl::PointCloud<PointType> laserCloudMap;

                        for ( int i = 0; i < 4851; i++ )
                        {
                            laserCloudMap += *m_laser_cloud_corner_array[ i ];
                            laserCloudMap += *m_laser_cloud_surface_array[ i ];
                        }

                        sensor_msgs::PointCloud2 laserCloudMsg;
                        pcl::toROSMsg( laserCloudMap, laserCloudMsg );
                        laserCloudMsg.header.stamp = ros::Time().fromSec( m_time_odom );
                        laserCloudMsg.header.frame_id = "/camera_init";
                        m_pub_laser_cloud_map.publish( laserCloudMsg );
                        m_file_logger.printf("publish lasermappoints %d\n", laserCloudMap.size());
                    }
                }

                //配准到全局坐标系之后发布出去
                int laserCloudFullResNum = m_laser_cloud_full_res->points.size();

                compute_interpolatation_rodrigue( m_q_w_incre, m_interpolatation_omega, m_interpolatation_theta, m_interpolatation_omega_hat );
                m_interpolatation_omega_hat_sq2 = m_interpolatation_omega_hat * m_interpolatation_omega_hat;

                static bool print_once = true;
                static FILE *ptest = NULL;
                //插值计算每个点的偏移数量
                for ( int i = 0; i < laserCloudFullResNum; i++ )
                {
                    if(print_once)
                    {
                        //if(ptest)
                          //  fprintf(ptest, );
                        float angle = atan2( m_laser_cloud_full_res->points[ i ].y, m_laser_cloud_full_res->points[ i ].x );
                        angle = angle * 180 / 3.1416;
                        m_file_logger.printf("%d %f %f\n", i, angle, m_laser_cloud_full_res->points[ i ].intensity);
                    }
                    pointAssociateToMap( &m_laser_cloud_full_res->points[ i ], &m_laser_cloud_full_res->points[ i ], m_laser_cloud_full_res->points[ i ].intensity, 1 );
                }
                print_once = false;
                //printf
                #if 0
                static int printflag = true;
                FILE *p = NULL;
                if(printflag)
                {
                    p = fopen("/home/cpf/outtest.txt", "w");
                }
                if(printflag && p)
                {
                    fprintf(p, "total:%d\n", laserCloudFullResNum);
                    for(int i = 0; i < laserCloudFullResNum;i++)
                        fprintf(p, "%d %f %f\n",i,  m_laser_cloud_full_res->points[ i ].intensity, ((i % 10000) * 3 + (i / 10000))  / 29999.0);
                }
                if(printflag)
                {
                    printflag = false;
                    fclose(p);
                }
                #endif
                //endprint

                pcl::UniformSampling<PointType> filter;
                filter.setInputCloud(m_laser_cloud_full_res);
                filter.setRadiusSearch(m_map_downsample_para);
                filter.filter(*m_laser_cloud_full_res);
                printf("after fileter %d\n", m_laser_cloud_full_res->points.size());

                sensor_msgs::PointCloud2 laserCloudFullRes3;
                pcl::toROSMsg( *m_laser_cloud_full_res, laserCloudFullRes3 );
                laserCloudFullRes3.header.stamp = ros::Time().fromSec( m_time_odom );
                laserCloudFullRes3.header.frame_id = "/camera_init";
                m_pub_laser_cloud_full_res.publish( laserCloudFullRes3 ); //single_frame_with_pose_tranfromed

                if ( m_if_save_to_pcd_files )
                {
                    m_pcl_tools_aftmap.save_to_pcd_files( "aft_mapp", *m_laser_cloud_full_res, 1 );
                }

                double full_registered_time_f = ros::Time::now().toSec();


                //时间为 零， ？？？
                nav_msgs::Odometry odomAftMapped;
                odomAftMapped.header.frame_id = "/camera_init";
                odomAftMapped.child_frame_id = "/aft_mapped";
                //odomAftMapped.header.stamp = ros::Time().fromSec( m_time_odom );
                odomAftMapped.header.stamp = ros::Time::now();
                if ( 1 )
                {
                    odomAftMapped.pose.pose.orientation.x = m_q_w_curr.x();
                    odomAftMapped.pose.pose.orientation.y = m_q_w_curr.y();
                    odomAftMapped.pose.pose.orientation.z = m_q_w_curr.z();
                    odomAftMapped.pose.pose.orientation.w = m_q_w_curr.w();

                    odomAftMapped.pose.pose.position.x = m_t_w_curr.x();
                    odomAftMapped.pose.pose.position.y = m_t_w_curr.y();
                    odomAftMapped.pose.pose.position.z = m_t_w_curr.z();
                }
                else
                {
                    Eigen::Quaterniond q_s_half, q_pub;
                    Eigen::Vector3d    t_s_half, t_pub;
                    t_s_half = m_t_w_incre * 0.5;
                    q_s_half = m_q_I.slerp( 0.5, m_q_w_incre );

                    t_pub = m_q_w_last * t_s_half + m_t_w_last;
                    q_pub = m_q_w_last * q_s_half;
                    odomAftMapped.pose.pose.orientation.x = q_pub.x();
                    odomAftMapped.pose.pose.orientation.y = q_pub.y();
                    odomAftMapped.pose.pose.orientation.z = q_pub.z();
                    odomAftMapped.pose.pose.orientation.w = q_pub.w();

                    odomAftMapped.pose.pose.position.x = t_pub.x();
                    odomAftMapped.pose.pose.position.y = t_pub.y();
                    odomAftMapped.pose.pose.position.z = t_pub.z();
                }
                m_pub_odom_aft_mapped.publish( odomAftMapped ); // name: Odometry aft_mapped_to_init

                geometry_msgs::PoseStamped laserAfterMappedPose;
                laserAfterMappedPose.header = odomAftMapped.header;
                laserAfterMappedPose.pose = odomAftMapped.pose.pose;
                m_laser_after_mapped_path.header.stamp = odomAftMapped.header.stamp;
                m_laser_after_mapped_path.header.frame_id = "/camera_init";
                m_laser_after_mapped_path.poses.push_back( laserAfterMappedPose );
                m_pub_laser_aft_mapped_path.publish( m_laser_after_mapped_path );

                static tf::TransformBroadcaster br;
                tf::Transform                   transform;
                tf::Quaternion                  q;
                transform.setOrigin( tf::Vector3( m_t_w_curr( 0 ),
                                                  m_t_w_curr( 1 ),
                                                  m_t_w_curr( 2 ) ) );
                q.setW( m_q_w_curr.w() );
                q.setX( m_q_w_curr.x() );
                q.setY( m_q_w_curr.y() );
                q.setZ( m_q_w_curr.z() );
                transform.setRotation( q );
                br.sendTransform( tf::StampedTransform( transform, odomAftMapped.header.stamp, "/camera_init", "/aft_mapped" ) );

                double end_time_f = ros::Time::now().toSec();

                printf("beg_cube:[%f] [%f] [%f] [%f]\n", map_get_time_f - begin_time_f, iterator_end_time_f- map_get_time_f, coner_surface_tomap_time_f-iterator_end_time_f,
                       full_registered_time_f - coner_surface_tomap_time_f);
                frameCount++;
            }
            std::chrono::nanoseconds dura( 1 );
            std::this_thread::sleep_for( dura );
        }
    }
};

#endif // LASER_MAPPING_HPP
