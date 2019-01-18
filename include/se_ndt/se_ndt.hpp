#ifndef SE_NDT
#define SE_NDT
#include <vector>
#include <pcl/point_cloud.h>
#include <pcl/common/io.h>
#include <ndt_map/ndt_map.h>
#include <ndt_map/lazy_grid.h>
#include <ndt_map/ndt_cell.h>
#include <ndt_map/pointcloud_utils.h>
#include "se_ndt/ndt_matcher_d2d_se.h"
#include "se_ndt/ndt_histogram.h"
#ifdef GL_VISUALIZE
    #include "ndt_visualisation/ndt_viz.h"
#endif
#include <thread>
using namespace std;
Eigen::Matrix<double,6,6> getHes(Eigen::Matrix<double,6,6> Hessian,Eigen::Matrix<double,6,1> score_gradient);
class NDTMatch_SE{
    public:
        perception_oru::NDTMap** loadSavedMap(int index);
        bool useSaved=false;
        unsigned int NumInputs;
		perception_oru::NDTMap ***map;
		//perception_oru::NDTMap ***mapLocal_prev;
		perception_oru::NDTMap ***mapLocal;
		vector<float> resolutions,size;
		vector<int> inputs,resolutions_order;
        Eigen::Affine3d T, Td;
        std::vector<perception_oru::NDTHistogram> hists;
        pcl::KdTreeFLANN<pcl::PointXYZL> pose_kdtree;
        pcl::PointCloud<pcl::PointXYZL>::Ptr poses;
        unsigned int num_clouds=0;
        float max_size;

        Eigen::Affine3d matchFaster(Eigen::Affine3d Tinit, pcl::PointCloud<pcl::PointXYZI>::Ptr cloud);
        Eigen::Affine3d matchFaster(pcl::PointCloud<pcl::PointXYZI>::Ptr cloud);
        vector<pcl::PointCloud<pcl::PointXYZ>::Ptr> getSegmentsFast(pcl::PointCloud<pcl::PointXYZI>::Ptr laserCloudIn);
        Eigen::Affine3d matchFaster_OM(Eigen::Affine3d Tinit, pcl::PointCloud<pcl::PointXYZI>::Ptr cloud);
        Eigen::Affine3d mapUpdate(pcl::PointCloud<pcl::PointXYZI>::Ptr cloud, bool constant_velocity=true);

		NDTMatch_SE(initializer_list<float> b,initializer_list<int> c,initializer_list<float> d,initializer_list<int> inputs,int max_iter);
        void loadMap(perception_oru::NDTMap **map,std::vector<pcl::PointCloud<pcl::PointXYZ>::Ptr> input_clouds,float sensor_range=100);
		NDTMatch_SE(){};
		~NDTMatch_SE();
		perception_oru::NDTMatcherD2D_SE matcher;
		Eigen::Matrix<double,6,6> getPoseCovariance(Eigen::Affine3d T);
		void setNeighbours(short int i){matcher.n_neighbours=i;};
		float sensor_range=100;
    private:
		bool firstRun;
#ifdef GL_VISUALIZE
    public:
        NDTViz *viewer = NULL;
        void visualize();
    private:
        void visualize_thread();
#endif
};
typedef Eigen::Transform<double,3,Eigen::Affine,Eigen::ColMajor> ET;
Eigen::Matrix<double,7,6> getJacobian(Eigen::VectorXd v);
#endif/*SE_NDT*/
