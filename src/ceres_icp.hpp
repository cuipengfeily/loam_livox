// Author: Lin Jiarong          ziv.lin.ljr@gmail.com

#ifndef __ceres_icp_hpp__
#define __ceres_icp_hpp__
#define MAX_LOG_LEVEL -100
#include "eigen_math.hpp"
#include <Eigen/Eigen>
#include <ceres/ceres.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <math.h>
#include <stdio.h>
#include <vector>
// Point to Point ICP
// Contour to contour ICP
// Plane to Plane ICP
//p2p with motion deblur
template <typename _T>
struct ceres_icp_point2point
{
    Eigen::Matrix<_T, 3, 1> m_current_pt;
    Eigen::Matrix<_T, 3, 1> m_closest_pt;
    _T m_motion_blur_s;
    Eigen::Matrix<_T, 4, 1> m_q_last;
    Eigen::Matrix<_T, 3, 1> m_t_last;
    _T m_weigh;
    ceres_icp_point2point( const Eigen::Matrix<_T, 3, 1> current_pt,
                           const Eigen::Matrix<_T, 3, 1> closest_pt,
                           const _T &motion_blur_s = 1.0,
                           Eigen::Matrix<_T, 4, 1> q_s = Eigen::Matrix<_T, 4, 1>( 1, 0, 0, 0 ),
                           Eigen::Matrix<_T, 3, 1> t_s = Eigen::Matrix<_T, 3, 1>( 0, 0, 0 ) ) : m_current_pt( current_pt ),
                                                                                                m_closest_pt( closest_pt ),
                                                                                                m_motion_blur_s( motion_blur_s ),
                                                                                                m_q_last( q_s ),
                                                                                                m_t_last( t_s )

    {
        m_weigh = 1.0;
    };

    template <typename T>
    bool operator()( const T *_q, const T *_t, T *residual ) const
    {

        Eigen::Quaternion<T> q_last{ ( T ) m_q_last( 0 ), ( T ) m_q_last( 1 ), ( T ) m_q_last( 2 ), ( T ) m_q_last( 3 ) };
        Eigen::Matrix<T, 3, 1> t_last = m_t_last.template cast<T>();

        Eigen::Quaternion<T> q_incre{ _q[ 3 ], _q[ 0 ], _q[ 1 ], _q[ 2 ] };
        Eigen::Matrix<T, 3, 1> t_incre{ _t[ 0 ], _t[ 1 ], _t[ 2 ] };

        Eigen::Quaternion<T> q_interpolate = Eigen::Quaternion<T>::Identity().slerp( ( T ) m_motion_blur_s, q_incre );
        Eigen::Matrix<T, 3, 1> t_interpolate = t_incre * T( m_motion_blur_s );

        Eigen::Matrix<T, 3, 1> pt{ T( m_current_pt( 0 ) ), T( m_current_pt( 1 ) ), T( m_current_pt( 2 ) ) };
        Eigen::Matrix<T, 3, 1> pt_transfromed;
        pt_transfromed = q_last * ( q_interpolate * pt + t_interpolate ) + t_last;

        //Eigen::Matrix< T, 3, 1 > vec_err = ( pt_transfromed - m_closest_pt );

        residual[ 0 ] = ( pt_transfromed( 0 ) - T( m_closest_pt( 0 ) ) ) * T( m_weigh );
        residual[ 1 ] = ( pt_transfromed( 1 ) - T( m_closest_pt( 1 ) ) ) * T( m_weigh );
        residual[ 2 ] = ( pt_transfromed( 2 ) - T( m_closest_pt( 2 ) ) ) * T( m_weigh );
        return true;
    };

