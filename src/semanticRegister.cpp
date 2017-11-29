#define BULK_ADJUSTMENT 1

#ifdef BULK_ADJUSTMENT
#include "g2o/core/sparse_optimizer.h"
#include "g2o/types/slam3d/types_slam3d.h"
#include "g2o/core/optimization_algorithm_factory.h"
using namespace g2o;
#endif

#include <iostream>
#include <sstream>
#include <string>
#include <fstream>
#include <unistd.h>
#include <boost/program_options.hpp>
#include <pcl/filters/crop_box.h>
#include <se_ndt/ndt_fuser_hmt_se.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
using namespace std;
namespace po = boost::program_options;

Eigen::Matrix<double,7,6> getJacobian(Eigen::VectorXd v)
{
	Eigen::Matrix<double,7,6> m;
	double x=v(0)/2;
	double y=v(1)/2;
	double z=v(2)/2;
	double cx=cos(x);
	double cy=cos(y);
	double cz=cos(z);
	double sx=sin(x);
	double sy=sin(y);
	double sz=sin(z);
	m(0,3)=0;
	m(1,3)=0;
	m(2,3)=0;
	m(3,3)=-sx*sy*cz+cx*cy*sz;
	m(4,3)=-sx*cy*sz-cx*sy*cz;
	m(5,3)=+cx*cy*cz+sx*sy*sz;
	m(6,3)=-sx*cy*cz+cx*sy*sz;

	m(0,4)=0;
	m(1,4)=0;
	m(2,4)=0;
	m(3,4)=+cx*cy*cz-sx*sy*sz;
	m(4,4)=-cx*sy*sz-sx*cy*cz;
	m(5,4)=-sx*sy*cz-cx*cy*sz;
	m(6,4)=-cx*sy*cz+sx*cy*sz;

	m(0,5)=0;
	m(1,5)=0;
	m(2,5)=0;
	m(3,5)=-cx*sy*sz+sx*cy*cz;
	m(4,5)=+cx*cy*cz+sx*sy*sz;
	m(5,5)=-sx*cy*sz-cx*sy*cz;
	m(6,5)=-cx*cy*sz+sx*sy*cz;

	m=m/2;

	m(0,0)=1;//1
	m(1,0)=0;
	m(2,0)=0;
	m(3,0)=0;
	m(4,0)=0;
	m(5,0)=0;
	m(6,0)=0;

	m(0,1)=0;
	m(1,1)=1;//1	
	m(2,1)=0;
	m(3,1)=0;
	m(4,1)=0;
	m(5,1)=0;
	m(6,1)=0;

	m(0,2)=0;
	m(1,2)=0;
	m(2,2)=1;//1
	m(3,2)=0;
	m(4,2)=0;
	m(5,2)=0;
	m(6,2)=0;

	return m;
}
Eigen::VectorXd get7d(Eigen::Affine3d p)
{
	Eigen::VectorXd v(7);
	Eigen::Quaternion<double> q(p.rotation());
	v<<p.translation(),q.coeffs();
	v(6)=sqrt(1-v.segment(3,3).norm());
	return v;
}
vector<double>  getMeasure(string filename)
{
	ifstream infile(filename); // for example
	string line = "";
	vector<double> measure;
	while (getline(infile, line)){
		measure.push_back(stod(line));
	}
    return measure;
}
pcl::PointCloud<pcl::PointXYZI>::Ptr cropIt(pcl::PointCloud<pcl::PointXYZI>::Ptr laserCloud, std::vector<float>& b)
{
	pcl::PointCloud<pcl::PointXYZI>::Ptr outC(new pcl::PointCloud<pcl::PointXYZI>);
	pcl::CropBox<pcl::PointXYZI> cropBoxFilter(false);
	cropBoxFilter.setInputCloud(laserCloud);
	cropBoxFilter.setMin(Eigen::Vector4f (b[0],b[1],b[2], 1));
	cropBoxFilter.setMax(Eigen::Vector4f (b[3],b[4],b[5], 1));
	cropBoxFilter.setNegative(true);
	cropBoxFilter.filter(*outC);
	return outC;
}

