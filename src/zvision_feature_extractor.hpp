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

#ifndef ZVISION_LASER_SCAN_HANDLER_HPP
#define ZVISION_LASER_SCAN_HANDLER_HPP

#include <cmath>
#include <vector>

#define USE_HASH 1
#define SHOW_OPENCV_VIS 0
#if USE_HASH
#include <unordered_map>
#endif

#include <Eigen/Eigen>
#include <Eigen/Eigen>
#include <nav_msgs/Odometry.h>

#include <pcl/filters/voxel_grid.h>
#include <pcl/kdtree/kdtree_flann.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl_conversions/pcl_conversions.h>
#include <ros/ros.h>
#include <sensor_msgs/Imu.h>
#include <sensor_msgs/PointCloud2.h>
#include <tf/transform_broadcaster.h>
#include <tf/transform_datatypes.h>

#include "tools/angle.h"
#include "eigen_math.hpp"
#include "tools/common.h"
#include "tools/pcl_tools.hpp"

#define PCL_DATA_SAVE_DIR "/home/ziv/data/loam_pc"

#define IF_LIVOX_HANDLER_REMOVE 0
#define IF_APPEND 0
#define printf_line printf( " %s %d \r\n", __FILE__, __LINE__ );

using namespace std;
using namespace PCL_TOOLS;

class Zvision_laser
{
  public:
    string SOFT_WARE_VERSION = string( "V_0.1_beta" );

    enum E_point_type
    {
        e_pt_normal = 0,                      // normal points
        e_pt_000 = 0x0001 << 0,               // points [0,0,0]
        e_pt_too_near = 0x0001 << 1,          // points in short range
        e_pt_reflectivity_low = 0x0001 << 2,  // low reflectivity
        e_pt_reflectivity_high = 0x0001 << 3, // high reflectivity
        e_pt_circle_edge = 0x0001 << 4,       // points near the edge of circle
        e_pt_nan = 0x0001 << 5,               // points with infinite value
        e_pt_small_view_angle = 0x0001 << 6,  // points with large viewed angle
    };

    enum E_feature_type // if and only if normal point can be labeled
    {
        e_label_invalid = -1,
        e_label_unlabeled = 0,
        e_label_corner = 0x0001 << 0,
        e_label_surface = 0x0001 << 1,
        e_label_near_nan = 0x0001 << 2,
        e_label_near_zero = 0x0001 << 3,
        e_label_hight_intensity = 0x0001 << 4,
    };

    // Encode point infos using points intensity, which is more convenient for debugging.
    enum E_intensity_type
    {
        e_I_raw = 0,
        e_I_motion_blur,
        e_I_motion_mix,
        e_I_sigma,
        e_I_scan_angle,
        e_I_curvature,
        e_I_view_angle,
        e_I_time_stamp
    };

    struct Pt_infos
    {
        int   pt_type = e_pt_normal;
        int   pt_label = e_label_unlabeled;
        int   idx = 0.f;
        float raw_intensity = 0.f;
        float time_stamp = 0.0;
        float polar_angle = 0.f;
        int   polar_direction = 0;
        float polar_dis_sq2 = 0.f;
        float depth_sq2 = 0.f;
        float curvature = 0.0;
        float view_angle = 0.0;
        float sigma = 0.0;
        Eigen::Matrix< float, 2, 1 > pt_2d_img; // project to X==1 plane
    };

    // E_intensity_type   default_return_intensity_type = e_I_raw;
    E_intensity_type default_return_intensity_type = e_I_motion_blur;
    // E_intensity_type default_return_intensity_type = e_I_scan_angle;
    // E_intensity_type default_return_intensity_type = e_I_curvature;
    // E_intensity_type default_return_intensity_type = e_I_view_angle;

    int   pcl_data_save_index = 0;

    float max_fov = 17; // Edge of circle to main axis
    float m_max_edge_polar_pos = 0;
    float m_time_internal_pts = 1.0e-5; // 10us = 1e-5
    float m_cx = 0;
    float m_cy = 0;
    int   m_if_save_pcd_file = 0;
    int   m_input_points_size;
    double m_first_receive_time = -1;
    double m_current_time;
    double m_last_maximum_time_stamp;
    float thr_corner_curvature = 0.05;
    float thr_surface_curvature = 0.01;
    float minimum_view_angle = 10;
    std::vector< Pt_infos >  m_pts_info_vec;
    std::vector< PointType > m_raw_pts_vec;
#if USE_HASH
    std::unordered_map< PointType, Pt_infos *, Pt_hasher, Pt_compare >           m_map_pt_idx; // using hash_map
    std::unordered_map< PointType, Pt_infos *, Pt_hasher, Pt_compare >::iterator m_map_pt_idx_it;
#else
    std::map< PointType, Pt_infos *, Pt_compare >           m_map_pt_idx;
    std::map< PointType, Pt_infos *, Pt_compare >::iterator m_map_pt_idx_it;
#endif

    float m_zvision_min_allow_dis = 1.0;
    float m_zvision_min_sigma = 7e-3;
    float m_zvision_max_allow_dis = 15.0;

    std::vector< pcl::PointCloud< pcl::PointXYZI > > m_last_laser_scan;

    int     m_img_width = 800;
    int     m_img_heigh = 800;

    ~Zvision_laser() {}

    Zvision_laser()
    {
        // Some data init
        cout << "========= Hello, this is livox laser ========" << endl;
        cout << "Compile time:  " << __TIME__ << endl;
        cout << "Softward version: " << SOFT_WARE_VERSION << endl;
        cout << "========= End ========" << endl;

        m_max_edge_polar_pos = std::pow( tan( max_fov / 57.3 ) * 1, 2 );
    }

    template < typename T >
    T dis2_xy( T x, T y )
    {
        return x * x + y * y;
    }

    template < typename T >
    T depth2_xyz( T x, T y, T z )
    {
        return x * x + y * y + z * z;
    }

    template < typename T >
    T depth_xyz( T x, T y, T z )
    {
        return sqrt( depth2_xyz( x, y, z ) );
    }

    template < typename T >
    Pt_infos *find_pt_info( const T &pt )
    {
        m_map_pt_idx_it = m_map_pt_idx.find( pt );
        //printf( "Input pt is [%lf, %lf, %lf]\r\n", pt.x, pt.y, pt.z );
        if ( m_map_pt_idx_it == m_map_pt_idx.end() )
        {
            printf( "Input pt is [%lf, %lf, %lf]\r\n", pt.x, pt.y, pt.z );
            printf( "Error!!!!\r\n" );
            assert( m_map_pt_idx_it != m_map_pt_idx.end() ); // else, there must be something error happened before.
        }
        return m_map_pt_idx_it->second;
    }