    static ceres::CostFunction *Create( const Eigen::Matrix<_T, 3, 1> current_pt,
                                        const Eigen::Matrix<_T, 3, 1> closest_pt,
                                        const _T motion_blur_s = 1.0,
                                        Eigen::Matrix<_T, 4, 1> q_s = Eigen::Matrix<_T, 4, 1>( 1, 0, 0, 0 ),
                                        Eigen::Matrix<_T, 3, 1> t_s = Eigen::Matrix<_T, 3, 1>( 0, 0, 0 ) )
    {
        return ( new ceres::AutoDiffCostFunction<
                 ceres_icp_point2point, 3, 4, 3>(
            new ceres_icp_point2point( current_pt, closest_pt, motion_blur_s ) ) );
    }
};

//point-to-line with motion deblur
template <typename _T>
struct ceres_icp_point2line
{
    Eigen::Matrix<_T, 3, 1> m_current_pt;
    Eigen::Matrix<_T, 3, 1> m_target_line_a, m_target_line_b;
    Eigen::Matrix<_T, 3, 1> m_unit_vec_ab;
    _T m_motion_blur_s;
    Eigen::Matrix<_T, 4, 1> m_q_last;
    Eigen::Matrix<_T, 3, 1> m_t_last;
    _T m_weigh;
    ceres_icp_point2line( const Eigen::Matrix<_T, 3, 1> &current_pt,
                          const Eigen::Matrix<_T, 3, 1> &target_line_a,
                          const Eigen::Matrix<_T, 3, 1> &target_line_b,
                          const _T motion_blur_s = 1.0,
                          Eigen::Matrix<_T, 4, 1> q_s = Eigen::Matrix<_T, 4, 1>( 1, 0, 0, 0 ),
                          Eigen::Matrix<_T, 3, 1> t_s = Eigen::Matrix<_T, 3, 1>( 0, 0, 0 ) ) : m_current_pt( current_pt ), m_target_line_a( target_line_a ),
                                                                                               m_target_line_b( target_line_b ),
                                                                                               m_motion_blur_s( motion_blur_s ),
                                                                                               m_q_last( q_s ),
                                                                                               m_t_last( t_s )
    {
        m_unit_vec_ab = target_line_b - target_line_a;
        m_unit_vec_ab = m_unit_vec_ab / m_unit_vec_ab.norm();
        // m_weigh = 1/m_current_pt.norm();
        m_weigh = 1.0;
        //cout << m_unit_vec_ab.transpose() <<endl;
    };

    //计算残差的模板
    template <typename T>
    bool operator()( const T *_q, const T *_t, T *residual ) const
    {

        Eigen::Quaternion<T> q_last{ ( T ) m_q_last( 0 ), ( T ) m_q_last( 1 ), ( T ) m_q_last( 2 ), ( T ) m_q_last( 3 ) };
        Eigen::Matrix<T, 3, 1> t_last = m_t_last.template cast<T>();

        Eigen::Quaternion<T> q_incre{ _q[ 3 ], _q[ 0 ], _q[ 1 ], _q[ 2 ] };//当前帧的旋转
        Eigen::Matrix<T, 3, 1> t_incre{ _t[ 0 ], _t[ 1 ], _t[ 2 ] };//当前帧的平移

        Eigen::Quaternion<T> q_interpolate = Eigen::Quaternion<T>::Identity().slerp( ( T ) m_motion_blur_s, q_incre );//根据点在当前帧中的位置(0.0 is first point - 1.0 is last point)插值旋转量
        Eigen::Matrix<T, 3, 1> t_interpolate = t_incre * T( m_motion_blur_s );//根据点在当前帧中的位置(0.0 is first point - 1.0 is last point)插值平移

        Eigen::Matrix<T, 3, 1> pt = m_current_pt.template cast<T>();
        Eigen::Matrix<T, 3, 1> pt_transfromed;
        pt_transfromed = q_last * ( q_interpolate * pt + t_interpolate ) + t_last;//使用插值四元数和插值坐标平移作运动补偿，用q_last和t_last做雷达坐标系到MAP坐标系中的转换

        Eigen::Matrix<T, 3, 1> tar_line_pt_a = m_target_line_a.template cast<T>();//ICP点到线中， 线的起点
        Eigen::Matrix<T, 3, 1> vec_line_ab_unit = m_unit_vec_ab.template cast<T>();//ICP点到线中，线的单位向量

        Eigen::Matrix<T, 3, 1> vec_ac = pt_transfromed - tar_line_pt_a;
        Eigen::Matrix<T, 3, 1> residual_vec = vec_ac - Eigen_math::vector_project_on_unit_vector( vec_ac, vec_line_ab_unit );//以ICP点到 直线的垂足(FOOT)为起点，以ICP点为终点的向量

        residual[ 0 ] = residual_vec( 0 ) * T( m_weigh );//权重都是1
        residual[ 1 ] = residual_vec( 1 ) * T( m_weigh );//权重都是1
        residual[ 2 ] = residual_vec( 2 ) * T( m_weigh );//权重都是1

        return true;
    };