int main(int argc, char** argv)
{
	string transforms;
	string ndt_folder;
	char IFS=',';
	float parameter;
	bool Inv=false;
	po::options_description desc("Allowed options");
	desc.add_options()
	("help,h", "produce help message")
	("skip", "skip point cloud first line")
	("nc,n", "Do not concatenate transforms")
	 ("inv,i",  "Inverse apply transforms")
	 ("transforms,t", po::value<std::string >(&transforms), "File with initial transforms")
	 ("pointclouds,p", po::value<std::vector<string> >()->multitoken(), "Point cloud files")
	 ("b", po::value<std::vector<float> >()->multitoken(), "Bounding box--Atention! not working yet!")
	 ("parameter", po::value<float>(&parameter), "Bounding box--Atention! not working yet!")
	 ("ifs,f", po::value<char>(&IFS), "Pointcloud IFS")
	 ("ndt_folder", po::value<std::string >(&ndt_folder)->default_value("/tmp/semantic/"), "Folder to load/save computed NDTs")
	 ("sem,s", po::value<std::vector<string> >()->multitoken(), "First semantic input files");

	po::parsed_options parsed_options = po::command_line_parser(argc, argv)
      .options(desc).style(po::command_line_style::unix_style ).run();

   std::vector<std::vector<std::string>> sem_files;
   for (const po::option& o : parsed_options.options) {
      if (o.string_key == "sem")
         sem_files.push_back(o.value);
   }

    po::variables_map vm;
//    po::store(po::parse_command_line(argc, argv, desc, po::command_line_style::unix_style ^ po::command_line_style::allow_short), vm);
	po::store(parsed_options, vm);
    po::notify(vm);
	vector<string> pointcloud_files;
	vector<float> box;

	bool trans=false;
	ifstream in_trans;
	if(vm.count("transforms")) { in_trans.open(transforms, ifstream::in); trans=true; }

	bool skip=false;
	if(vm.count("skip")) skip=true;
	if(vm.count("inv"))Inv=true;
	bool conc=true;
	if(vm.count("nc")) conc=false;
	if(vm.count("help"))
	{
		cout<<desc;
		return 0;
	}
	if (!vm["pointclouds"].empty() && (pointcloud_files= vm["pointclouds"].as<vector<string> >()).size() >= 2) {
		///cout<<"success pointcloud read";
	}else {cout<<"pointclouds read failure";};
	bool h_box=false;
	if(vm.count("b")) {h_box=true;box=vm["b"].as<vector<float> >();if(box.size()!=6){cout<<"Wrong box size! must be 6."<<endl;return -1;}}
	int num_files=pointcloud_files.size();
	for(int i=0;i<sem_files.size();i++)
		num_files=min(num_files,(int ) sem_files.at(i).size());


	Eigen::Affine3d T;
	Eigen::Affine3d Tt;
	T.setIdentity();
	Tt.setIdentity();
	//NDTMatch_SE matcher ({0.5,0.1,0.05},{0,1,0,1,2},{25,25,10},{3},{-1},0.60,25);
	//NDTMatch_SE matcher ({200,60,200,70,50,5},{0,1,2,3,4,5},{200,200,200},{'*'},{0},0.01,50);// :-D
	//lslgeneric::NDTFuserHMT_SE matcher (the_initial_pose,{the_resolutions},{the_order_with which_the_resolutions_are_used},{the_size_of_the_map},{the_tail_segments},{ignore_values},reject_percentage,number_of_iterations);
#ifndef BULK_ADJUSTMENT
	NDTMatch_SE matcher ({1,2,0.5},{0,1,0,2},{200,200,200},{'e'},{1},0.01,50);// :-D
	for(int i=0;i<num_files;i++)
	{
		pcl::PointCloud<pcl::PointXYZI>::Ptr cloud3=h_box?cropIt(getCloud<pcl::PointXYZI>(pointcloud_files[i],IFS,skip),box):getCloud<pcl::PointXYZI>(pointcloud_files[i],IFS,skip);
		pcl::PointCloud<pcl::PointXYZ>::Ptr cloud1(new pcl::PointCloud<pcl::PointXYZ>);
		pcl::copyPointCloud(*cloud3,*cloud1);

		if(trans){
			Eigen::Affine3d T=readTransform(in_trans);
			pcl::transformPointCloud(*cloud1,*cloud1,T);
		}
		std::vector<double> rsd_min;
		for(int j=0;j<cloud1->size();j++)
			rsd_min.push_back(1);
		Tt=matcher.match(cloud1,{rsd_min});
		if(i!=0)
		{
			if(Inv) T=Tt*T;
			else T=T*Tt;
			for(int i=0;i<4;i++)
				for(int j=0;j<4;j++)
					cout<<(conc?T(i,j):Tt(i,j))<<", ";
			cout<<endl;
			cout<<matcher.getPoseCovariance(Tt);
		}
	}
#else
	struct stat st = {0};

	if (stat(ndt_folder.c_str(), &st) == -1) {
		mkdir(ndt_folder.c_str(), 0700);
	}
	//NDTMatch_SE matcher ({200,60,200,70,50,5},{0,1,2,3,4,5},{200,200,200},{'*'},{0},0.01,50);// :-D
	//NDTMatch_SE matcher ({100,20,5,2,1,0.5},{0,1,2,3,4,3,4,5},{200,200,200},{'*'},{0},0.01,5,true);// :-D
	NDTMatch_SE matcher ({70,10,1,2,parameter},{0,1,0,1,2,3,2,4},{200,200,200},{'=','=','=','=','=','=','=','='},{1,2,3,4,5,6,7,8},0.01,5);// :-D
	matcher.setNeighbours(3);
	matcher.IFS=IFS;
	matcher.skip=skip;
	matcher.precomputed_ndt_folder=ndt_folder;
	HyperGraph::VertexSet vertices;
	HyperGraph::EdgeSet edges;
	SparseOptimizer optimizer;
	OptimizationAlgorithmProperty solverProperty;
	OptimizationAlgorithmFactory* solverFactory = OptimizationAlgorithmFactory::instance();
	optimizer.setAlgorithm(solverFactory->construct("lm_fix6_3", solverProperty));
	if (! optimizer.solver()) {
		cerr << "Error allocating solver. Allocating \"" << "strSolver" << "\" failed!" << endl;
		return 0;
	}
	for(int i=0;i<num_files;i++)
	{
		VertexSE3* vert=new VertexSE3();
		vert->setId(i);
		if(i==0)
		{
			vert->setFixed(true);
			Eigen::Isometry3d Tr;
			Tr.setIdentity();
			vert->setEstimate(Tr);
		}
		optimizer.addVertex(vert);
	}

	for(int i=0;i<num_files;i++)
	{

//		pcl::PointCloud<pcl::PointXYZI>::Ptr cloud1_=getCloud(pointcloud_files[i],IFS,skip);
//		pcl::PointCloud<pcl::PointXYZ>::Ptr cloud1(new pcl::PointCloud<pcl::PointXYZ>);
//		pcl::copyPointCloud(*cloud1_,*cloud1);
		for(int j=i+1;j<num_files;j++)
		{
//			pcl::PointCloud<pcl::PointXYZI>::Ptr cloud2_=getCloud(pointcloud_files[j],IFS,skip);
//			pcl::PointCloud<pcl::PointXYZ>::Ptr cloud2(new pcl::PointCloud<pcl::PointXYZ>);
//			pcl::copyPointCloud(*cloud2_,*cloud2);

//			if(trans){
//				Eigen::Affine3d T=readTransform(in_trans);
//				pcl::transformPointCloud(*cloud2,*cloud2,T);
//			}
//			Tt=matcher.match(cloud1,cloud2,{std::vector<double>()},{std::vector<double>()});
			std::vector<double> sem1,sem2;
			sem1=getMeasure(sem_files[0][i]);
			sem2=getMeasure(sem_files[0][j]);
//			Tt=matcher.match(pointcloud_files[i],pointcloud_files[j],{std::vector<double>()},{std::vector<double>()});
			Tt=matcher.match(pointcloud_files[i],pointcloud_files[j],{sem1,sem1,sem1,sem1,sem1,sem1,sem1,sem1},{sem2,sem2,sem2,sem2,sem2,sem2,sem2,sem2});
			EdgeSE3 *edge=new EdgeSE3();
			edge->vertices()[0]=optimizer.vertex(i);
			edge->vertices()[1]=optimizer.vertex(j);
			Eigen::Isometry3d b;
			b.translation() = Tt.translation();
			std::cerr<<"["<<i<<","<<j<<"]\t"<<Tt.translation().transpose()<<std::endl;
			b.linear() = Tt.rotation();
			edge->setMeasurement(b);
			edge->setInformation(matcher.getPoseCovariance(Tt));
			optimizer.addEdge(edge);
	/*
		for(int i=0;i<4;i++)
			for(int j=0;j<4;j++)
				cout<<Tt(i,j)<<", ";
		cout<<endl;
*/
		}
	}
	optimizer.initializeOptimization();
	optimizer.optimize(200);
	for(int k=0;k<num_files;k++)
	{
		Eigen::Affine3d Tp=static_cast<VertexSE3*>(optimizer.vertex(k))->estimate();
		for(int i=0;i<4;i++)
			for(int j=0;j<4;j++)
				cout<<Tp(i,j)<<", ";
		cout<<endl;
	}

	optimizer.save("tutorial_before.g2o");
	optimizer.clear();
	// destroy all the singletons
	//Factory::destroy();
	OptimizationAlgorithmFactory::destroy();
	HyperGraphActionLibrary::destroy();

#endif

	return 0;
}