    void get_features_zvision( pcl::PointCloud< PointType > &pc_corners, pcl::PointCloud< PointType > &pc_surface, pcl::PointCloud< PointType > &pc_full_res, int laser)
    {
        int corner_num = 0;
        int surface_num = 0;
        int full_num = 0;
        pc_corners.resize( m_pts_info_vec.size() );
        pc_surface.resize( m_pts_info_vec.size() );
        pc_full_res.resize( m_pts_info_vec.size() );
        int begin_idx = laser * 10000;
        int end_idx = begin_idx + 10000 - 1;
        if(255 == laser)
        {
            begin_idx = 0;
            end_idx = 29999;
        }

        //printf("vecsize : %d\n", m_pts_info_vec.size());
        int pt_critical_rm_mask = e_pt_000 | e_pt_nan;
        for ( size_t i = 0; i < m_pts_info_vec.size(); i++ )
        {
            if ( m_pts_info_vec[ i ].idx > end_idx ||
                 m_pts_info_vec[ i ].idx < begin_idx )
                continue;

            int point_id = m_pts_info_vec[ i ].idx;

            if ( ( m_pts_info_vec[ i ].pt_type & pt_critical_rm_mask ) == 0 )
            {

                #if 1
                if ( m_pts_info_vec[ i ].pt_label & e_label_corner )
                {
                    if ( m_pts_info_vec[ i ].pt_type != e_pt_normal )
                        continue;
                    if ( m_pts_info_vec[ i ].depth_sq2 < std::pow( 30, 2 ) )
                    {
                        pc_corners.points[ corner_num ] = m_raw_pts_vec[ i ];
                        //set_intensity( pc_corners.points[ corner_num ], e_I_motion_blur );
                        //pc_corners.points[ corner_num ].intensity = m_pts_info_vec[ i ].time_stamp;
                        pc_corners.points[ corner_num ].intensity = ((point_id % 10000) * 3 + (point_id / 10000)) / 29999.0;//强度阈 为点的ID除以点的总数，便于计算点的时间点
#if 0
                        if(((674 == corner_num)) || (18551 == point_id))
                            {
                            printf("printf:%d %d %f\n", corner_num, point_id, pc_corners.points[ corner_num ].intensity);
                            //printf("printf:%d %d %f\n", corner_num,18551, pc_corners.points[ corner_num ].intensity);
                        }
#endif
                        corner_num++;

                    }
                }
                if ( m_pts_info_vec[ i ].pt_label & e_label_surface )
                {
                    if ( m_pts_info_vec[ i ].depth_sq2 < std::pow( 1000, 2 ) )
                    {
                        pc_surface.points[ surface_num ] = m_raw_pts_vec[ i ];
                        //pc_surface.points[ surface_num ].intensity = float(m_pts_info_vec[ i ].time_stamp);
                        pc_surface.points[ surface_num ].intensity = ((point_id % 10000) * 3 + (point_id / 10000))  / 29999.0;
                        //set_intensity( pc_surface.points[ surface_num ], e_I_motion_blur );
                        surface_num++;
                    }
                }

                pc_full_res.points[ full_num ] = m_raw_pts_vec[ i ];
                //pc_full_res.points[ full_num ].intensity = m_pts_info_vec[ i ].time_stamp;
                pc_full_res.points[ full_num ].intensity = ((point_id % 10000) * 3 + (point_id / 10000)) / 29999.0;
                full_num++;
                #endif
            }
        }

        //printf("Get_features , corner num = %d, suface num = %d, blur from %.2f~%.2f\r\n", corner_num, surface_num, minimum_blur, maximum_blur);
        pc_corners.resize(corner_num);
        pc_surface.resize(surface_num);
        pc_full_res.resize(full_num);
        printf("printf:conersize:%d surfsize:%d\n", corner_num, surface_num);
    }


    void get_features( pcl::PointCloud< PointType > &pc_corners, pcl::PointCloud< PointType > &pc_surface, pcl::PointCloud< PointType > &pc_full_res, float minimum_blur = 0.0, float maximum_blur = 0.3 )
    {
        int corner_num = 0;
        int surface_num = 0;
        int full_num = 0;
        pc_corners.resize( m_pts_info_vec.size() );
        pc_surface.resize( m_pts_info_vec.size() );
        pc_full_res.resize( m_pts_info_vec.size() );
        float maximum_idx = maximum_blur * m_pts_info_vec.size();
        float minimum_idx = minimum_blur * m_pts_info_vec.size();
        int pt_critical_rm_mask = e_pt_000 | e_pt_nan;
        for ( size_t i = 0; i < m_pts_info_vec.size(); i++ )
        {
            if ( m_pts_info_vec[ i ].idx > maximum_idx ||
                 m_pts_info_vec[ i ].idx < minimum_idx )
                continue;

            if ( ( m_pts_info_vec[ i ].pt_type & pt_critical_rm_mask ) == 0 )
            {
                if ( m_pts_info_vec[ i ].pt_label & e_label_corner )
                {
                    if ( m_pts_info_vec[ i ].pt_type != e_pt_normal )
                        continue;
                    if ( m_pts_info_vec[ i ].depth_sq2 < std::pow( 30, 2 ) )
                    {
                        pc_corners.points[ corner_num ] = m_raw_pts_vec[ i ];
                        //set_intensity( pc_corners.points[ corner_num ], e_I_motion_blur );
                        pc_corners.points[ corner_num ].intensity = m_pts_info_vec[ i ].time_stamp;
                        corner_num++;
                    }
                }
                if ( m_pts_info_vec[ i ].pt_label & e_label_surface )
                {
                    if ( m_pts_info_vec[ i ].depth_sq2 < std::pow( 1000, 2 ) )
                    {
                        pc_surface.points[ surface_num ] = m_raw_pts_vec[ i ];
                        pc_surface.points[ surface_num ].intensity = float(m_pts_info_vec[ i ].time_stamp);
                        //set_intensity( pc_surface.points[ surface_num ], e_I_motion_blur );
                        surface_num++;
                    }
                }

                pc_full_res.points[ full_num ] = m_raw_pts_vec[ i ];
                pc_full_res.points[ full_num ].intensity = m_pts_info_vec[ i ].time_stamp;
                full_num++;
            }
        }

        //printf("Get_features , corner num = %d, suface num = %d, blur from %.2f~%.2f\r\n", corner_num, surface_num, minimum_blur, maximum_blur);
        pc_corners.resize(corner_num);
        pc_surface.resize(surface_num);
        pc_full_res.resize(full_num);
    }

    template < typename T >
    void set_intensity( T &pt, const E_intensity_type &i_type = e_I_motion_blur )
    {
        Pt_infos *pt_info = find_pt_info( pt );
        switch ( i_type )
        {
        case ( e_I_raw ):
            pt.intensity = pt_info->raw_intensity;
            break;
        case ( e_I_motion_blur ):
            pt.intensity = ( ( float ) pt_info->idx ) / ( float ) m_input_points_size;
            assert( pt.intensity <= 1.0 && pt.intensity >= 0.0 );
            break;
        case ( e_I_motion_mix ):
            pt.intensity = 0.1 * ( ( float ) pt_info->idx + 1 ) / ( float ) m_input_points_size + ( int ) ( pt_info->raw_intensity );
            break;
        case ( e_I_scan_angle ):
            pt.intensity = pt_info->polar_angle;
            break;
        case ( e_I_curvature ):
            pt.intensity = pt_info->curvature;
            break;
        case ( e_I_view_angle ):
            pt.intensity = pt_info->view_angle;
            break;
        case (e_I_time_stamp):
            pt.intensity = pt_info->time_stamp;
        default:
            pt.intensity = ( ( float ) pt_info->idx + 1 ) / ( float ) m_input_points_size;
        }
        return;
    }

    template < typename T >
    cv::Mat draw_dbg_img( cv::Mat &img, std::vector< T > &pt_list_eigen, cv::Scalar color = cv::Scalar::all( 255 ), int radius = 3 )
    {
        cv::Mat      res_img = img.clone();
        unsigned int pt_size = pt_list_eigen.size();

        for ( unsigned int idx = 0; idx < pt_size; idx++ )
        {
            draw_pt( res_img, pt_list_eigen[ idx ], color, radius );
        }

        return res_img;
    }

    void add_mask_of_point( Pt_infos *pt_infos, const E_point_type &pt_type, int neighbor_count = 0 )
    {

        int idx = pt_infos->idx;
        pt_infos->pt_type |= pt_type;

        if ( neighbor_count > 0 )
        {
            for ( int i = -neighbor_count; i < neighbor_count; i++ )
            {
                idx = pt_infos->idx + i;

                if ( i != 0 && ( idx >= 0 ) && ( idx < ( int ) m_pts_info_vec.size() ) )
                {
                    //cout << "Add mask, id  = " << idx << "  type = " << pt_type << endl;
                    m_pts_info_vec[ idx ].pt_type |= pt_type;
                }
            }
        }
    }