    static ceres::CostFunction *Create( const Eigen::Matrix<_T, 3, 1> &current_pt,
                                        const Eigen::Matrix<_T, 3, 1> &target_line_a,
                                        const Eigen::Matrix<_T, 3, 1> &target_line_b,
                                        const _T motion_blur_s = 1.0,
                                        Eigen::Matrix<_T, 4, 1> q_last = Eigen::Matrix<_T, 4, 1>( 1, 0, 0, 0 ),
                                        Eigen::Matrix<_T, 3, 1> t_last = Eigen::Matrix<_T, 3, 1>( 0, 0, 0 ) )
    {
        // TODO: can be vector or distance
        /*AutoDiffCostFunction的模板参数顺序：
          1: 代价函数，也即 operator（）重载
          2: 残差块中残差项的数量，这里是3个残差项 delta_x/y/z
          3: 参数块 1 中参数的数量4个旋转参数 operator()中第一个参数的数量
          4: 参数块 2 中参数的数量3个平移参数 operator()中第二个参数的数量
        */

        return ( new ceres::AutoDiffCostFunction<
                 ceres_icp_point2line, 3, 4, 3>(
            new ceres_icp_point2line( current_pt, target_line_a, target_line_b, motion_blur_s, q_last, t_last ) ) );
    }
};

// point to plane with motion deblur
template <typename _T>
struct ceres_icp_point2plane
{
    Eigen::Matrix<_T, 3, 1> m_current_pt;
    Eigen::Matrix<_T, 3, 1> m_target_line_a, m_target_line_b, m_target_line_c;
    Eigen::Matrix<_T, 3, 1> m_unit_vec_ab, m_unit_vec_ac, m_unit_vec_n;
    _T m_motion_blur_s;
    _T m_weigh;
    Eigen::Matrix<_T, 4, 1> m_q_last;
    Eigen::Matrix<_T, 3, 1> m_t_last;
    ceres_icp_point2plane( const Eigen::Matrix<_T, 3, 1> &current_pt,
                           const Eigen::Matrix<_T, 3, 1> &target_line_a,
                           const Eigen::Matrix<_T, 3, 1> &target_line_b,
                           const Eigen::Matrix<_T, 3, 1> &target_line_c,
                           const _T motion_blur_s = 1.0,
                           Eigen::Matrix<_T, 4, 1> q_s = Eigen::Matrix<_T, 4, 1>( 1, 0, 0, 0 ),
                           Eigen::Matrix<_T, 3, 1> t_s = Eigen::Matrix<_T, 3, 1>( 0, 0, 0 ) ) : m_current_pt( current_pt ), m_target_line_a( target_line_a ),
                                                                                                m_target_line_b( target_line_b ),
                                                                                                m_target_line_c( target_line_c ),
                                                                                                m_motion_blur_s( motion_blur_s ),
                                                                                                m_q_last( q_s ),
                                                                                                m_t_last( t_s )