    void eval_point_zvision( Pt_infos *pt_info )
    {
        if ( pt_info->depth_sq2 < m_zvision_min_allow_dis * m_zvision_min_allow_dis ) // to close
        {
            //cout << "Add mask, id  = " << idx << "  type = e_too_near" << endl;
            add_mask_of_point( pt_info, e_pt_too_near );
        }

        if ( pt_info->depth_sq2 > m_zvision_max_allow_dis * m_zvision_max_allow_dis ) // to close
        {
            //cout << "Add mask, id  = " << idx << "  type = e_too_near" << endl;
            //printf("#############too far\n");
            add_mask_of_point( pt_info, e_pt_too_near );
        }

        //pt_info->sigma = pt_info->raw_intensity / pt_info->polar_dis_sq2;

        //if ( pt_info->sigma < m_zvision_min_sigma )
        {
            //cout << "Add mask, id  = " << idx << "  type = e_reflectivity_low" << endl;
            //add_mask_of_point( pt_info, e_pt_reflectivity_low );
        }
    }

    void eval_point( Pt_infos *pt_info )
    {
        if ( pt_info->depth_sq2 < m_zvision_min_allow_dis * m_zvision_min_allow_dis ) // to close
        {
            //cout << "Add mask, id  = " << idx << "  type = e_too_near" << endl;
            add_mask_of_point( pt_info, e_pt_too_near );
        }

        pt_info->sigma = pt_info->raw_intensity / pt_info->polar_dis_sq2;

        if ( pt_info->sigma < m_zvision_min_sigma )
        {
            //cout << "Add mask, id  = " << idx << "  type = e_reflectivity_low" << endl;
            add_mask_of_point( pt_info, e_pt_reflectivity_low );
        }
    }

    // compute curvature and view angle
    void compute_features_zvision()
    {
        unsigned int pts_size = m_raw_pts_vec.size();
        size_t       curvature_ssd_size = 2;//使用5个点计算曲率，所以前两个点是没办法计算的
        int          critical_rm_point = e_pt_000 | e_pt_nan;
        float        neighbor_accumulate_xyz[ 3 ] = { 0.0, 0.0, 0.0 };

        //printf("coner: %f\n", thr_corner_curvature);
        //30000个点的顺序，0-9999为 Laser 0, 10000-19999为 Laser 1， 20000-29999为 Laser 2
        //cout << "Surface_thr = " << thr_surface_curvature << " , corner_thr = " << thr_corner_curvature<< " ,minimum_view_angle = " << minimum_view_angle << endl;

        for(int laser = 0;laser < 3;laser++)
        {
            int begin = 10000 * laser;
            for(int group = curvature_ssd_size; group < 10000 - curvature_ssd_size; group++)
            {
                int idx = begin + group;
                if ( m_pts_info_vec[ idx ].pt_type & critical_rm_point )//无效点或者是0点都不计算
                {
                    continue;
                }

#if 1
                int group_id = group;
                int laser_id = laser;
                if((group_id >= 2) || (group <= 29997))
                {//每条线条的左右两侧两个点 无法计算曲率
                    double pre2_ath = angle_data[7 * (group_id - 2) +laser_id + 1];
                    double pre1_ath = angle_data[7 * (group_id - 1) + laser_id + 1];
                    double curr_ath = angle_data[7 * (group_id) + laser_id + 1];
                    double aft1_ath = angle_data[7 * (group_id + 1) + laser_id + 1];
                    double aft2_ath = angle_data[7 * (group_id + 2) + laser_id + 1];

                    double delta_0 = pre1_ath - pre2_ath;
                    double delta_1 = curr_ath - pre1_ath;
                    double delta_2 = aft1_ath - curr_ath;
                    double delta_3 = aft2_ath - aft1_ath;

                    if(((delta_0 > 0.00001) && (delta_1 > 0.00001) &&  (delta_2 > 0.00001) && (delta_3 > 0.00001))
                        ||((delta_0 < 0.00001) && (delta_1 < 0.00001) &&  (delta_2 < 0.00001) && (delta_3 < 0.00001))
                            )//不是边界点
                    {
                    }
                    else
                    {
                        continue;
                    }
                }

                /*********** Compute curvate ************/
                neighbor_accumulate_xyz[ 0 ] = 0.0;
                neighbor_accumulate_xyz[ 1 ] = 0.0;
                neighbor_accumulate_xyz[ 2 ] = 0.0;

                for ( size_t i = 1; i <= curvature_ssd_size; i++ )
                {
                    if ( ( m_pts_info_vec[ idx + i ].pt_type & e_pt_000 ) || ( m_pts_info_vec[ idx - i ].pt_type & e_pt_000 ) )//前后相邻的点是0点
                    {
                        if ( i == 1 )//邻近 1的点
                        {
                            m_pts_info_vec[ idx ].pt_label |= e_label_near_zero;
                        }
                        else//邻近二的点
                        {
                            m_pts_info_vec[ idx ].pt_label = e_label_invalid;
                        }
                        break;
                    }
                    else if ( ( m_pts_info_vec[ idx + i ].pt_type & e_pt_nan ) || ( m_pts_info_vec[ idx - i ].pt_type & e_pt_nan ) )//前后相邻的点是无效点
                    {
                        if ( i == 1 )//邻近 1的点
                        {
                            m_pts_info_vec[ idx ].pt_label |= e_label_near_nan;
                        }
                        else//邻近 2的点
                        {
                            m_pts_info_vec[ idx ].pt_label = e_label_invalid;
                        }
                        break;
                    }
                    else//邻近 1的点
                    {
                            neighbor_accumulate_xyz[ 0 ] += m_raw_pts_vec[ idx + i ].x + m_raw_pts_vec[ idx - i ].x;//前后相邻2个点都是有效点
                            neighbor_accumulate_xyz[ 1 ] += m_raw_pts_vec[ idx + i ].y + m_raw_pts_vec[ idx - i ].y;
                            neighbor_accumulate_xyz[ 2 ] += m_raw_pts_vec[ idx + i ].z + m_raw_pts_vec[ idx - i ].z;
                    }
                }

                if(m_pts_info_vec[ idx ].pt_label == e_label_invalid)//无法计算曲率
                {
                    continue;
                }

                //对于可以计算曲率的点计算其曲率
                neighbor_accumulate_xyz[ 0 ] -= curvature_ssd_size * 2 * m_raw_pts_vec[ idx ].x;
                neighbor_accumulate_xyz[ 1 ] -= curvature_ssd_size * 2 * m_raw_pts_vec[ idx ].y;
                neighbor_accumulate_xyz[ 2 ] -= curvature_ssd_size * 2 * m_raw_pts_vec[ idx ].z;
                m_pts_info_vec[ idx ].curvature = neighbor_accumulate_xyz[ 0 ] * neighbor_accumulate_xyz[ 0 ] + neighbor_accumulate_xyz[ 1 ] * neighbor_accumulate_xyz[ 1 ] +
                                                  neighbor_accumulate_xyz[ 2 ] * neighbor_accumulate_xyz[ 2 ];

                /*********** Compute plane angle ************/
                Eigen::Matrix< float, 3, 1 > vec_a( m_raw_pts_vec[ idx ].x, m_raw_pts_vec[ idx ].y, m_raw_pts_vec[ idx ].z );
                Eigen::Matrix< float, 3, 1 > vec_b( m_raw_pts_vec[ idx + curvature_ssd_size ].x - m_raw_pts_vec[ idx - curvature_ssd_size ].x,
                                                    m_raw_pts_vec[ idx + curvature_ssd_size ].y - m_raw_pts_vec[ idx - curvature_ssd_size ].y,
                                                    m_raw_pts_vec[ idx + curvature_ssd_size ].z - m_raw_pts_vec[ idx - curvature_ssd_size ].z );
                m_pts_info_vec[ idx ].view_angle = Eigen_math::vector_angle( vec_a  , vec_b, 1 ) * 57.3;

                //点和平面的夹角过小，小于10度
                //printf( "Idx = %d, angle = %.2f\r\n", idx,  m_pts_info_vec[ idx ].view_angle );
                if ( m_pts_info_vec[ idx ].view_angle > minimum_view_angle )//
                {

                    if( m_pts_info_vec[ idx ].curvature < thr_surface_curvature )//曲率小于0.01的点为平面点
                    {
                        m_pts_info_vec[ idx ].pt_label |= e_label_surface;
                    }

                    float sq2_diff = 0.1;

                    if ( m_pts_info_vec[ idx ].curvature > thr_corner_curvature )//曲率大于0.05的点为角点
                    // if ( abs( m_pts_info_vec[ idx ].view_angle - m_pts_info_vec[ idx + curvature_ssd_size ].view_angle ) > edge_angle_diff ||
                    //      abs( m_pts_info_vec[ idx ].view_angle - m_pts_info_vec[ idx - curvature_ssd_size ].view_angle ) > edge_angle_diff )
                    {
                        if ( (m_pts_info_vec[ idx ].depth_sq2 <= m_pts_info_vec[ idx - curvature_ssd_size ].depth_sq2 && //这样的点 \/
                             m_pts_info_vec[ idx ].depth_sq2 <= m_pts_info_vec[ idx + curvature_ssd_size ].depth_sq2)
                             #if 0
                             ||
                                                         (m_pts_info_vec[ idx ].depth_sq2 >= m_pts_info_vec[ idx - curvature_ssd_size ].depth_sq2 && //这样的点 /\..
                                                                                      m_pts_info_vec[ idx ].depth_sq2 >= m_pts_info_vec[ idx + curvature_ssd_size ].depth_sq2)
                             #endif
                             )
                        {
                            //距离平方与左右两侧差两个点位的点的距离平方不超过0.1
                            if ( abs( m_pts_info_vec[ idx ].depth_sq2 - m_pts_info_vec[ idx - curvature_ssd_size ].depth_sq2 ) < sq2_diff * m_pts_info_vec[ idx ].depth_sq2 ||//距离平方与左右两侧差两个点位的点
                                 abs( m_pts_info_vec[ idx ].depth_sq2 - m_pts_info_vec[ idx + curvature_ssd_size ].depth_sq2 ) < sq2_diff * m_pts_info_vec[ idx ].depth_sq2 )
                                m_pts_info_vec[ idx ].pt_label |= e_label_corner;
                        }
                    }
                }
                #endif

            }
        }
    }

    // compute curvature and view angle
    void compute_features()
    {
        unsigned int pts_size = m_raw_pts_vec.size();
        size_t       curvature_ssd_size = 2;//使用5个点计算曲率，所以前两个点是没办法计算的
        int          critical_rm_point = e_pt_000 | e_pt_nan;
        float        neighbor_accumulate_xyz[ 3 ] = { 0.0, 0.0, 0.0 };

        //30000个点的顺序，0-9999为 Laser 0, 10000-19999为 Laser 1， 20000-29999为 Laser 2
        //cout << "Surface_thr = " << thr_surface_curvature << " , corner_thr = " << thr_corner_curvature<< " ,minimum_view_angle = " << minimum_view_angle << endl;

        for ( size_t idx = curvature_ssd_size; idx < pts_size - curvature_ssd_size; idx++ )//计算曲率
        {
            if ( m_pts_info_vec[ idx ].pt_type & critical_rm_point )//无效点或者是0点都不计算
            {
                continue;
            }

            /*********** Compute curvate ************/
            neighbor_accumulate_xyz[ 0 ] = 0.0;
            neighbor_accumulate_xyz[ 1 ] = 0.0;
            neighbor_accumulate_xyz[ 2 ] = 0.0;

            for ( size_t i = 1; i <= curvature_ssd_size; i++ )
            {
                if ( ( m_pts_info_vec[ idx + i ].pt_type & e_pt_000 ) || ( m_pts_info_vec[ idx - i ].pt_type & e_pt_000 ) )//前后相邻的点是0点
                {
                    if ( i == 1 )//邻近 1的点
                    {
                        m_pts_info_vec[ idx ].pt_label |= e_label_near_zero;
                    }
                    else//邻近二的点
                    {
                        m_pts_info_vec[ idx ].pt_label = e_label_invalid;
                    }
                    break;
                }
                else if ( ( m_pts_info_vec[ idx + i ].pt_type & e_pt_nan ) || ( m_pts_info_vec[ idx - i ].pt_type & e_pt_nan ) )//前后相邻的点是无效点
                {
                    if ( i == 1 )//邻近 1的点
                    {
                        m_pts_info_vec[ idx ].pt_label |= e_label_near_nan;
                    }
                    else//邻近 2的点
                    {
                        m_pts_info_vec[ idx ].pt_label = e_label_invalid;
                    }
                    break;
                }
                else//邻近 1的点
                {
                    neighbor_accumulate_xyz[ 0 ] += m_raw_pts_vec[ idx + i ].x + m_raw_pts_vec[ idx - i ].x;//前后相邻2个点都是有效点
                    neighbor_accumulate_xyz[ 1 ] += m_raw_pts_vec[ idx + i ].y + m_raw_pts_vec[ idx - i ].y;
                    neighbor_accumulate_xyz[ 2 ] += m_raw_pts_vec[ idx + i ].z + m_raw_pts_vec[ idx - i ].z;
                }
            }

            if(m_pts_info_vec[ idx ].pt_label == e_label_invalid)//无法计算曲率
            {
                continue;
            }

            //对于可以计算曲率的点计算其曲率
            neighbor_accumulate_xyz[ 0 ] -= curvature_ssd_size * 2 * m_raw_pts_vec[ idx ].x;
            neighbor_accumulate_xyz[ 1 ] -= curvature_ssd_size * 2 * m_raw_pts_vec[ idx ].y;
            neighbor_accumulate_xyz[ 2 ] -= curvature_ssd_size * 2 * m_raw_pts_vec[ idx ].z;
            m_pts_info_vec[ idx ].curvature = neighbor_accumulate_xyz[ 0 ] * neighbor_accumulate_xyz[ 0 ] + neighbor_accumulate_xyz[ 1 ] * neighbor_accumulate_xyz[ 1 ] +
                                              neighbor_accumulate_xyz[ 2 ] * neighbor_accumulate_xyz[ 2 ];

            /*********** Compute plane angle ************/
            Eigen::Matrix< float, 3, 1 > vec_a( m_raw_pts_vec[ idx ].x, m_raw_pts_vec[ idx ].y, m_raw_pts_vec[ idx ].z );
            Eigen::Matrix< float, 3, 1 > vec_b( m_raw_pts_vec[ idx + curvature_ssd_size ].x - m_raw_pts_vec[ idx - curvature_ssd_size ].x,
                                                m_raw_pts_vec[ idx + curvature_ssd_size ].y - m_raw_pts_vec[ idx - curvature_ssd_size ].y,
                                                m_raw_pts_vec[ idx + curvature_ssd_size ].z - m_raw_pts_vec[ idx - curvature_ssd_size ].z );
            m_pts_info_vec[ idx ].view_angle = Eigen_math::vector_angle( vec_a  , vec_b, 1 ) * 57.3;

            //点和平面的夹角过小，小于10度
            //printf( "Idx = %d, angle = %.2f\r\n", idx,  m_pts_info_vec[ idx ].view_angle );
            if ( m_pts_info_vec[ idx ].view_angle > minimum_view_angle )//
            {

                if( m_pts_info_vec[ idx ].curvature < thr_surface_curvature )//曲率小于0.01的点为平面点
                {
                    m_pts_info_vec[ idx ].pt_label |= e_label_surface;
                }

                float sq2_diff = 0.1;

                if ( m_pts_info_vec[ idx ].curvature > thr_corner_curvature )//曲率大于0.05的点为角点
                // if ( abs( m_pts_info_vec[ idx ].view_angle - m_pts_info_vec[ idx + curvature_ssd_size ].view_angle ) > edge_angle_diff ||
                //      abs( m_pts_info_vec[ idx ].view_angle - m_pts_info_vec[ idx - curvature_ssd_size ].view_angle ) > edge_angle_diff )
                {
                    if ( m_pts_info_vec[ idx ].depth_sq2 <= m_pts_info_vec[ idx - curvature_ssd_size ].depth_sq2 && //这样的点 \/
                         m_pts_info_vec[ idx ].depth_sq2 <= m_pts_info_vec[ idx + curvature_ssd_size ].depth_sq2 )
                    {
                        //距离平方与左右两侧差两个点位的点的距离平方不超过0.1
                        if ( abs( m_pts_info_vec[ idx ].depth_sq2 - m_pts_info_vec[ idx - curvature_ssd_size ].depth_sq2 ) < sq2_diff * m_pts_info_vec[ idx ].depth_sq2 ||//距离平方与左右两侧差两个点位的点
                             abs( m_pts_info_vec[ idx ].depth_sq2 - m_pts_info_vec[ idx + curvature_ssd_size ].depth_sq2 ) < sq2_diff * m_pts_info_vec[ idx ].depth_sq2 )
                            m_pts_info_vec[ idx ].pt_label |= e_label_corner;
                    }
                }
            }
        }
    }
    template < typename T >
    int projection_scan_3d_2d_zvision( pcl::PointCloud< T > &laserCloudIn )
    {

        unsigned int pts_size = laserCloudIn.size();
        m_pts_info_vec.clear();// point info
        m_pts_info_vec.resize( pts_size );
        m_raw_pts_vec.resize( pts_size );// original points
        m_map_pt_idx.clear();//point and it's point info
        m_map_pt_idx.reserve( pts_size );

        m_input_points_size = 0;
        for ( unsigned int idx = 0; idx < pts_size; idx++ )//every point
        {
            m_raw_pts_vec[ idx ] = laserCloudIn.points[ idx ];
            Pt_infos *pt_info = &m_pts_info_vec[ idx ];
            m_map_pt_idx.insert( std::make_pair( laserCloudIn.points[ idx ], pt_info ) );
            pt_info->raw_intensity = laserCloudIn.points[ idx ].intensity;
            pt_info->idx = idx;
            //pt_info->time_stamp = m_current_time + ( ( float ) idx ) * m_time_internal_pts;//time stamp
            pt_info->time_stamp = m_current_time + (idx % 10000) * 0.000005 + (idx / 10000) * 0.0000016;//time stamp
            m_last_maximum_time_stamp = pt_info->time_stamp;
            m_input_points_size++;

            if ( !std::isfinite( laserCloudIn.points[ idx ].x ) ||
                 !std::isfinite( laserCloudIn.points[ idx ].y ) ||
                 !std::isfinite( laserCloudIn.points[ idx ].z ) )
            {
                add_mask_of_point( pt_info, e_pt_nan );//无效点
                continue;
            }

            if ( laserCloudIn.points[ idx ].y == 0 )
            {
                add_mask_of_point( pt_info, e_pt_000 );
                continue;
            }

            m_map_pt_idx.insert( std::make_pair( laserCloudIn.points[ idx ], pt_info ) );

            //x*x + y * y + z * z
            pt_info->depth_sq2 = depth2_xyz( laserCloudIn.points[ idx ].x, laserCloudIn.points[ idx ].y, laserCloudIn.points[ idx ].z );

            //cout << "eval_point: d = " << pts_depth[ idx ] << " , " << laserCloudIn.points[ idx ].x << " , " << laserCloudIn.points[ idx ].y << " , " << laserCloudIn.points[ idx ].z << endl;
            //pt_info->pt_2d_img << laserCloudIn.points[ idx ].y / laserCloudIn.points[ idx ].x, laserCloudIn.points[ idx ].z / laserCloudIn.points[ idx ].x;
            //pt_info->polar_dis_sq2 = dis2_xy( pt_info->pt_2d_img( 0 ), pt_info->pt_2d_img( 1 ) );// (y*y + z*z) / (x*x)

            //打上标签，距离过短
            eval_point_zvision( pt_info );

            //扫描视场的边界点，扫瞄线斜率变化过快的点, geometry means current and laser center's angle k
            if ( pt_info->polar_dis_sq2 > m_max_edge_polar_pos )
            {
                //add_mask_of_point( pt_info, e_pt_circle_edge, 2 );
            }
        }
        return 0;
    }