    {
        //assert( motion_blur_s <= 1.5 && motion_blur_s >= 0.0 );
        //assert( motion_blur_s <= 1.01 && motion_blur_s >= 0.0 );
        m_unit_vec_ab = target_line_b - target_line_a;
        m_unit_vec_ab = m_unit_vec_ab / m_unit_vec_ab.norm();

        m_unit_vec_ac = target_line_c - target_line_a;
        m_unit_vec_ac = m_unit_vec_ac / m_unit_vec_ac.norm();

        m_unit_vec_n = m_unit_vec_ab.cross( m_unit_vec_ac );
        m_weigh = 1.0;
    };

    template <typename T>
    bool operator()( const T *_q, const T *_t, T *residual ) const
    {

        Eigen::Quaternion<T> q_last{ ( T ) m_q_last( 0 ), ( T ) m_q_last( 1 ), ( T ) m_q_last( 2 ), ( T ) m_q_last( 3 ) };
        Eigen::Matrix<T, 3, 1> t_last = m_t_last.template cast<T>();

        Eigen::Quaternion<T> q_incre{ _q[ 3 ], _q[ 0 ], _q[ 1 ], _q[ 2 ] };
        Eigen::Matrix<T, 3, 1> t_incre{ _t[ 0 ], _t[ 1 ], _t[ 2 ] };

        Eigen::Quaternion<T> q_interpolate = Eigen::Quaternion<T>::Identity().slerp( ( T ) m_motion_blur_s, q_incre );
        Eigen::Matrix<T, 3, 1> t_interpolate = t_incre * T( m_motion_blur_s );

        Eigen::Matrix<T, 3, 1> pt = m_current_pt.template cast<T>();
        Eigen::Matrix<T, 3, 1> pt_transfromed;
        pt_transfromed = q_last * ( q_interpolate * pt + t_interpolate ) + t_last;

        Eigen::Matrix<T, 3, 1> tar_line_pt_a = m_target_line_a.template cast<T>();
        Eigen::Matrix<T, 3, 1> vec_line_plane_norm = m_unit_vec_n.template cast<T>();

        Eigen::Matrix<T, 3, 1> vec_ad = pt_transfromed - tar_line_pt_a;
        Eigen::Matrix<T, 3, 1> residual_vec = Eigen_math::vector_project_on_unit_vector( vec_ad, vec_line_plane_norm ) * T( m_weigh );

        residual[ 0 ] = residual_vec( 0 ) * T( m_weigh );
        residual[ 1 ] = residual_vec( 1 ) * T( m_weigh );
        residual[ 2 ] = residual_vec( 2 ) * T( m_weigh );
        //cout << residual_vec.rows() << "  " <<residual_vec.cols()  <<endl;

        //cout << " *** " << residual_vec[0] << "  " << residual_vec[1] << "  " << residual_vec[2] ;
        //cout << " *** " << residual[0] << "  " << residual[1] << "  " << residual[2] << " *** " << endl;
        return true;
    };

    static ceres::CostFunction *Create( const Eigen::Matrix<_T, 3, 1> &current_pt,
                                        const Eigen::Matrix<_T, 3, 1> &target_line_a,
                                        const Eigen::Matrix<_T, 3, 1> &target_line_b,
                                        const Eigen::Matrix<_T, 3, 1> &target_line_c,
                                        const _T motion_blur_s = 1.0,
                                        Eigen::Matrix<_T, 4, 1> q_last = Eigen::Matrix<_T, 4, 1>( 1, 0, 0, 0 ),
                                        Eigen::Matrix<_T, 3, 1> t_last = Eigen::Matrix<_T, 3, 1>( 0, 0, 0 ) )
    {
        // TODO: can be vector or distance
        return ( new ceres::AutoDiffCostFunction<
                 ceres_icp_point2plane, 3, 4, 3>(
            new ceres_icp_point2plane( current_pt, target_line_a, target_line_b, target_line_c, motion_blur_s, q_last, t_last ) ) );
    }
};

#endif