    template < typename T >
    int projection_scan_3d_2d( pcl::PointCloud< T > &laserCloudIn, std::vector< float > &scan_id_index )
    {

        unsigned int pts_size = laserCloudIn.size();
        m_pts_info_vec.clear();// point info
        m_pts_info_vec.resize( pts_size );
        m_raw_pts_vec.resize( pts_size );// original points
        std::vector< int > edge_idx;
        std::vector< int > split_idx;
        scan_id_index.resize( pts_size );// scan id
        edge_idx.clear();
        m_map_pt_idx.clear();//point and it's point info
        m_map_pt_idx.reserve( pts_size );
        std::vector< int > zero_idx;

        m_input_points_size = 0;
        for ( unsigned int idx = 0; idx < pts_size; idx++ )//every point
        {
            m_raw_pts_vec[ idx ] = laserCloudIn.points[ idx ];
            Pt_infos *pt_info = &m_pts_info_vec[ idx ];
            m_map_pt_idx.insert( std::make_pair( laserCloudIn.points[ idx ], pt_info ) );
            pt_info->raw_intensity = laserCloudIn.points[ idx ].intensity;
            pt_info->idx = idx;
            pt_info->time_stamp = m_current_time + ( ( float ) idx ) * m_time_internal_pts;//time stamp
            m_last_maximum_time_stamp = pt_info->time_stamp;
            m_input_points_size++;

            if ( !std::isfinite( laserCloudIn.points[ idx ].x ) ||
                 !std::isfinite( laserCloudIn.points[ idx ].y ) ||
                 !std::isfinite( laserCloudIn.points[ idx ].z ) )
            {
                add_mask_of_point( pt_info, e_pt_nan );//无效点
                continue;
            }

            if ( laserCloudIn.points[ idx ].x == 0 )
            {
                if ( idx == 0 )//第一个点为什么一定不能为0,是因为要计算下面的 laserCloudIn.points[ idx ].y / laserCloudIn.points[ idx ].x吗，
                {
                    // TODO: handle this case.
                    std::cout << "First point should be normal!!!" << std::endl;
                    return 0;
                }
                else
                {
                    pt_info->pt_2d_img = m_pts_info_vec[ idx - 1 ].pt_2d_img;//
                    pt_info->polar_dis_sq2 = m_pts_info_vec[ idx - 1 ].polar_dis_sq2;
                    //cout << "Add mask, id  = " << idx << "  type = e_point_000" << endl;
                    add_mask_of_point( pt_info, e_pt_000 );
                    continue;
                }
            }

            m_map_pt_idx.insert( std::make_pair( laserCloudIn.points[ idx ], pt_info ) );

            //x*x + y * y + z * z
            pt_info->depth_sq2 = depth2_xyz( laserCloudIn.points[ idx ].x, laserCloudIn.points[ idx ].y, laserCloudIn.points[ idx ].z );

            //cout << "eval_point: d = " << pts_depth[ idx ] << " , " << laserCloudIn.points[ idx ].x << " , " << laserCloudIn.points[ idx ].y << " , " << laserCloudIn.points[ idx ].z << endl;
            pt_info->pt_2d_img << laserCloudIn.points[ idx ].y / laserCloudIn.points[ idx ].x, laserCloudIn.points[ idx ].z / laserCloudIn.points[ idx ].x;
            pt_info->polar_dis_sq2 = dis2_xy( pt_info->pt_2d_img( 0 ), pt_info->pt_2d_img( 1 ) );// (y*y + z*z) / (x*x)

            //打上标签，距离过短或者反射率过小
            eval_point( pt_info );

            //扫描视场的边界点，扫瞄线斜率变化过快的点, geometry means current and laser center's angle k
            if ( pt_info->polar_dis_sq2 > m_max_edge_polar_pos )
            {
                add_mask_of_point( pt_info, e_pt_circle_edge, 2 );
            }

            // Split scans
            if ( idx >= 1 )
            {
                float dis_incre = pt_info->polar_dis_sq2 - m_pts_info_vec[ idx - 1 ].polar_dis_sq2;

                if ( dis_incre > 0 ) // far away from zero
                {
                    pt_info->polar_direction = 1;
                }

                if ( dis_incre < 0 ) // move toward zero
                {
                    pt_info->polar_direction = -1;
                }

                if ( pt_info->polar_direction == -1 && m_pts_info_vec[ idx - 1 ].polar_direction == 1 )/*这样的点 /\0 */
                {
                    if ( edge_idx.size() == 0 || ( idx - split_idx[ split_idx.size() - 1 ] ) > 50 )
                    {
                        split_idx.push_back( idx );
                        edge_idx.push_back( idx );// outline
                        continue;
                    }
                }

                if ( pt_info->polar_direction == 1 && m_pts_info_vec[ idx - 1 ].polar_direction == -1 )/* /这样的点 \/ */
                {
                    if ( zero_idx.size() == 0 || ( idx - split_idx[ split_idx.size() - 1 ] ) > 50 )// 8's 1/4 is 50 points ?
                    {
                        split_idx.push_back( idx );

                        zero_idx.push_back( idx );//zero
                        continue;
                    }
                }
            }
        }

        split_idx.push_back( pts_size - 1 );

        int   val_index = 0;
        int   pt_angle_index = 0;
        float scan_angle = 0;
        int   internal_size = 0;

        for ( int idx = 0; idx < ( int ) pts_size; idx++ )
        {
            if ( idx == 0 || idx > split_idx[ val_index + 1 ] )
            {
                if ( idx > split_idx[ val_index + 1 ] )
                {
                    val_index++;
                }

                internal_size = split_idx[ val_index + 1 ] - split_idx[ val_index ];//分割后的 1/4 8字形 中有几个点

                if ( m_pts_info_vec[ split_idx[ val_index + 1 ] ].polar_dis_sq2 > 10000 )//斜率大于100
                {
                    pt_angle_index = split_idx[ val_index + 1 ] - ( int ) ( internal_size * 0.20 );
                    scan_angle = atan2( m_pts_info_vec[ pt_angle_index ].pt_2d_img( 1 ), m_pts_info_vec[ pt_angle_index ].pt_2d_img( 0 ) ) * 57.3;
                    scan_angle = scan_angle + 180.0;
                    //pt_angle_index = split_idx[ val_index + 1 ] - 10;
                    printf("#############bigger than 10000############################################\n");
                }
                else
                {
                    pt_angle_index = split_idx[ val_index + 1 ] - ( int ) ( internal_size * 0.80 );
                    scan_angle = atan2( m_pts_info_vec[ pt_angle_index ].pt_2d_img( 1 ), m_pts_info_vec[ pt_angle_index ].pt_2d_img( 0 ) ) * 57.3;
                    scan_angle = scan_angle + 180.0;
                    //pt_angle_index = split_idx[ val_index ] + 10;
                }

                //cout << "Idx  = " << idx <<  " val = "<< val_index << "  angle = " << scan_angle << endl;
            }
            m_pts_info_vec[ idx ].polar_angle = scan_angle;
            scan_id_index[ idx ] = scan_angle;
        }

        //cout << "===== " << zero_idx.size() << "  " << edge_idx.size() << "  " << split_idx.size() << "=====" << endl;

        return split_idx.size() - 1;
    }

    template < typename T >
    int points_split_to_lines( pcl::PointCloud< T > &laserCloudIn, std::vector< float > &scan_id_index )
    {

        unsigned int pts_size = laserCloudIn.size();
        m_pts_info_vec.clear();
        m_pts_info_vec.resize( pts_size );
        m_raw_pts_vec.resize( pts_size );
        std::vector< int > edge_idx;
        std::vector< int > split_idx;
        scan_id_index.resize( pts_size );
        edge_idx.clear();
        m_map_pt_idx.clear();
        m_map_pt_idx.reserve( pts_size );
        std::vector< int > zero_idx;

        m_input_points_size = 0;
        for ( unsigned int idx = 0; idx < pts_size; idx++ )
        {
            m_raw_pts_vec[ idx ] = laserCloudIn.points[ idx ];
            Pt_infos *pt_info = &m_pts_info_vec[ idx ];
            m_map_pt_idx.insert( std::make_pair( laserCloudIn.points[ idx ], pt_info ) );
            pt_info->raw_intensity = laserCloudIn.points[ idx ].intensity;
            pt_info->idx = idx;
            pt_info->time_stamp = m_current_time + ( ( float ) idx ) * m_time_internal_pts;
            m_last_maximum_time_stamp = pt_info->time_stamp;
            m_input_points_size++;

            if ( !std::isfinite( laserCloudIn.points[ idx ].x ) ||
                 !std::isfinite( laserCloudIn.points[ idx ].y ) ||
                 !std::isfinite( laserCloudIn.points[ idx ].z ) )
            {
                add_mask_of_point( pt_info, e_pt_nan );//无效点
                continue;
            }

            if ( laserCloudIn.points[ idx ].x == 0 )
            {
                if ( idx == 0 )//第一个点为什么一定不能为0,是因为要计算下面的 laserCloudIn.points[ idx ].y / laserCloudIn.points[ idx ].x吗，
                {
                    // TODO: handle this case.
                    std::cout << "First point should be normal!!!" << std::endl;
                    return 0;
                }
                else
                {
                    pt_info->pt_2d_img = m_pts_info_vec[ idx - 1 ].pt_2d_img;
                    pt_info->polar_dis_sq2 = m_pts_info_vec[ idx - 1 ].polar_dis_sq2;
                    //cout << "Add mask, id  = " << idx << "  type = e_point_000" << endl;
                    add_mask_of_point( pt_info, e_pt_000 );
                    continue;
                }
            }

            m_map_pt_idx.insert( std::make_pair( laserCloudIn.points[ idx ], pt_info ) );

            //x*x + y * y + z * z
            pt_info->depth_sq2 = depth2_xyz( laserCloudIn.points[ idx ].x, laserCloudIn.points[ idx ].y, laserCloudIn.points[ idx ].z );

            //cout << "eval_point: d = " << pts_depth[ idx ] << " , " << laserCloudIn.points[ idx ].x << " , " << laserCloudIn.points[ idx ].y << " , " << laserCloudIn.points[ idx ].z << endl;
            pt_info->pt_2d_img << laserCloudIn.points[ idx ].y / laserCloudIn.points[ idx ].x, laserCloudIn.points[ idx ].z / laserCloudIn.points[ idx ].x;
            pt_info->polar_dis_sq2 = dis2_xy( pt_info->pt_2d_img( 0 ), pt_info->pt_2d_img( 1 ) );

            //打上标签，距离过短或者反射率过小
            eval_point( pt_info );

            //扫描视场的边界点，扫瞄线斜率变化过快的点
            if ( pt_info->polar_dis_sq2 > m_max_edge_polar_pos )
            {
                add_mask_of_point( pt_info, e_pt_circle_edge, 2 );
            }

            // Split scans
            if ( idx >= 1 )
            {
                float dis_incre = pt_info->polar_dis_sq2 - m_pts_info_vec[ idx - 1 ].polar_dis_sq2;

                if ( dis_incre > 0 ) // far away from zero
                {
                    pt_info->polar_direction = 1;
                }

                if ( dis_incre < 0 ) // move toward zero
                {
                    pt_info->polar_direction = -1;
                }

                if ( pt_info->polar_direction == -1 && m_pts_info_vec[ idx - 1 ].polar_direction == 1 )/*这样的点 /\0 */
                {
                    if ( edge_idx.size() == 0 || ( idx - split_idx[ split_idx.size() - 1 ] ) > 50 )
                    {
                        split_idx.push_back( idx );
                        edge_idx.push_back( idx );
                        continue;
                    }
                }

                if ( pt_info->polar_direction == 1 && m_pts_info_vec[ idx - 1 ].polar_direction == -1 )/* /这样的点 \/ */
                {
                    if ( zero_idx.size() == 0 || ( idx - split_idx[ split_idx.size() - 1 ] ) > 50 )
                    {
                        split_idx.push_back( idx );

                        zero_idx.push_back( idx );
                        continue;
                    }
                }
            }
        }

        split_idx.push_back( pts_size - 1 );

        int   val_index = 0;
        int   pt_angle_index = 0;
        float scan_angle = 0;
        int   internal_size = 0;

        for ( int idx = 0; idx < ( int ) pts_size; idx++ )
        {
            if ( idx == 0 || idx > split_idx[ val_index + 1 ] )
            {
                if ( idx > split_idx[ val_index + 1 ] )
                {
                    val_index++;
                }

                internal_size = split_idx[ val_index + 1 ] - split_idx[ val_index ];

                if ( m_pts_info_vec[ split_idx[ val_index + 1 ] ].polar_dis_sq2 > 10000 )
                {
                    pt_angle_index = split_idx[ val_index + 1 ] - ( int ) ( internal_size * 0.20 );
                    scan_angle = atan2( m_pts_info_vec[ pt_angle_index ].pt_2d_img( 1 ), m_pts_info_vec[ pt_angle_index ].pt_2d_img( 0 ) ) * 57.3;
                    scan_angle = scan_angle + 180.0;
                    //pt_angle_index = split_idx[ val_index + 1 ] - 10;
                }
                else
                {
                    pt_angle_index = split_idx[ val_index + 1 ] - ( int ) ( internal_size * 0.80 );
                    scan_angle = atan2( m_pts_info_vec[ pt_angle_index ].pt_2d_img( 1 ), m_pts_info_vec[ pt_angle_index ].pt_2d_img( 0 ) ) * 57.3;
                    scan_angle = scan_angle + 180.0;
                    //pt_angle_index = split_idx[ val_index ] + 10;
                }

                //cout << "Idx  = " << idx <<  " val = "<< val_index << "  angle = " << scan_angle << endl;
            }
            m_pts_info_vec[ idx ].polar_angle = scan_angle;
            scan_id_index[ idx ] = scan_angle;
        }

        //cout << "===== " << zero_idx.size() << "  " << edge_idx.size() << "  " << split_idx.size() << "=====" << endl;

        return split_idx.size() - 1;
    }

    // will be delete...
    //    template<typename T>
    void reorder_laser_cloud_scan( std::vector< pcl::PointCloud< pcl::PointXYZI > > &in_laserCloudScans, std::vector< std::vector< int > > &pts_mask )
    {
        unsigned int min_pts_per_scan = 0;
        cout << "Before reorder" << endl;
        //cout << "Cloud size: " << in_laserCloudScans.size() << endl;
        //std::vector< pcl::PointCloud< PointType > > res_laser_cloud( in_laserCloudScans.size() - 2 ); // abandon first and last
        //std::vector<std::vector<int>> res_pts_mask( in_laserCloudScans.size() - 2 );
        std::vector< pcl::PointCloud< pcl::PointXYZI > > res_laser_cloud( in_laserCloudScans.size() ); // abandon first and last
        std::vector< std::vector< int > >                res_pts_mask( in_laserCloudScans.size() );
        std::map< float, int > map_angle_idx;

        // for (unsigned int i = 1; i < in_laserCloudScans.size() - 1; i++ )
        for ( unsigned int i = 0; i < in_laserCloudScans.size() - 0; i++ )
        {
            if ( in_laserCloudScans[ i ].size() > min_pts_per_scan )
            {
                //cout << i << endl;
                //cout << "[" << i << "] size = ";
                //cout << in_laserCloudScans[ i ].size() << "  ,id = " << ( int ) in_laserCloudScans[ i ].points[ 0 ].intensity << endl;
                map_angle_idx.insert( std::make_pair( in_laserCloudScans[ i ].points[ 0 ].intensity, i ) );
            }
            else
            {
                continue;
            }
        }

        cout << "After reorder" << endl;
        std::map< float, int >::iterator it;
        int current_index = 0;

        for ( it = map_angle_idx.begin(); it != map_angle_idx.end(); it++ )
        {
            //cout << "[" << current_index << "] id = " << it->first << endl;
            if ( in_laserCloudScans[ it->second ].size() > min_pts_per_scan )
            {
                res_laser_cloud[ current_index ] = in_laserCloudScans[ it->second ];
                res_pts_mask[ current_index ] = pts_mask[ it->second ];
                current_index++;
            }
        }

        res_laser_cloud.resize( current_index );
        res_pts_mask.resize( current_index );
        //cout << "Final size = " << current_index <<endl;
        //printf_line;
        in_laserCloudScans = res_laser_cloud;
        pts_mask = res_pts_mask;
        cout << "Return size = " << pts_mask.size() << "  " << in_laserCloudScans.size() << endl;
        return;
    }

    // Split whole point cloud into scans.
    template < typename T >
    void split_laser_scan_zvision(const pcl::PointCloud< T > &laserCloudIn,
                           std::vector< pcl::PointCloud< PointType > > &laserCloudScans )
    {
        int laser_cnt = 3;//三个激光器视场
        int group_cnt = 10000;//每个视场10000个点
        std::vector< std::vector< int > > pts_mask;
        laserCloudScans.resize( laser_cnt );
        pts_mask.resize( laser_cnt );
        PointType point;
        int       scan_idx = 0;

        for(int laser = 0;laser < laser_cnt;laser++)
        {
            pts_mask[ laser ].reserve( group_cnt );
            laserCloudScans[ laser ].reserve( group_cnt );
            for(int group = 0; group<group_cnt; group++)
            {
                laserCloudScans[laser][group] = laserCloudIn.points[ laser * group_cnt +  group];
                pts_mask[laser][group] = ( m_pts_info_vec[ laser * group_cnt +  group].pt_type );
            }
        }


        int remove_point_pt_type = e_pt_000 |
                                   e_pt_too_near |
                                   e_pt_nan // |
                                 //e_circle_edge
                                   ;

        for ( unsigned int i = 0; i < laserCloudScans.size(); i++ )
        {
            //std::cout << "Scan idx = " << i;
            //cout << "  ,length = " << laserCloudScans[ i ].size();
            //cout << "  ,intensity = " << laserCloudScans[ i ].points[ 0 ].intensity << std::endl;
            int scan_avail_num = 0;
            for ( unsigned int idx = 0; idx < laserCloudScans[ i ].size(); idx++ )
            {
                if ( ( pts_mask[ i ][ idx ] & remove_point_pt_type ) == 0 )
                //if(pts_mask[i][idx] == e_normal )
                {
                    if ( laserCloudScans[ i ].points[ idx ].y == 0 )//X为零说明是无效点
                    {
                        printf( "Error!!! Mask = %d\r\n", pts_mask[ i ][ idx ] );
                        assert( laserCloudScans[ i ].points[ idx ].x != 0 );
                        continue;
                    }
                    laserCloudScans[ i ].points[ scan_avail_num ] = laserCloudScans[ i ].points[ idx ];
                    set_intensity( laserCloudScans[ i ].points[ scan_avail_num ], default_return_intensity_type );
                    scan_avail_num++;
                }
            }
            laserCloudScans[ i ].resize( scan_avail_num );
        }
        //printf_line;
    }

    // Split whole point cloud into scans.
    template < typename T >
    void split_laser_scan( const int clutter_size, const pcl::PointCloud< T > &laserCloudIn,
                           const std::vector< float > &                 scan_id_index,
                           std::vector< pcl::PointCloud< PointType > > &laserCloudScans )
    {
        std::vector< std::vector< int > > pts_mask;
        laserCloudScans.resize( clutter_size );
        pts_mask.resize( clutter_size );
        PointType point;
        int       scan_idx = 0;

        for ( unsigned int i = 0; i < laserCloudIn.size(); i++ )
        {

            point = laserCloudIn.points[ i ];

            //point.intensity = ( float ) ( scan_id_index[ i ] );

            if ( i > 0 && ( ( scan_id_index[ i ] ) != ( scan_id_index[ i - 1 ] ) ) )
            {
                //std::cout << "Scan idx = " << scan_idx << " intensity = " << scan_id_index[ i ] << std::endl;
                scan_idx = scan_idx + 1;
                pts_mask[ scan_idx ].reserve( 5000 );
            }

            laserCloudScans[ scan_idx ].push_back( point );
            pts_mask[ scan_idx ].push_back( m_pts_info_vec[ i ].pt_type );
        }
        laserCloudScans.resize(scan_idx);


        int remove_point_pt_type = e_pt_000 |
                                   e_pt_too_near |
                                   e_pt_nan // |
                                 //e_circle_edge
                                   ;

        for ( unsigned int i = 0; i < laserCloudScans.size(); i++ )
        {
            //std::cout << "Scan idx = " << i;
            //cout << "  ,length = " << laserCloudScans[ i ].size();
            //cout << "  ,intensity = " << laserCloudScans[ i ].points[ 0 ].intensity << std::endl;
            int scan_avail_num = 0;
            for ( unsigned int idx = 0; idx < laserCloudScans[ i ].size(); idx++ )
            {
                if ( ( pts_mask[ i ][ idx ] & remove_point_pt_type ) == 0 )
                //if(pts_mask[i][idx] == e_normal )
                {
                    if ( laserCloudScans[ i ].points[ idx ].x == 0 )//X为零说明是无效点
                    {
                        printf( "Error!!! Mask = %d\r\n", pts_mask[ i ][ idx ] );
                        assert( laserCloudScans[ i ].points[ idx ].x != 0 );
                        continue;
                    }
                    laserCloudScans[ i ].points[ scan_avail_num ] = laserCloudScans[ i ].points[ idx ];
                    set_intensity( laserCloudScans[ i ].points[ scan_avail_num ], default_return_intensity_type );
                    scan_avail_num++;
                    // cur_pt_idx++;
                }
            }
            laserCloudScans[ i ].resize( scan_avail_num );
        }
        //printf_line;
    }

    template < typename T >
    std::vector< pcl::PointCloud< pcl::PointXYZI > > extract_laser_features( pcl::PointCloud< T > &laserCloudIn, std::vector<pcl::PointCloud<PointType>>& outLines, double time_stamp = -1 )
    {
        //printf_line;
        assert(time_stamp >= 0.0);
        if(time_stamp == 0 ) // old firmware, without timestamp
        {
            m_current_time = m_last_maximum_time_stamp;
        }
        if ( m_first_receive_time <= 0 )
        {
            m_first_receive_time = time_stamp;
        }

        m_current_time = time_stamp - m_first_receive_time;
        //printf("First extract feature, time = %.5f \r\n", m_first_receive_time);
        //printf("Extract features, current time = %.5f \r\n", m_current_time);
        std::vector< pcl::PointCloud< PointType > > laserCloudScans, temp_laser_scans;
        std::vector< float >                        scan_id_index;
        laserCloudScans.clear();//three laser field
        m_map_pt_idx.clear();
        //printf_line;
        if ( m_if_save_pcd_file )
        {
            stringstream ss;
            ss << PCL_DATA_SAVE_DIR << "/pc_" << pcl_data_save_index << ".pcd" << endl;
            pcl_data_save_index = pcl_data_save_index + 1;
            std::cout << "Save file = " << ss.str() << endl;
            pcl::io::savePCDFileASCII( ss.str(), laserCloudIn );
        }

        //分割成了 8 字体的四分之一长度线条
        int clutter_size = projection_scan_3d_2d( laserCloudIn, scan_id_index );//第二个参数是每个点的角度数据
        compute_features();//计算曲率

        if ( clutter_size == 0 )
        {
            return laserCloudScans;
        }
        else
        {
            static int flag = 0;
            split_laser_scan( clutter_size, laserCloudIn, scan_id_index, laserCloudScans );
            //printf("clutter:%d %d\n", clutter_size, laserCloudScans.size());
            if(flag == 0)
            {
                FILE *p = fopen("/home/cpf/ttt.txt", "w");
                flag = 1;
                for(int it =0;it < clutter_size;it++)
                    for(int itt = 0; itt<laserCloudScans[it].size();itt++)
                    {
                        int color[3] = {0};
                        color[it % 3] = 255;
                        fprintf(p, "%f %f %f %d %d %d\n", laserCloudScans[it][itt].x, laserCloudScans[it][itt].y, laserCloudScans[it][itt].z, color[0], color[1], color[2]);
                    }
            }
            return laserCloudScans;
        }
    }

    template < typename T >
    std::vector< pcl::PointCloud< pcl::PointXYZI > > extract_laser_features_zvision( pcl::PointCloud< T > &laserCloudIn, double time_stamp = -1 )
    {
        //printf_line;
        assert(time_stamp >= 0.0);
        if(time_stamp == 0 ) // old firmware, without timestamp
        {
            m_current_time = m_last_maximum_time_stamp;
        }
        if ( m_first_receive_time <= 0 )//初始化为-1
        {
            m_first_receive_time = time_stamp;
        }

        m_current_time = time_stamp - m_first_receive_time;//当前数据中的时间戳 减去 第一帧数据的时间戳
        //printf("First extract feature, time = %.5f \r\n", m_first_receive_time);
        //printf("Extract features, current time = %.5f \r\n", m_current_time);
        std::vector< pcl::PointCloud< PointType > > laserCloudScans, temp_laser_scans;
        std::vector< float >                        scan_id_index;
        laserCloudScans.clear();//three laser field
        m_map_pt_idx.clear();
        //printf_line;
        if ( m_if_save_pcd_file )
        {
            stringstream ss;
            ss << PCL_DATA_SAVE_DIR << "/pc_" << pcl_data_save_index << ".pcd" << endl;
            pcl_data_save_index = pcl_data_save_index + 1;
            std::cout << "Save file = " << ss.str() << endl;
            pcl::io::savePCDFileASCII( ss.str(), laserCloudIn );
        }

        //分割成了 8 字体的四分之一长度线条
        projection_scan_3d_2d_zvision( laserCloudIn );//计算每个点的距离平方,打上距离过小或者是无效点的标签
        int clutter_size = 3;//三个激光器单独计算特征值，避免出现单个视场特征点过于集中
        compute_features_zvision();//计算曲率，打上特征点标签

        {
            static int flag = 0;
            split_laser_scan_zvision(laserCloudIn, laserCloudScans );
            //printf("clutter:%d %d\n", clutter_size, laserCloudScans.size());
            if(flag == 1)
            {
                FILE *p = fopen("/home/cpf/ttt.txt", "w");
                flag = 1;
                for(int it =0;it < clutter_size;it++)
                    for(int itt = 0; itt<laserCloudScans[it].size();itt++)
                    {
                        int color[3] = {0};
                        color[it % 3] = 255;
                        fprintf(p, "%f %f %f %d %d %d\n", laserCloudScans[it][itt].x, laserCloudScans[it][itt].y, laserCloudScans[it][itt].z, color[0], color[1], color[2]);
                    }
            }
            return laserCloudScans;
        }
    }
};

#endif // LIVOX_LASER_SCAN_HANDLER_HPP
