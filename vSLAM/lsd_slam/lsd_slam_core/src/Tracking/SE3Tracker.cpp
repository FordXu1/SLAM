/**
* This file is part of LSD-SLAM.
* 欧式变换 SE(3) R,t 匹配跟踪
* https://blog.csdn.net/lancelot_vim/article/details/51758870
* https://blog.csdn.net/kokerf/article/details/78005934
* 这个类是Tracking算法的核心类，里面定义了和刚体运动相关的Traqcking所需要得数据和算法
* 
* LSD-SLAM的Tracking是算法框架中三大部分之一。其主要实现函数为SlamSystem::trackFrame
* 这个函数的代码主要分为如下几个步骤实现：
*  1.  构造新的图像帧： 把当前图像构建为新的图像帧
*  2.  更新参考帧：        如果当前参考帧不是最近的关键帧，则更新参考帧
*  3.  初始化位姿：        把上一帧与参考帧的位姿当做初始位姿
*  4.  SE3求解：             调用SE3Tracker计算当前帧和参考帧间的位姿变换
*  5.  判断是否跟踪失败：根据跟踪的像素点个数多少以及跟踪质量来判断
*  6.  关键帧筛选：            通过计算得分确定是否构造新的关键帧
* 
* 这里　是　SE3跟踪求解　　欧式变换矩阵求解　
* 
* LSD-SLAM在位姿估计中采用了直接法，也就是通过最小化光度误差来求解两帧图像间的位姿变化。
* 并且采用了LM算法迭代求解。
* 
* 误差函数　E(se3) = SUM((Iref(pi) - I(W(pi,Dref(pi),se3))^2)
* (参考帧下的像素值-参考帧3d点变换到当前帧下的像素值)^2　
* 也就是 所有匹配点　的光度误差和
* 
* 1. 首先在参考帧下根据深度信息计算出3d点云
* 2. 其次，计算参考帧下的点变换到当前帧下的亚像素像素值，进而计算初始　光度匹配误差err
* 3. 再次，利用误差对单个3d点的导数信息计算单个误差的可信程度，进行加权后方差归一化，得到权重ｗ，为了减少外点（outliers）对算法的影响
* 4. 然后，利用误差函数对误差向量求导数，求得各个点的误差之间的　协方差矩阵，也是雅克比矩阵Ｊ,计算系数A 和 b 
* 5. 最后计算　变换矩阵　se3的　更新量 dse3　
* 　　　　　　  dse3 = -(J转置*J)逆矩阵*J转置*w*err  直接求逆矩阵计算量较大
*              也可以写成：
* 　　　      　J转置*J * dse3 = -J转置*w*error
*              紧凑形式：
* 　　　　　　  A * dse3 = b
*       使用　LDLT分解 A = LDU＝LDL转置＝LDLT，求解线性方程组　A * x = b
*       记录　ｕ　＝　LD，　D为对角矩阵　L为下三角矩阵　L转置为上三角矩阵
*             v  ＝  L转置
*       将A分解为上面两个矩阵　相乘　A = u * v
*       Ax = b就可以化为u(v*x) = u*y = b
*       先求解　y
*       得到　  y = v*x
*       再求解　x 即　dse3 是欧式变换矩阵se3的更新量
* 
*       for(int i=0;i<6;i++) A(i,i) *= 1+LM_lambda;// (A+λI)  　列文伯格马尔夸克更新算法　LM 调整量　
*       Vector6 inc = A.ldlt().solve(b);// LDLT矩阵分解求解　dse3 李代数更新量
* 
* 6. 然后对　变换矩阵　左乘　se3指数映射　进行　更新　
* 　　se3　指数映射到SE3 通过　李群乘法直接　对　变换矩阵　左乘更新　
* 　　Sophus::SE3f new_referenceToFrame = Sophus::SE3f::exp((inc)) * referenceToFrame;　　　　
* 
* LSD-SLAM在求解两帧图像间的SE3变换主要在SE3Tracker类中进行实现。
* 该类中有四个函数比较重要，分别为
*  1. SE3Tracker::trackFrame             主调函数
*  2. SE3Tracker::calcResidualAndBuffers  被调用计算　
*      初始误差　err 参考帧3d点变换到当前帧图像坐标系下的残差(灰度误差)　和　梯度(对应点像素梯度)　
*     
*  3. SE3Tracker::calcWeightsAndResidual　被调用计算    
*     误差可信度权重w　并对误差方差归一化得到加权平均后的误差
*  4. SE3Tracker::calculateWarpUpdate　　 被调用计算    
*　　　误差关系矩阵　协方差矩阵　雅克比矩阵J　以及A=J转置*J b=-J转置*w*error 求接线性方程组
* 
*1.  SE3Tracker::trackFrame  
*  图像金字塔迭代level-4到level-1
*       Step1: 对参考帧当前层构造点云(reference->makePointCloud) 
*                  利用逆深度信息　和　相应的像素坐标 以及相机内参数得到　在参考帧坐标下的3d点    
*                  (X，Y，Z) = D  *  (u,v,1)*K逆 =1/(1/D)* (u*fx_inv + cx_inv, v+fy_inv+cy_inv, 1)
*       Step2: 计算变换到当前帧的残差和梯度(calcResidualAndBuffers)
*          计算参考帧3d点变换到当前帧图像坐标系下的残差(灰度误差)　和　梯度(对应点像素梯度)　
*          1. 参考帧3d点 R，t变换到 当前相机坐标系下 
*           　　　Eigen::Matrix3f rotMat = referenceToFrame.rotationMatrix();// 旋转矩阵
*         　　　　Eigen::Vector3f transVec = referenceToFrame.translation();// 平移向量
*                Eigen::Vector3f Wxp = rotMat * (*refPoint) + transVec;// 欧式变换
*          2. 再 投影到 当前相机 像素平面下
*                float u_new = (Wxp[0]/Wxp[2])*fx_l + cx_l;// float 浮点数类型
*                float v_new = (Wxp[1]/Wxp[2])*fy_l + cy_l;
*          3. 根据浮点数坐标 四周的四个整数点坐标的梯度 使用位置差加权进行求和得到亚像素灰度值　和　亚像素　梯度值
*                 x=u_new;
*                 y=v_new;
*                 int ix = (int)x;// 取整数    左上方点
*                 int iy = (int)y;
*                 float dx = x - ix;// 差值
*                 float dy = y - iy;
                  float dxdy = dx*dy;
*                 // map 像素梯度  指针
*                 const Eigen::Vector4f* bp = mat +ix+iy*width;// 左上方点　像素位置 梯度的 指针
* // 使用 左上方点 右上方点  右下方点 左下方点 的灰度梯度信息 以及 相应位置差值作为权重值 线性加权获取　亚像素梯度信息
*               resInterp  =  dxdy * *(const Eigen::Vector3f*)(bp+1+width)// 右下方点
*                          + (dy-dxdy) * *(const Eigen::Vector3f*)(bp+width)// 左下方点
*                          + (dx-dxdy) * *(const Eigen::Vector3f*)(bp+1)// 右上方点
*                          + (1-dx-dy+dxdy) * *(const Eigen::Vector3f*)(bp);// 左上方点
* 
*        需要注意的是，在给梯度变量赋值的时候，这里乘了焦距
* 　　　　这里求得　亚像素梯度之后　又乘上了　相机内参数，因为在求　误差偏导数时需要　需要用到　dx*fx　　dy*fy  
*               *(buf_warped_dx+idx) = fx_l * resInterp[0];// 当前帧匹配点亚像素 梯度　gx = dx*fx  
*               *(buf_warped_dy+idx) = fy_l * resInterp[1];// 匹配点亚像素 梯度               gy = dy*fy
*               *(buf_warped_residual+idx) = residual;// 对应 匹配点 像素误差
*               这里的　  dx= resInterp[0],  dy =  resInterp[0] 亚像素梯度
* 
*                (buf_d+idx) = 1.0f / (*refPoint)[2]       ;// 参考帧 Z轴 倒数  = 参考帧逆深度
*               *(buf_idepthVar+idx) = (*refColVar)[1];// 参考帧逆深度方差
* 　　　　  4.  记录变换　
* 　　　　　　　a. 对参考帧灰度　进行一次仿射变换后　和　当前帧亚像素灰度做差得到　残差(灰度误差)
*                  现在求得的残差只是纯粹的光度误差
* 　　　　　　　　   float c1 = affineEstimation_a * (*refColVar)[0] + affineEstimation_b;// 参考帧 灰度[0]  仿射变换后
*                   float c2 = resInterp[2];//  当前帧 亚像素 第三维度是图像 灰度值
*                  float residual = c1 - c2;// 匹配点对 的灰度  误差值　　
*                                      
*               疑问1：代码中对参考帧的灰度做了一个仿射变换的处理，这里的原理是什么？
*                     在SVO代码中的确有考虑到两帧见位姿相差过大，因此通过在空间上的仿射变换之后再求灰度的操作。
*                     但是在这里的代码中没有看出具体原理。
*             b. 对两帧下　3d点坐标的　Z轴深度值　的变化比例
*                     float depthChange = (*refPoint)[2] / Wxp[2];
*                     usageCount += depthChange < 1 ? depthChange : 1;//   记录深度改变的比例  
*　　　　　　  c. 匹配点总数
*                  buf_warped_size = idx;// 匹配点数量＝　包括好的匹配点　和　不好的匹配点　数量　　　　　　　　　
*         Step3: 计算 方差归一化加权残差和　使用误差方差　对　误差归一化 后求和 (calcWeightsAndResidual)
*              计算误差权重，归一化方差以及Huber-weight，最终把这个系数存在数组　buf_weight_p　中
*　　　　　　   每个像素点都有一个光度匹配误差，而每个点匹配的可信度根据其偏导数可以求得，
*　　　　　　   加权每一个误差　然后求所有点　光度匹配误差的均值
* 　　　　      误差导数　drpdd = dr= -(dx*fx　*　(tx*z' - tz*x')/(z'^2*d)  + dy*fy　*　(ty*z' - tz*y')/(z'^2*d))
*                                = -(gx　*　(tx*z' - tz*x')/(z'^2*d)  + gy　*　(ty*z' - tz*y')/(z'^2*d))
*                      dx, dy 为参考帧3d点映射到当前帧图像平面后的梯度
*             　　　　　fx,  fy 相机内参数
*             　　　　　tx,ty,tz 为参考帧到当前帧的平移变换(忽略旋转变换)　
                                t  = translation()
*            　　　　　 x',y',z'  为参考帧3d点变换到当前帧坐标系下的3d点     
                                 x' = Wxp[0] , y' = Wxp[1] , z' = Wxp[2] 
*             　　　　　d = d = *(buf_d) 为参考帧3d点在参考帧下的逆深度
*                      gx = *(buf_warped_dx)
*                      gy = *(buf_warped_dy)
*              方差平方   float s = settings.var_weight  *  *(buf_idepthVar+i);//    参考帧 逆深度方差  平方
* 　           误差权重为加权方差平方的倒数  float w_p = 1.0f / ((cameraPixelNoise2) + s * drpdd * drpdd);// 误差权重 方差倒数
*              初步误差　　　　             rp =*( buf_warped_residual) ; //初步误差
*              加权的误差                  float weighted_rp = fabs(rp*sqrtf(w_p));// |r|加权的误差
*              Huber-weight权重  
*              float wh = fabs(weighted_rp < (settings.huber_d/2) ? 1 : (settings.huber_d/2) / weighted_rp);
*              记录　*(buf_weight_p+i) = wh * w_p;    // 乘上 Huber-weight 权重 得到最终误差权重避免外点的影响
* 　　　       求和　 sumRes += wh * w_p * rp*rp;　// 加权误差和
*             取均值　return sumRes / buf_warped_size;// 返回加权误差均值　　　　　　　　　
		lastErr =  sumRes / buf_warped_size;
*      Step4: 计算雅克比向量以及A和b(calculateWarpUpdate)
* 　　　　　　J转置*J * dse3 = -J转置*w*lastErr
*  　　　　　  对于参考帧中的一个3D点pi位置处的残差求雅可比，这里需要使用链式求导法则 
*                       Ji =      1/z'*dx*fx + 0* dy *fy
*                                  0* dx *fx +  1/z' *dy *fy
*                         -1 *    -x'/z'^2 *dx*fx  - y'/z'^2 *dy*fy 
*                                 -x'*y'/z'^2 *dx*fx - (1+y'^2/z'^2) *dy*fy
*                                 (1+x'^2/z'^2)*dx*fx + x'*y'/z'^2*dy*fy
*                                - y'/z' *dx*fx + x'/z' * dy*fy
*
*                       A = J *J转置*(*(buf_weight_p+i))
*                       b = -J转置*(*(buf_weight_p+i))*lastErr
* 
*      Step5: 计算得到收敛的delta，并且更新SE3(dse3 = A.ldlt().solve(b))
* 　　　　 for(int i=0;i<6;i++) A(i,i) *= 1+LM_lambda;// (A+λI)  　列文伯格马尔夸克更新算法　LM 调整量　
*          Vector6 inc = A.ldlt().solve(b);// LDLT矩阵分解求解　dse3 李代数更新量
　　　　　　Sophus::SE3f new_referenceToFrame = Sophus::SE3f::exp((inc)) * referenceToFrame;　
* 　　　　
*      重复Step2-Step5直到收敛或者达到最大迭代次数
* 
* 计算下一层金字塔
* 
*/

#include "SE3Tracker.h"
#include <opencv2/highgui/highgui.hpp>
#include "DataStructures/Frame.h"
#include "Tracking/TrackingReference.h"
#include "util/globalFuncs.h"
#include "IOWrapper/ImageDisplay.h"
#include "Tracking/least_squares.h"

#include <Eigen/Core>

namespace lsd_slam
{

// 指令集优化 X86 SSE指令集优化  ARM NENO指令集优化
// 通过callOptimized这个宏调用 calcResidualAndBuffers 这个函数进行优化操作
#if defined(ENABLE_NEON)
	#define callOptimized(function, arguments) function##NEON arguments
#else
	#if defined(ENABLE_SSE)
		#define callOptimized(function, arguments) (USESSE ? function##SSE arguments : function arguments)
	#else
		#define callOptimized(function, arguments) function arguments
	#endif
#endif

// 使用图像存储 参数和 相机内参数初始化
SE3Tracker::SE3Tracker(int w, int h, Eigen::Matrix3f K)
{
  // 图像尺寸
	width = w;
	height = h;
// 相机内参数
	this->K = K;
	fx = K(0,0);
	fy = K(1,1);
	cx = K(0,2);
	cy = K(1,2);
// 系统全局配置文件 稠密深度跟踪器设置类
	// 定义了一些Tracking相关的设定，比如最大迭代次数，
	// 认为收敛的阈值(百分比形式，有些数据认为98%收敛，有些是99%)，
	// 还有huber距离所需参数等
	settings = DenseDepthTrackerSettings();
	//settings.maxItsPerLvl[0] = 2;
// 相机内参数 逆  倒数
	KInv = K.inverse();
	fxi = KInv(0,0);
	fyi = KInv(1,1);
	cxi = KInv(0,2);
	cyi = KInv(1,2);

// 分配内存
	buf_warped_residual = (float*)Eigen::internal::aligned_malloc(w*h*sizeof(float));
	// 梯度
	buf_warped_dx = (float*)Eigen::internal::aligned_malloc(w*h*sizeof(float));
	buf_warped_dy = (float*)Eigen::internal::aligned_malloc(w*h*sizeof(float));
	// 坐标
	buf_warped_x = (float*)Eigen::internal::aligned_malloc(w*h*sizeof(float));
	buf_warped_y = (float*)Eigen::internal::aligned_malloc(w*h*sizeof(float));
	buf_warped_z = (float*)Eigen::internal::aligned_malloc(w*h*sizeof(float));
       // 逆深度
	buf_d = (float*)Eigen::internal::aligned_malloc(w*h*sizeof(float));
	// 逆深度方差
	buf_idepthVar = (float*)Eigen::internal::aligned_malloc(w*h*sizeof(float));
	// 计算权重
	buf_weight_p = (float*)Eigen::internal::aligned_malloc(w*h*sizeof(float));

	buf_warped_size = 0;
	
// 可视化Tracking的迭代过程 需要的 图像
	debugImageWeights = cv::Mat(height,width,CV_8UC3);
	debugImageResiduals = cv::Mat(height,width,CV_8UC3);// 残差
	debugImageSecondFrame = cv::Mat(height,width,CV_8UC3);
	debugImageOldImageWarped = cv::Mat(height,width,CV_8UC3);
	debugImageOldImageSource = cv::Mat(height,width,CV_8UC3);
	
	lastResidual = 0;
	iterationNumber = 0;
	pointUsage = 0;
	lastGoodCount = lastBadCount = 0;// 计数器

	diverged = false;
}
// 类析构函数
SE3Tracker::~SE3Tracker()
{
  // cv::Mat 对象 自带的 释放存储空间的 方法
	debugImageResiduals.release();
	debugImageWeights.release();
	debugImageSecondFrame.release();
	debugImageOldImageSource.release();
	debugImageOldImageWarped.release();
// Eigen对象的 内存释放 方法
	Eigen::internal::aligned_free((void*)buf_warped_residual);
	Eigen::internal::aligned_free((void*)buf_warped_dx);// 梯度
	Eigen::internal::aligned_free((void*)buf_warped_dy);
	Eigen::internal::aligned_free((void*)buf_warped_x);// 坐标
	Eigen::internal::aligned_free((void*)buf_warped_y);
	Eigen::internal::aligned_free((void*)buf_warped_z);

	Eigen::internal::aligned_free((void*)buf_d);// 逆深度
	Eigen::internal::aligned_free((void*)buf_idepthVar);// 逆深度方差
	Eigen::internal::aligned_free((void*)buf_weight_p);// 权重
}

// tracks a frame.
// first_frame has depth, second_frame DOES NOT have depth.
// 第一帧 参考帧 3d点 按照 欧式变换矩阵 以及当前帧的第4层相机内参数 变换到图像平面下
// 看映射后的坐标是否合理，以及看两个坐标系下点坐标的 Z轴深度信息的变化
float SE3Tracker::checkPermaRefOverlap(
		Frame* reference,
		SE3 referenceToFrameOrg)
{
	Sophus::SE3f referenceToFrame = referenceToFrameOrg.cast<float>();// 欧式变换矩阵
	// 上锁
	boost::unique_lock<boost::mutex> lock2 = boost::unique_lock<boost::mutex>(reference->permaRef_mutex);

	int w2 = reference->width(QUICK_KF_CHECK_LVL)-1;// 第4层
	int h2 = reference->height(QUICK_KF_CHECK_LVL)-1;
	Eigen::Matrix3f KLvl = reference->K(QUICK_KF_CHECK_LVL);
	float fx_l = KLvl(0,0);// 第四层 相机内参数 
	float fy_l = KLvl(1,1);
	float cx_l = KLvl(0,2);
	float cy_l = KLvl(1,2);

	Eigen::Matrix3f rotMat = referenceToFrame.rotationMatrix();// 旋转矩阵
	Eigen::Vector3f transVec = referenceToFrame.translation();// 平移向量
	
	const Eigen::Vector3f* refPoint = reference->permaRef_posData;// 参考帧3d点   存储的起始指针
	const Eigen::Vector3f* refPoint_max = reference->permaRef_posData + reference->permaRefNumPts;// 最大位置


	float usageCount = 0;
	for(;refPoint<refPoint_max; refPoint++)
	{
		Eigen::Vector3f Wxp = rotMat * (*refPoint) + transVec;// 变换到当前图像坐标系下 的坐标
		float u_new = (Wxp[0]/Wxp[2])*fx_l + cx_l;// 对应图像上的像素坐标
		float v_new = (Wxp[1]/Wxp[2])*fy_l + cy_l;
		if((u_new > 0 && v_new > 0 && u_new < w2 && v_new < h2))//3d点 映射到 图像平面上 2d点坐标需要在合理范围内
		{
			float depthChange = (*refPoint)[2] / Wxp[2];// 两个坐标系下 逆深度的变换比例
			usageCount += depthChange < 1 ? depthChange : 1;// 按照最大值1  记录总和
		}
	}
	pointUsage = usageCount / (float)reference->permaRefNumPts;//  变换 均值
	return pointUsage;
}


// tracks a frame.
// first_frame has depth, second_frame DOES NOT have depth.
SE3 SE3Tracker::trackFrameOnPermaref(
		Frame* reference,
		Frame* frame,
		SE3 referenceToFrameOrg)
{

	Sophus::SE3f referenceToFrame = referenceToFrameOrg.cast<float>();

	boost::shared_lock<boost::shared_mutex> lock = frame->getActiveLock();
	boost::unique_lock<boost::mutex> lock2 = boost::unique_lock<boost::mutex>(reference->permaRef_mutex);

	affineEstimation_a = 1; affineEstimation_b = 0;

	NormalEquationsLeastSquares ls;
	diverged = false;
	trackingWasGood = true;

	callOptimized(calcResidualAndBuffers, (reference->permaRef_posData, reference->permaRef_colorAndVarData, 0, reference->permaRefNumPts, frame, referenceToFrame, QUICK_KF_CHECK_LVL, false));
	if(buf_warped_size < MIN_GOODPERALL_PIXEL_ABSMIN * (width>>QUICK_KF_CHECK_LVL)*(height>>QUICK_KF_CHECK_LVL))
	{
		diverged = true;
		trackingWasGood = false;
		return SE3();
	}
	if(useAffineLightningEstimation)
	{
		affineEstimation_a = affineEstimation_a_lastIt;
		affineEstimation_b = affineEstimation_b_lastIt;
	}
	float lastErr = callOptimized(calcWeightsAndResidual,(referenceToFrame));

	float LM_lambda = settings.lambdaInitialTestTrack;

	for(int iteration=0; iteration < settings.maxItsTestTrack; iteration++)
	{
		callOptimized(calculateWarpUpdate,(ls));


		int incTry=0;
		while(true)
		{
			// solve LS system with current lambda
			Vector6 b = -ls.b;
			Matrix6x6 A = ls.A;
			for(int i=0;i<6;i++) A(i,i) *= 1+LM_lambda;
			Vector6 inc = A.ldlt().solve(b);
			incTry++;

			// apply increment. pretty sure this way round is correct, but hard to test.
			Sophus::SE3f new_referenceToFrame = Sophus::SE3f::exp((inc)) * referenceToFrame;

			// re-evaluate residual
			callOptimized(calcResidualAndBuffers, (reference->permaRef_posData, reference->permaRef_colorAndVarData, 0, reference->permaRefNumPts, frame, new_referenceToFrame, QUICK_KF_CHECK_LVL, false));
			if(buf_warped_size < MIN_GOODPERALL_PIXEL_ABSMIN * (width>>QUICK_KF_CHECK_LVL)*(height>>QUICK_KF_CHECK_LVL))
			{
				diverged = true;
				trackingWasGood = false;
				return SE3();
			}
			float error = callOptimized(calcWeightsAndResidual,(new_referenceToFrame));


			// accept inc?
			if(error < lastErr)
			{
				// accept inc
				referenceToFrame = new_referenceToFrame;
				if(useAffineLightningEstimation)
				{
					affineEstimation_a = affineEstimation_a_lastIt;
					affineEstimation_b = affineEstimation_b_lastIt;
				}
				// converged?
				if(error / lastErr > settings.convergenceEpsTestTrack)
					iteration = settings.maxItsTestTrack;


				lastErr = error;


				if(LM_lambda <= 0.2)
					LM_lambda = 0;
				else
					LM_lambda *= settings.lambdaSuccessFac;

				break;
			}
			else
			{
				if(!(inc.dot(inc) > settings.stepSizeMinTestTrack))
				{
					iteration = settings.maxItsTestTrack;
					break;
				}

				if(LM_lambda == 0)
					LM_lambda = 0.2;
				else
					LM_lambda *= std::pow(settings.lambdaFailFac, incTry);
			}
		}
	}

	lastResidual = lastErr;

	trackingWasGood = !diverged
			&& lastGoodCount / (frame->width(QUICK_KF_CHECK_LVL)*frame->height(QUICK_KF_CHECK_LVL)) > MIN_GOODPERALL_PIXEL
			&& lastGoodCount / (lastGoodCount + lastBadCount) > MIN_GOODPERGOODBAD_PIXEL;

	return toSophus(referenceToFrame);
}

////////////////////////////////////////////////////////////////////////////////////////
////////////////// 最重要的跟踪匹配　函数////////////////////////////////////////////
// SE3Tracker::trackFrame函数的主体是一个for循环，从图像金字塔的高层level-4开始遍历直到底层level-1。
// 在循环内实现加权高斯牛顿优化算法（Weighted Gauss-Newton Optimization），
// 其实就是增加了鲁棒函数的高斯牛顿。
// tracks a frame.
// first_frame has depth, second_frame DOES NOT have depth.
SE3 SE3Tracker::trackFrame(
		TrackingReference* reference,//  参考帧，第一帧　含有　逆深度信息
		Frame* frame,//  当前帧　第二帧　　没有深度　需要匹配后　获取　
		const SE3& frameToReference_initialEstimate)// 初始变换矩阵　　fram 到参考帧　的　变换矩阵
// 对于初始位姿，LSD-SLAM中使用了上一帧和参考帧间的位姿作为当前位姿估计的初值
{
// 内存上锁
	boost::shared_lock<boost::shared_mutex> lock = frame->getActiveLock();
	diverged = false;
	trackingWasGood = true;
	affineEstimation_a = 1; affineEstimation_b = 0;// 仿射变换参数
// 保存　跟踪　信息
	if(saveAllTrackingStages)
	{
		saveAllTrackingStages = false;
		saveAllTrackingStagesInternal = true;
	}
// 初始化　跟踪迭代信息	
	if (plotTrackingIterationInfo)
	{
		const float* frameImage = frame->image();// 当前帧　第０层　图像
		for (int row = 0; row < height; ++ row)// 每行
			for (int col = 0; col < width; ++ col)// 每列
			  // 按照初始图像像素灰度值　设置　颜色
				setPixelInCvMat(&debugImageSecondFrame,getGrayCvPixel(frameImage[col+row*width]), col, row, 1);
	}

	// ============ track frame ============
// 首先将初始估计记录下来(记录了参考帧到当前帧的 欧式变换矩阵)
	// 参考帧 到 当前帧　 fram　的　变换矩阵
	Sophus::SE3f referenceToFrame = frameToReference_initialEstimate.inverse().cast<float>();
	NormalEquationsLeastSquares ls;// 然后定义一个6自由度矩阵的误差判别计算对象ls
	
	int numCalcResidualCalls[PYRAMID_LEVELS];// cell数量  5层　金字塔
	int numCalcWarpUpdateCalls[PYRAMID_LEVELS];//　

	float last_residual = 0;// 最终的残差

// 函数的主体是一个for循环
	for(int lvl=SE3TRACKING_MAX_LEVEL-1;lvl >= SE3TRACKING_MIN_LEVEL;lvl--)// 在每一层金字塔上进行跟踪
	{
           // 从最高层的　金字塔图像(尺寸最小)　开始　向下计算
	     // 从图像金字塔的高层level-4开始遍历直到底层level-1
		numCalcResidualCalls[lvl] = 0;
		numCalcWarpUpdateCalls[lvl] = 0;
// 【１】 有参考帧逆深度信息和对应像素点坐标　计算　在参考帧坐标系下的3d 点
		reference->makePointCloud(lvl);
//【２】计算参考帧3d点变换到当前帧图像坐标系下的残差(灰度误差)　和　梯度(对应点像素梯度)　
		// 计算匹配　点对　像素误差　　在这个函数中，把　buf_warped　相关的参数全部更新，并且更新了上次的相似变换参数等
		callOptimized(calcResidualAndBuffers, (reference->posData[lvl], reference->colorAndVarData[lvl], SE3TRACKING_MIN_LEVEL == lvl ? reference->pointPosInXYGrid[lvl] : 0, reference->numData[lvl], frame, referenceToFrame, lvl, (plotTracking && lvl == SE3TRACKING_MIN_LEVEL)));
		// buf_warped_size 记录了　匹配点数量　包括好的匹配点　和　不好的匹配点　数量　(只要投影后在图像范围内就可以)
		if(buf_warped_size < MIN_GOODPERALL_PIXEL_ABSMIN * (width>>lvl)*(height>>lvl))
		{//　如果匹配点数量过少，已经少于了这一层图像像素数量的1%)，那么我们认为差别太大，Tracking失败，返回一个空的SE3
			diverged = true;
			trackingWasGood = false;// Tracking失败
			return SE3();// 返回一个空的SE3
		}
        // 使用　仿射变换参数
        // 如果使用了，那么把通过calcResidualAndBuffers函数更新的affineEstimation_a_lastIt以及affineEstimation_b_lastIt，赋值给仿射变换系数
		if(useAffineLightningEstimation)
		{
			affineEstimation_a = affineEstimation_a_lastIt;
			affineEstimation_b = affineEstimation_b_lastIt;
		}
// 【３】使用误差协方差(误差求导) 对方差归一化 后求和　调用calcWeightsAndResidual得到误差，并记录调用次数
              // 以及误差权重矩阵　　避免外点的影响 
		float lastErr = callOptimized(calcWeightsAndResidual,(referenceToFrame));

		numCalcResidualCalls[lvl]++;

		float LM_lambda = settings.lambdaInitial[lvl];// λ 　LM优化的　调整量
// 【４】每一层进行 LM优化　(A+λI)x=b　在最大迭代次数范围内进行优化　
		for(int iteration=0; iteration < settings.maxItsPerLvl[lvl]; iteration++)
		{
                 // 计算　加权平均误差　以及误差权重
			callOptimized(calculateWarpUpdate,(ls));

			numCalcWarpUpdateCalls[lvl]++;

			iterationNumber = iteration;//　迭代次数

			int incTry=0;
			while(true)
			{
				// solve LS system with current lambda
				Vector6 b = -ls.b;
				Matrix6x6 A = ls.A;
			// 列文伯格马尔夸克优化算法更新
				for(int i=0;i<6;i++) A(i,i) *= 1+LM_lambda;// (A+λI)  LM 调整量　
				Vector6 inc = A.ldlt().solve(b);// LDLT矩阵分解求解　dse3 李代数更新量
				incTry++;
                         // 对　变换矩阵　左乘　se3指数映射　进行　更新　
				// apply increment. pretty sure this way round is correct, but hard to test.
				Sophus::SE3f new_referenceToFrame = Sophus::SE3f::exp((inc)) * referenceToFrame;
				//Sophus::SE3f new_referenceToFrame = referenceToFrame * Sophus::SE3f::exp((inc));


				// re-evaluate residual
				callOptimized(calcResidualAndBuffers, (reference->posData[lvl], reference->colorAndVarData[lvl], SE3TRACKING_MIN_LEVEL == lvl ? reference->pointPosInXYGrid[lvl] : 0, reference->numData[lvl], frame, new_referenceToFrame, lvl, (plotTracking && lvl == SE3TRACKING_MIN_LEVEL)));
				if(buf_warped_size < MIN_GOODPERALL_PIXEL_ABSMIN* (width>>lvl)*(height>>lvl))
				{// 匹配点数量记录　包括好的匹配点　和　不好的匹配点　数量
				  // 匹配点数量少于阈值　　优化失败
					diverged = true;
					trackingWasGood = false;// 优化失败
					return SE3();//　返回空
				}
                               // 新变换下的误差
				float error = callOptimized(calcWeightsAndResidual,(new_referenceToFrame));
				numCalcResidualCalls[lvl]++;
				// accept inc?
		// 误差变小了
				if(error < lastErr)// 误差变小了
				{
					// accept inc
				       // 更新　新求解的　变换矩阵 
					referenceToFrame = new_referenceToFrame;
					if(useAffineLightningEstimation)
					{// 更新　仿射变换系数
						affineEstimation_a = affineEstimation_a_lastIt;
						affineEstimation_b = affineEstimation_b_lastIt;
					}
					// 打印调试信息
					if(enablePrintDebugInfo && printTrackingIterationInfo)
					{
						// debug output
						printf("(%d-%d): ACCEPTED increment of %f with lambda %.1f, residual: %f -> %f\n",
								lvl,iteration, sqrt(inc.dot(inc)), LM_lambda, lastErr, error);

						printf("         p=%.4f %.4f %.4f %.4f %.4f %.4f\n",
								referenceToFrame.log()[0],referenceToFrame.log()[1],referenceToFrame.log()[2],
								referenceToFrame.log()[3],referenceToFrame.log()[4],referenceToFrame.log()[5]);
					}

					// converged?
					if(error / lastErr > settings.convergenceEps[lvl])
					{
						if(enablePrintDebugInfo && printTrackingIterationInfo)
						{
							printf("(%d-%d): FINISHED pyramid level (last residual reduction too small).\n",
									lvl,iteration);
						}
						iteration = settings.maxItsPerLvl[lvl];
					}

					last_residual = lastErr = error;


					if(LM_lambda <= 0.2)
						LM_lambda = 0;
					else
						LM_lambda *= settings.lambdaSuccessFac;

					break;
				}
		// 误差反而没有减少
				else
				{
					if(enablePrintDebugInfo && printTrackingIterationInfo)
					{
						printf("(%d-%d): REJECTED increment of %f with lambda %.1f, (residual: %f -> %f)\n",
								lvl,iteration, sqrt(inc.dot(inc)), LM_lambda, lastErr, error);
					}

					if(!(inc.dot(inc) > settings.stepSizeMin[lvl]))
					{
						if(enablePrintDebugInfo && printTrackingIterationInfo)
						{
							printf("(%d-%d): FINISHED pyramid level (stepsize too small).\n",
									lvl,iteration);
						}
						iteration = settings.maxItsPerLvl[lvl];
						break;
					}
                                        // 调整 LM 参数
					if(LM_lambda == 0)
						LM_lambda = 0.2;
					else
						LM_lambda *= std::pow(settings.lambdaFailFac, incTry);
				}
			}
		}
	}

	if(plotTracking)
		Util::displayImage("TrackingResidual", debugImageResiduals, false);


	if(enablePrintDebugInfo && printTrackingIterationInfo)
	{
		printf("Tracking: ");
			for(int lvl=PYRAMID_LEVELS-1;lvl >= 0;lvl--)
			{
				printf("lvl %d: %d (%d); ",
					lvl,
					numCalcResidualCalls[lvl],
					numCalcWarpUpdateCalls[lvl]);
			}

		printf("\n");
	}

	saveAllTrackingStagesInternal = false;

	lastResidual = last_residual;

	trackingWasGood = !diverged
			&& lastGoodCount / (frame->width(SE3TRACKING_MIN_LEVEL)*frame->height(SE3TRACKING_MIN_LEVEL)) > MIN_GOODPERALL_PIXEL
			&& lastGoodCount / (lastGoodCount + lastBadCount) > MIN_GOODPERGOODBAD_PIXEL;

	if(trackingWasGood)
		reference->keyframe->numFramesTrackedOnThis++;

	frame->initialTrackedResidual = lastResidual / pointUsage;
	frame->pose->thisToParent_raw = sim3FromSE3(toSophus(referenceToFrame.inverse()),1);
	frame->pose->trackingParent = reference->keyframe->pose;
	return toSophus(referenceToFrame.inverse());
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// 每个像素点都有一个光度匹配误差，而每个点匹配的可信度根据其偏导数可以求得，
// 加权每一个误差　然后求所有点　光度匹配误差的均值
// 再者　计算误差的权重，在优化更新函数中作为权重
	
// 使用　误差方差对方差归一化 后求和
// 计算权重  和　像素匹配误差
// 该函数的功能是计算归一化方差的光度误差系数，也就是计算公式(14)，
// 并且乘以了Huber-weight，最终把这个系数存在数组buf_weight_p中。
// 可能是考虑参考帧到当前帧的位姿变换比较小，
// 所以作者只考虑了位移t而忽略旋转R。
// 这样使得式子(14)中的偏导的形式简单了很多。
// 这里主要求　误差函数的导数　即得到　误差的协方差矩阵
#if defined(ENABLE_SSE)
float SE3Tracker::calcWeightsAndResidualSSE(
		const Sophus::SE3f& referenceToFrame)
{
	const __m128 txs = _mm_set1_ps((float)(referenceToFrame.translation()[0]));
	const __m128 tys = _mm_set1_ps((float)(referenceToFrame.translation()[1]));
	const __m128 tzs = _mm_set1_ps((float)(referenceToFrame.translation()[2]));

	const __m128 zeros = _mm_set1_ps(0.0f);
	const __m128 ones = _mm_set1_ps(1.0f);


	const __m128 depthVarFacs = _mm_set1_ps((float)settings.var_weight);// float depthVarFac = var_weight;	// the depth var is over-confident. this is a constant multiplier to remedy that.... HACK
	const __m128 sigma_i2s = _mm_set1_ps((float)cameraPixelNoise2);


	const __m128 huber_res_ponlys = _mm_set1_ps((float)(settings.huber_d/2));

	__m128 sumResP = zeros;


	float sumRes = 0;

	for(int i=0;i<buf_warped_size-3;i+=4)
	{
//		float px = *(buf_warped_x+i);	// x'
//		float py = *(buf_warped_y+i);	// y'
//		float pz = *(buf_warped_z+i);	// z'
//		float d = *(buf_d+i);	// d
//		float rp = *(buf_warped_residual+i); // r_p
//		float gx = *(buf_warped_dx+i);	// \delta_x I
//		float gy = *(buf_warped_dy+i);  // \delta_y I
//		float s = depthVarFac * *(buf_idepthVar+i);	// \sigma_d^2


		// calc dw/dd (first 2 components):
		__m128 pzs = _mm_load_ps(buf_warped_z+i);	// z'
		__m128 pz2ds = _mm_rcp_ps(_mm_mul_ps(_mm_mul_ps(pzs, pzs), _mm_load_ps(buf_d+i)));  // 1 / (z' * z' * d)
		__m128 g0s = _mm_sub_ps(_mm_mul_ps(pzs, txs), _mm_mul_ps(_mm_load_ps(buf_warped_x+i), tzs));
		g0s = _mm_mul_ps(g0s,pz2ds); //float g0 = (tx * pz - tz * px) / (pz*pz*d);

		 //float g1 = (ty * pz - tz * py) / (pz*pz*d);
		__m128 g1s = _mm_sub_ps(_mm_mul_ps(pzs, tys), _mm_mul_ps(_mm_load_ps(buf_warped_y+i), tzs));
		g1s = _mm_mul_ps(g1s,pz2ds);

		 // float drpdd = gx * g0 + gy * g1;	// ommitting the minus
		__m128 drpdds = _mm_add_ps(
				_mm_mul_ps(g0s, _mm_load_ps(buf_warped_dx+i)),
				_mm_mul_ps(g1s, _mm_load_ps(buf_warped_dy+i)));

		 //float w_p = 1.0f / (sigma_i2 + s * drpdd * drpdd);
		__m128 w_ps = _mm_rcp_ps(_mm_add_ps(sigma_i2s,
				_mm_mul_ps(drpdds,
						_mm_mul_ps(drpdds,
								_mm_mul_ps(depthVarFacs,
										_mm_load_ps(buf_idepthVar+i))))));


		//float weighted_rp = fabs(rp*sqrtf(w_p));
		__m128 weighted_rps = _mm_mul_ps(_mm_load_ps(buf_warped_residual+i),
				_mm_sqrt_ps(w_ps));
		weighted_rps = _mm_max_ps(weighted_rps, _mm_sub_ps(zeros,weighted_rps));


		//float wh = fabs(weighted_rp < huber_res_ponly ? 1 : huber_res_ponly / weighted_rp);
		__m128 whs = _mm_cmplt_ps(weighted_rps, huber_res_ponlys);	// bitmask 0xFFFFFFFF for 1, 0x000000 for huber_res_ponly / weighted_rp
		whs = _mm_or_ps(
				_mm_and_ps(whs, ones),
				_mm_andnot_ps(whs, _mm_mul_ps(huber_res_ponlys, _mm_rcp_ps(weighted_rps))));




		// sumRes.sumResP += wh * w_p * rp*rp;
		if(i+3 < buf_warped_size)
			sumResP = _mm_add_ps(sumResP,
					_mm_mul_ps(whs, _mm_mul_ps(weighted_rps, weighted_rps)));

		// *(buf_weight_p+i) = wh * w_p;
		_mm_store_ps(buf_weight_p+i, _mm_mul_ps(whs, w_ps) );
	}
	sumRes = SSEE(sumResP,0) + SSEE(sumResP,1) + SSEE(sumResP,2) + SSEE(sumResP,3);

	return sumRes / ((buf_warped_size >> 2)<<2);
}
#endif
	
#if defined(ENABLE_NEON)
float SE3Tracker::calcWeightsAndResidualNEON(
		const Sophus::SE3f& referenceToFrame)
{
	float tx = referenceToFrame.translation()[0];
	float ty = referenceToFrame.translation()[1];
	float tz = referenceToFrame.translation()[2];


	float constants[] = {
		tx, ty, tz, settings.var_weight,
		cameraPixelNoise2, settings.huber_d/2, -1, -1 // last values are currently unused
	};
	// This could also become a constant if one register could be made free for it somehow
	float cutoff_res_ponly4[4] = {10000, 10000, 10000, 10000}; // removed
	float* cur_buf_warped_z = buf_warped_z;
	float* cur_buf_warped_x = buf_warped_x;
	float* cur_buf_warped_y = buf_warped_y;
	float* cur_buf_warped_dx = buf_warped_dx;
	float* cur_buf_warped_dy = buf_warped_dy;
	float* cur_buf_warped_residual = buf_warped_residual;
	float* cur_buf_d = buf_d;
	float* cur_buf_idepthVar = buf_idepthVar;
	float* cur_buf_weight_p = buf_weight_p;
	int loop_count = buf_warped_size / 4;
	int remaining = buf_warped_size - 4 * loop_count;
	float sum_vector[] = {0, 0, 0, 0};
	
	float sumRes=0;

	
#ifdef DEBUG
	loop_count = 0;
	remaining = buf_warped_size;
#else
	if (loop_count > 0)
	{
		__asm__ __volatile__
		(
			// Extract constants
			"vldmia   %[constants], {q8-q9}              \n\t" // constants(q8-q9)
			"vdup.32  q13, d18[0]                        \n\t" // extract sigma_i2 x 4 to q13
			"vdup.32  q14, d18[1]                        \n\t" // extract huber_res_ponly x 4 to q14
			//"vdup.32  ???, d19[0]                        \n\t" // extract cutoff_res_ponly x 4 to ???
			"vdup.32  q9, d16[0]                         \n\t" // extract tx x 4 to q9, overwrite!
			"vdup.32  q10, d16[1]                        \n\t" // extract ty x 4 to q10
			"vdup.32  q11, d17[0]                        \n\t" // extract tz x 4 to q11
			"vdup.32  q8, d17[1]                         \n\t" // extract depthVarFac x 4 to q8, overwrite!
			
			"veor     q15, q15, q15                      \n\t" // set sumRes.sumResP(q15) to zero (by xor with itself)
			".loopcalcWeightsAndResidualNEON:            \n\t"
			
				"vldmia   %[buf_idepthVar]!, {q7}           \n\t" // s(q7)
				"vldmia   %[buf_warped_z]!, {q2}            \n\t" // pz(q2)
				"vldmia   %[buf_d]!, {q3}                   \n\t" // d(q3)
				"vldmia   %[buf_warped_x]!, {q0}            \n\t" // px(q0)
				"vldmia   %[buf_warped_y]!, {q1}            \n\t" // py(q1)
				"vldmia   %[buf_warped_residual]!, {q4}     \n\t" // rp(q4)
				"vldmia   %[buf_warped_dx]!, {q5}           \n\t" // gx(q5)
				"vldmia   %[buf_warped_dy]!, {q6}           \n\t" // gy(q6)
		
				"vmul.f32 q7, q7, q8                        \n\t" // s *= depthVarFac
				"vmul.f32 q12, q2, q2                       \n\t" // pz*pz (q12, temp)
				"vmul.f32 q3, q12, q3                       \n\t" // pz*pz*d (q3)
		
				"vrecpe.f32 q3, q3                          \n\t" // 1/(pz*pz*d) (q3)
				"vmul.f32 q12, q9, q2                       \n\t" // tx*pz (q12)
				"vmls.f32 q12, q11, q0                      \n\t" // tx*pz - tz*px (q12) [multiply and subtract] {free: q0}
				"vmul.f32 q0, q10, q2                       \n\t" // ty*pz (q0) {free: q2}
				"vmls.f32 q0, q11, q1                       \n\t" // ty*pz - tz*py (q0) {free: q1}
				"vmul.f32 q12, q12, q3                      \n\t" // g0 (q12)
				"vmul.f32 q0, q0, q3                        \n\t" // g1 (q0)
		
				"vmul.f32 q12, q12, q5                      \n\t" // gx * g0 (q12) {free: q5}
				"vldmia %[cutoff_res_ponly4], {q5}          \n\t" // cutoff_res_ponly (q5), load for later
				"vmla.f32 q12, q6, q0                       \n\t" // drpdd = gx * g0 + gy * g1 (q12) {free: q6, q0}
				
				"vmov.f32 q1, #1.0                          \n\t" // 1.0 (q1), will be used later
		
				"vmul.f32 q12, q12, q12                     \n\t" // drpdd*drpdd (q12)
				"vmul.f32 q12, q12, q7                      \n\t" // s*drpdd*drpdd (q12)
				"vadd.f32 q12, q12, q13                     \n\t" // sigma_i2 + s*drpdd*drpdd (q12)
				"vrecpe.f32 q12, q12                        \n\t" // w_p = 1/(sigma_i2 + s*drpdd*drpdd) (q12) {free: q7}
		
				// float weighted_rp = fabs(rp*sqrtf(w_p));
				"vrsqrte.f32 q7, q12                        \n\t" // 1 / sqrtf(w_p) (q7)
				"vrecpe.f32 q7, q7                          \n\t" // sqrtf(w_p) (q7)
				"vmul.f32 q7, q7, q4                        \n\t" // rp*sqrtf(w_p) (q7)
				"vabs.f32 q7, q7                            \n\t" // weighted_rp (q7)
		
				// float wh = fabs(weighted_rp < huber_res_ponly ? 1 : huber_res_ponly / weighted_rp);
				"vrecpe.f32 q6, q7                          \n\t" // 1 / weighted_rp (q6)
				"vmul.f32 q6, q6, q14                       \n\t" // huber_res_ponly / weighted_rp (q6)
				"vclt.f32 q0, q7, q14                       \n\t" // weighted_rp < huber_res_ponly ? all bits 1 : all bits 0 (q0)
				"vbsl     q0, q1, q6                        \n\t" // sets elements in q0 to 1(q1) where above condition is true, and to q6 where it is false {free: q6}
				"vabs.f32 q0, q0                            \n\t" // wh (q0)
		
				// sumRes.sumResP += wh * w_p * rp*rp
				"vmul.f32 q4, q4, q4                        \n\t" // rp*rp (q4)
				"vmul.f32 q4, q4, q12                       \n\t" // w_p*rp*rp (q4)
				"vmla.f32 q15, q4, q0                       \n\t" // sumRes.sumResP += wh*w_p*rp*rp (q15) {free: q4}
				
				// if(weighted_rp > cutoff_res_ponly)
				//     wh = 0;
				// *(buf_weight_p+i) = wh * w_p;
				"vcle.f32 q4, q7, q5                        \n\t" // mask in q4: ! (weighted_rp > cutoff_res_ponly)
				"vmul.f32 q0, q0, q12                       \n\t" // wh * w_p (q0)
				"vand     q0, q0, q4                        \n\t" // set q0 to 0 where condition for q4 failed (i.e. weighted_rp > cutoff_res_ponly)
				"vstmia   %[buf_weight_p]!, {q0}            \n\t"
				
			"subs     %[loop_count], %[loop_count], #1    \n\t"
			"bne      .loopcalcWeightsAndResidualNEON     \n\t"
				
			"vstmia   %[sum_vector], {q15}                \n\t"

		: /* outputs */ [buf_warped_z]"+&r"(cur_buf_warped_z),
						[buf_warped_x]"+&r"(cur_buf_warped_x),
						[buf_warped_y]"+&r"(cur_buf_warped_y),
						[buf_warped_dx]"+&r"(cur_buf_warped_dx),
						[buf_warped_dy]"+&r"(cur_buf_warped_dy),
						[buf_d]"+&r"(cur_buf_d),
						[buf_warped_residual]"+&r"(cur_buf_warped_residual),
						[buf_idepthVar]"+&r"(cur_buf_idepthVar),
						[buf_weight_p]"+&r"(cur_buf_weight_p),
						[loop_count]"+&r"(loop_count)
		: /* inputs  */ [constants]"r"(constants),
						[cutoff_res_ponly4]"r"(cutoff_res_ponly4),
						[sum_vector]"r"(sum_vector)
		: /* clobber */ "memory", "cc",
						"q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7", "q8", "q9", "q10", "q11", "q12", "q13", "q14", "q15"
		);
		
		sumRes += sum_vector[0] + sum_vector[1] + sum_vector[2] + sum_vector[3];
	}
#endif

	for(int i=buf_warped_size-remaining; i<buf_warped_size; i++)
	{
		float px = *(buf_warped_x+i);	// x'
		float py = *(buf_warped_y+i);	// y'
		float pz = *(buf_warped_z+i);	// z'
		float d = *(buf_d+i);	// d
		float rp = *(buf_warped_residual+i); // r_p
		float gx = *(buf_warped_dx+i);	// \delta_x I
		float gy = *(buf_warped_dy+i);  // \delta_y I
		float s = settings.var_weight * *(buf_idepthVar+i);	// \sigma_d^2


		// calc dw/dd (first 2 components):
		float g0 = (tx * pz - tz * px) / (pz*pz*d);
		float g1 = (ty * pz - tz * py) / (pz*pz*d);


		// calc w_p
		float drpdd = gx * g0 + gy * g1;	// ommitting the minus
		float w_p = 1.0f / (cameraPixelNoise2 + s * drpdd * drpdd);
		float weighted_rp = fabs(rp*sqrtf(w_p));

		float wh = fabs(weighted_rp < (settings.huber_d/2) ? 1 : (settings.huber_d/2) / weighted_rp);

		sumRes += wh * w_p * rp*rp;

		*(buf_weight_p+i) = wh * w_p;
	}

	return sumRes / buf_warped_size;
}
#endif
///////////////////////////////////////////////////////////////////
// 每个像素点都有一个光度匹配误差，而每个点匹配的可信度根据其偏导数可以求得，
// 加权每一个误差　然后求所有点　光度匹配误差的均值
// 再者　计算误差的权重，在优化更新函数中作为权重
	
// 使用　误差方差对方差归一化 后求和
// 计算权重  和　像素匹配误差
// 该函数的功能是计算归一化方差的光度误差系数，也就是计算公式(14)，
// 并且乘以了Huber-weight，最终把这个系数存在数组buf_weight_p中。
// 可能是考虑参考帧到当前帧的位姿变换比较小，
// 所以作者只考虑了位移t而忽略旋转R。
// 这样使得式子(14)中的偏导的形式简单了很多。
// 这里主要求　误差函数的导数　即得到　误差的协方差矩阵
float SE3Tracker::calcWeightsAndResidual(
		const Sophus::SE3f& referenceToFrame)// 参考帧　到　当前帧　欧式变换矩阵
{
  // 参考帧　到　当前帧　平移向量
	float tx = referenceToFrame.translation()[0];
	float ty = referenceToFrame.translation()[1];
	float tz = referenceToFrame.translation()[2];

	float sumRes = 0;
// 每个像素点都有一个光度匹配误差，而每个点匹配的可信度根据其偏导数可以求得，
// 加权每一个误差　然后求所有点　光度匹配误差的均值 
	for(int i=0;i<buf_warped_size;i++)// 对于初步匹配得　每一个　匹配点
	{
	  // 参考帧　映射到　当前帧　坐标系下的　3d 坐标
		float px = *(buf_warped_x+i);	// x'
		float py = *(buf_warped_y+i);	// y'
		float pz = *(buf_warped_z+i);	// z'
		
		float d = *(buf_d+i);	// d  　　　　　　　　	参考帧 Z轴 倒数  = 参考帧 逆深度
		float rp = *(buf_warped_residual+i); // r_p 　　	匹配点对 像素匹配误差
		
		float gx = *(buf_warped_dx+i);	// \delta_x I　	gx = dx*fx当前帧　亚像素梯度值gx仿射变换后(  乘以　相机内参数)
		float gy = *(buf_warped_dy+i);  // \delta_y I       	gy = gy*fy
		
		float s = settings.var_weight  *  *(buf_idepthVar+i);	// \sigma_d^2    参考帧 逆深度 方差  平方
// 参考帧3D点通过Rt映射到当前帧坐标系下在通过相机内参数映射到当前帧图像平面上
// 的到所谓的光度误差　该误差对参考帧p点处的逆深度D求偏导数得到
// dE =  -(dx*fx　*　(tx*z' - tz*x')/(z'^2*d)  + dy*fy　*　(ty*z' - tz*y')/(z'^2*d))

		// calc dw/dd (first 2 components):
		float g0 = (tx * pz - tz * px) / (pz*pz*d);//   
		float g1 = (ty * pz - tz * py) / (pz*pz*d);
		
		// 误差　偏导数的结果　calc w_p
		// 正如源码所注释，这里省略了负号
		float drpdd = gx * g0 + gy * g1;	// ommitting the minus
		
		float w_p = 1.0f / ((cameraPixelNoise2) + s * drpdd * drpdd);// 误差权重 方差倒数

		float weighted_rp = fabs(rp*sqrtf(w_p));// |r|加权的误差
		// 为了减少外点（outliers）对算法的影响，论文中使用了迭代变权重最小二乘
                // 其实在实现的时候，这里给的权重就是Huber-weight。 
		float wh = fabs(weighted_rp < (settings.huber_d/2) ? 1 : (settings.huber_d/2) / weighted_rp);

		//sumRes += wh * w_p * rp*rp;
		//*(buf_weight_p+i) = wh * w_p;// // 乘上 Huber-weight 权重 的最终误差
		*(buf_weight_p+i) = wh * w_p;// // 乘上 Huber-weight 权重 的最终误差
		sumRes += *(buf_weight_p+i) * rp*rp;// 加权误差和
		
	}

	return sumRes / buf_warped_size;// 返回加权误差均值
}
/////////////////////////////////////////////////////////////////////////////////////////////////


// 如果要可视化Tracking的迭代过程，那么第一步自然是把debug相关的参数都设置进去，
// 否则直接进行下一步，这个操作是通过调用calcResidualAndBuffers_debugStart()实现的
void SE3Tracker::calcResidualAndBuffers_debugStart()
{
  // 是否需要 可视化Tracking的迭代过程
	if(plotTrackingIterationInfo || saveAllTrackingStagesInternal)
	{
		int other = saveAllTrackingStagesInternal ? 255 : 0;
		// 填充图像
		fillCvMat(&debugImageResiduals,cv::Vec3b(other,other,255));
		fillCvMat(&debugImageWeights,cv::Vec3b(other,other,255));
		fillCvMat(&debugImageOldImageSource,cv::Vec3b(other,other,255));
		fillCvMat(&debugImageOldImageWarped,cv::Vec3b(other,other,255));
	}
}

// 完成匹配误差计算  保存误差信息图片
void SE3Tracker::calcResidualAndBuffers_debugFinish(int w)
{
  // 显示跟踪 优化迭代信息
	if(plotTrackingIterationInfo)
	{
		Util::displayImage( "Weights", debugImageWeights );
		Util::displayImage( "second_frame", debugImageSecondFrame );
		Util::displayImage( "Intensities of second_frame at transformed positions", debugImageOldImageSource );
		Util::displayImage( "Intensities of second_frame at pointcloud in first_frame", debugImageOldImageWarped );
		Util::displayImage( "Residuals", debugImageResiduals );

		// wait for key and handle it
		bool looping = true;
		while(looping)
		{
			int k = Util::waitKey(1);
			if(k == -1)
			{
				if(autoRunWithinFrame)
					break;
				else
					continue;
			}

			char key = k;
			if(key == ' ')
				looping = false;
			else
				handleKey(k);// 处理
		}
	}
// 保存跟踪迭代信息　图像
	if(saveAllTrackingStagesInternal)
	{
		char charbuf[500];

		snprintf(charbuf,500,"save/%sresidual-%d-%d.png",packagePath.c_str(),w,iterationNumber);// 格式化字符串 图片名
		cv::imwrite(charbuf,debugImageResiduals);// 写图片  参差

		snprintf(charbuf,500,"save/%swarped-%d-%d.png",packagePath.c_str(),w,iterationNumber);
		cv::imwrite(charbuf,debugImageOldImageWarped);

		snprintf(charbuf,500,"save/%sweights-%d-%d.png",packagePath.c_str(),w,iterationNumber);
		cv::imwrite(charbuf,debugImageWeights);// 权重图片

		printf("saved three images for lvl %d, iteration %d\n",w,iterationNumber);// 打印信息
	}
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////  跟踪完成获取　变换矩阵　和　3d 点后 反投影到当前帧　　计算　灰度匹配误差 ////////////////////////////
// calcResidualAndBuffers  X86平台 SSE指令集优化
#if defined(ENABLE_SSE)
float SE3Tracker::calcResidualAndBuffersSSE(
		const Eigen::Vector3f* refPoint,
		const Eigen::Vector2f* refColVar,
		int* idxBuf,
		int refNum,
		Frame* frame,
		const Sophus::SE3f& referenceToFrame,
		int level,
		bool plotResidual)
{
	return calcResidualAndBuffers(refPoint, refColVar, idxBuf, refNum, frame, referenceToFrame, level, plotResidual);
}
#endif
// calcResidualAndBuffers ARM平台 NENO指令集优化
#if defined(ENABLE_NEON)
float SE3Tracker::calcResidualAndBuffersNEON(
		const Eigen::Vector3f* refPoint,
		const Eigen::Vector2f* refColVar,
		int* idxBuf,
		int refNum,
		Frame* frame,
		const Sophus::SE3f& referenceToFrame,
		int level,
		bool plotResidual)
{
	return calcResidualAndBuffers(refPoint, refColVar, idxBuf, refNum, frame, referenceToFrame, level, plotResidual);
}
#endif

// #ifdef的使用和#if defined()的用法一致
// #ifndef又和#if !defined()的用法一致。

// 优化相关的函数，参数共有8个，在后面可以看到，其他函数通过  callOptimized 这个宏调用了这个函数进行优化操作
// 匹配帧通过 变换矩阵 和 相机内参数 投影到 当前图像平面上(值为float小数)
// 而原图像上的像素点坐标为 整数值，每一个像素位置对应有，梯度信息
// 那么 浮点数像素坐标对应的像素 是多少呢，根据浮点数坐标 四周的四个整数点坐标的梯度 加权和　后计算　灰度匹配误差
// 根据变换矩阵和原3D点和灰度值　计算匹配点对之间得　像素匹配误差　　以及　匹配点对　好坏标志图
/*
 input:
 refPoint　　　 	 参考帧 3d坐标 起始 地址指针
 refColVar　　　	 参考帧 灰度和 逆深度方差 起始地址
 idxBuf　　　　	匹配点好坏标志图 指针
 frame　　　　	当前帧　指针
 referenceToFrame　参考帧　变换到　当前帧下得　欧式变换矩阵　引用　
 level　　　　　　　金字塔层级
 plotResidual　　　匹配误差图标志
 */
float SE3Tracker::calcResidualAndBuffers(
		const Eigen::Vector3f* refPoint,// 参考帧 3d坐标 起始 地址
		const Eigen::Vector2f* refColVar,// 参考帧 灰度和 逆深度方差 起始地址
		int* idxBuf,// 匹配点好坏标志图 指针
		int refNum,// 参考帧 3d点数量
		Frame* frame,// 当前帧
		const Sophus::SE3f& referenceToFrame,// 参考帧　变换到　当前帧下得　欧式变换矩阵　
		int level,// 金字塔层级
		bool plotResidual)//显示　匹配误差图标志
{
// 如果要可视化Tracking的迭代过程，那么第一步自然是把debug相关的参数都设置进去，
// 否则直接进行下一步，这个操作是通过调用calcResidualAndBuffers_debugStart()实现的
	calcResidualAndBuffers_debugStart();
// 然后判断是否可视化残差，如果需要可视化，那么初始化残差
	if(plotResidual)
		debugImageResiduals.setTo(0);
// 本地参数设置 
	int w = frame->width(level);// 图像尺寸
	int h = frame->height(level);
	Eigen::Matrix3f KLvl = frame->K(level);// 相机内参数
	float fx_l = KLvl(0,0);
	float fy_l = KLvl(1,1);
	float cx_l = KLvl(0,2);
	float cy_l = KLvl(1,2);

	Eigen::Matrix3f rotMat = referenceToFrame.rotationMatrix();// 旋转矩阵
	Eigen::Vector3f transVec = referenceToFrame.translation();// 平移向量
	
	const Eigen::Vector3f* refPoint_max = refPoint + refNum;// 参考帧的 3d点坐标 存储的最大位置

	const Eigen::Vector4f* frame_gradients = frame->gradients(level);// 当前帧 像素梯度 获取 关键点
// 然后定义后续所要使用的变量
	int idx=0;
	float sumResUnweighted = 0;
	bool* isGoodOutBuffer = idxBuf != 0 ? frame->refPixelWasGood() : 0;// 匹配点 对 匹配效果好坏
	int goodCount = 0;
	int badCount = 0;
	float sumSignedRes = 0;
	float sxx=0,syy=0,sx=0,sy=0,sw=0;
	float usageCount = 0;

	for(;refPoint<refPoint_max; refPoint++, refColVar++, idxBuf++)// 对于每一个 参考帧的 3d点坐标
	{
           // 参考帧3d点 R，t变换到 当前相机坐标系下
		Eigen::Vector3f Wxp = rotMat * (*refPoint) + transVec;
		//  在 投影到 当前相机 像素平面下
		float u_new = (Wxp[0]/Wxp[2])*fx_l + cx_l;// float 浮点数类型
		float v_new = (Wxp[1]/Wxp[2])*fy_l + cy_l;

		// step 1a: coordinates have to be in image:
		// (inverse test to exclude NANs)
		//  投影后点在 图像和合理范围内  判断当前点是否投影到图像中
		if(!(u_new > 1 && v_new > 1 && u_new < w-2 && v_new < h-2))
		{
			if(isGoodOutBuffer != 0)// 指针部位空
				isGoodOutBuffer[*idxBuf] = false;// 并标记
			continue;
		}
// 使用 当前点 右方点  右下方点 正下方点 的灰度梯度信息 以及 相应位置差值作为权重值 线性加权获取加权梯度信息
// 然后差值得到亚像素精度级别的 梯度(注意深度的第三个维度是图像数据)
// 浮点数像素坐标对应的像素 是多少呢，根据浮点数坐标 四周的四个整数点坐标的梯度 加权和
		Eigen::Vector3f resInterp = getInterpolatedElement43(frame_gradients, u_new, v_new, w);
		
// 之后把参考图像数据做一次 仿射操作，
		// 疑问1：代码中对参考帧的灰度做了一个仿射变换的处理，这里的原理是什么？
		//在SVO代码中的确有考虑到两帧见位姿相差过大，因此通过在空间上的仿射变换之后再求灰度的操作。
		// 但是在这里的代码中没有看出具体原理。
		float c1 = affineEstimation_a * (*refColVar)[0] + affineEstimation_b;// 参考帧 灰度[0]  ;   逆深度方差[1] 
		float c2 = resInterp[2];//  当前帧 亚像素 第三维度是图像 灰度值
		float residual = c1 - c2;// 匹配点对 的灰度  误差值
// 通过差值自适应算得权重     也就是说，误差越大(匹配效果差)，权重越小 
		float weight = fabsf(residual) < 5.0f ? 1 : 5.0f / fabsf(residual);// 误差倒数为权重，误差小于5的分子为1 ， 大于5的分子为5 
     // 这些参数用来迭代计算 下一次　的　仿射变换系数　
		sxx += c1*c1*weight;// 参考帧 灰度值平方和               带误差倒数 权重
		syy += c2*c2*weight;// 匹配帧 亚像素 灰度值平方 和 带误差倒数 权重
		sx += c1*weight;// 参考帧 灰度值 和                             带误差倒数 权重
		sy += c2*weight;// 匹配帧 亚像素 灰度值 和                带误差倒数 权重
		sw += weight;//  误差倒数 权重  和
		
// 然后判断这个匹配点是好是坏，也是个自适应的阈值，这个阈值为一个最大的差异常数，加上 梯度值平和 乘以一个比例系数，  
              // 这个和 匹配点对 的灰度误差值平方  比较，如果残差的平方小于它，那么认为这个点的估计比较好，然后再把这个判断赋值给isGoodOutBuffer[*idxBuf]
               // 匹配点对 的灰度  误差值 平和  小于  与 当前帧 点 的x和y方向梯度平和 相关数  时  为好的匹配点
		bool isGood = residual*residual / (MAX_DIFF_CONSTANT + MAX_DIFF_GRAD_MULT*(resInterp[0]*resInterp[0] + resInterp[1]*resInterp[1])) < 1;
		if(isGoodOutBuffer != 0)
			isGoodOutBuffer[*idxBuf] = isGood;// 记录 匹配点对好坏标志
// 之后记录计算得到的这一帧改变的值
		*(buf_warped_x+idx) = Wxp(0);// 参考帧 3d点 通过R,t变换矩阵 变换到 当前图像坐标系下的坐标值  X
		*(buf_warped_y+idx) = Wxp(1);// Y
		*(buf_warped_z+idx) = Wxp(2);// Z
		
// 这里求得　亚像素梯度之后　又乘上了　相机内参数，因为在求　误差偏导数时需要　需要用到　dx*fx　　dy*fy  
		// calcWeightsAndResidual 求误差导数来求　误差的协方差矩阵　后归一化误差
		*(buf_warped_dx+idx) = fx_l * resInterp[0];// 当前帧匹配点亚像素 梯度 gx = dx*fx
		*(buf_warped_dy+idx) = fy_l * resInterp[1];// 匹配点亚像素 梯度             gy = dy*fy  
		*(buf_warped_residual+idx) = residual;// 对应 匹配点 像素误差

		*(buf_d+idx) = 1.0f / (*refPoint)[2];// 参考帧 Z轴 倒数  = 参考帧逆深度
		*(buf_idepthVar+idx) = (*refColVar)[1];// 参考帧逆深度方差
		idx++;// 点 ++

// 之后再记录 匹配点像素误差的平方和，以 及匹配点像素误差值(带符号)
		if(isGood)
		{
			sumResUnweighted += residual*residual;// 较好的  匹配点像素误差的平方和
			sumSignedRes += residual;// 较好的 匹配点像素误差和
			goodCount++;// 较好的匹配点对 计数
		}
		else
			badCount++;// 不好的匹配点对 计数
// 匹配点 变换前后 深度得变换  checkPermaRefOverlap()函数也有类似的计算 
		float depthChange = (*refPoint)[2] / Wxp[2];	// if depth becomes larger: pixel becomes "smaller", hence count it less.
		usageCount += depthChange < 1 ? depthChange : 1;//   记录深度改变的比例

// 调试  如果设置了画图，就把他们可视化出来
		// DEBUG STUFF
		if(plotTrackingIterationInfo || plotResidual)
		{
			// for debug plot only: find x,y again.
			// horribly inefficient, but who cares at this point...
			Eigen::Vector3f point = KLvl * (*refPoint);// 参考帧下 3d点对应的 像素点2d坐标
			int x = point[0] / point[2] + 0.5f;
			int y = point[1] / point[2] + 0.5f;

			if(plotTrackingIterationInfo)
			{
			  // 设置参考帧和 当前帧 关键点的 图像  使用 亚像素灰度值作为 点颜色
				setPixelInCvMat(&debugImageOldImageSource,getGrayCvPixel((float)resInterp[2]),u_new+0.5,v_new+0.5,(width/w));// 当前图像下的 关键点2d 坐标
				setPixelInCvMat(&debugImageOldImageWarped,getGrayCvPixel((float)resInterp[2]),x,y,(width/w));// 参考帧下得 关键点2d 坐标
			}
			if(isGood)// 匹配点对灰度像素匹配误差较小  显示  匹配误差图像
				setPixelInCvMat(&debugImageResiduals,getGrayCvPixel(residual+128),x,y,(width/w));// 误差越小 颜色越靠中间颜色 
			else
				setPixelInCvMat(&debugImageResiduals,cv::Vec3b(0,0,255),x,y,(width/w));// 误差较大的点 显示蓝色 rgb

		}
	}

	buf_warped_size = idx;// 匹配点数量＝　包括好的匹配点　和　不好的匹配点　数量

	pointUsage = usageCount / (float)refNum;// 深度改变均值
	lastGoodCount = goodCount;// 好的匹配点计数
	lastBadCount = badCount;// 不好得匹配点计数
	lastMeanRes = sumSignedRes / goodCount;// 匹配点像素误差均值
	
// 计算迭代之后得到的　下一次得仿射变换系数
	affineEstimation_a_lastIt = sqrtf((syy - sy*sy/sw) / (sxx - sx*sx/sw));
	affineEstimation_b_lastIt = (sy - affineEstimation_a_lastIt*sx)/sw;

	calcResidualAndBuffers_debugFinish(w);// 主要用来　保存调试信息　图像

	return sumResUnweighted / goodCount; // 匹配点像素误差的平方和 均值
}

	
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 计算雅克比矩阵J　线性方程系数A,和偏置b，使用LDLT矩阵分解求解方程得到　李代数更新量 dse3
// 利用误差函数对误差向量求导数，求得各个点的误差之间的　协方差矩阵，也是雅克比矩阵Ｊ
// 是计算雅可比以及最小二乘方程的系数
// 对于参考帧中的一个3D点pi位置处的残差求雅可比，这里需要使用链式求导法则 
// Ji =      1/z' *dx *fx + 0* dy *fy
//                 0*dx *fx +  1/z' *dy *fy
//   -1 *   - x'/z'^2 *dx*fx  - y'/z'^2 *dy*fy 
//            - x'*y'/z'^2 *dx*fx - (1+y'^2/z'^2) *dy*fy
//             (1+x'^2/z'^2)*dx*fx + x'*y'/z'^2*dy*fy
//              - y'/z' *dx*fx + x'/z' * dy*fy
// A = J *J转置*(*(buf_weight_p+i))
// b = -J转置*(*(buf_weight_p+i))*lastErr
// Vector6 inc = A.ldlt().solve(b);// LDLT矩阵分解求解　dse3 李代数更新量
#if defined(ENABLE_SSE)
Vector6 SE3Tracker::calculateWarpUpdateSSE(
		NormalEquationsLeastSquares &ls)
{
	ls.initialize(width*height);

//	printf("wupd SSE\n");
	for(int i=0;i<buf_warped_size-3;i+=4)
	{
		Vector6 v1,v2,v3,v4;
		__m128 val1, val2, val3, val4;

		// redefine pz
		__m128 pz = _mm_load_ps(buf_warped_z+i);
		pz = _mm_rcp_ps(pz);						// pz := 1/z


		__m128 gx = _mm_load_ps(buf_warped_dx+i);
		val1 = _mm_mul_ps(pz, gx);			// gx / z => SET [0]
		//v[0] = z*gx;
		v1[0] = SSEE(val1,0);
		v2[0] = SSEE(val1,1);
		v3[0] = SSEE(val1,2);
		v4[0] = SSEE(val1,3);




		__m128 gy = _mm_load_ps(buf_warped_dy+i);
		val1 = _mm_mul_ps(pz, gy);					// gy / z => SET [1]
		//v[1] = z*gy;
		v1[1] = SSEE(val1,0);
		v2[1] = SSEE(val1,1);
		v3[1] = SSEE(val1,2);
		v4[1] = SSEE(val1,3);


		__m128 px = _mm_load_ps(buf_warped_x+i);
		val1 = _mm_mul_ps(px, gy);
		val1 = _mm_mul_ps(val1, pz);	//  px * gy * z
		__m128 py = _mm_load_ps(buf_warped_y+i);
		val2 = _mm_mul_ps(py, gx);
		val2 = _mm_mul_ps(val2, pz);	//  py * gx * z
		val1 = _mm_sub_ps(val1, val2);  // px * gy * z - py * gx * z => SET [5]
		//v[5] = -py * z * gx +  px * z * gy;
		v1[5] = SSEE(val1,0);
		v2[5] = SSEE(val1,1);
		v3[5] = SSEE(val1,2);
		v4[5] = SSEE(val1,3);


		// redefine pz
		pz = _mm_mul_ps(pz,pz); 		// pz := 1/(z*z)

		// will use these for the following calculations a lot.
		val1 = _mm_mul_ps(px, gx);
		val1 = _mm_mul_ps(val1, pz);		// px * z_sqr * gx
		val2 = _mm_mul_ps(py, gy);
		val2 = _mm_mul_ps(val2, pz);		// py * z_sqr * gy


		val3 = _mm_add_ps(val1, val2);
		val3 = _mm_sub_ps(_mm_setr_ps(0,0,0,0),val3);	//-px * z_sqr * gx -py * z_sqr * gy
		//v[2] = -px * z_sqr * gx -py * z_sqr * gy;	=> SET [2]
		v1[2] = SSEE(val3,0);
		v2[2] = SSEE(val3,1);
		v3[2] = SSEE(val3,2);
		v4[2] = SSEE(val3,3);


		val3 = _mm_mul_ps(val1, py); // px * z_sqr * gx * py
		val4 = _mm_add_ps(gy, val3); // gy + px * z_sqr * gx * py
		val3 = _mm_mul_ps(val2, py); // py * py * z_sqr * gy
		val4 = _mm_add_ps(val3, val4); // gy + px * z_sqr * gx * py + py * py * z_sqr * gy
		val4 = _mm_sub_ps(_mm_setr_ps(0,0,0,0),val4); //val4 = -val4.
		//v[3] = -px * py * z_sqr * gx +
		//       -py * py * z_sqr * gy +
		//       -gy;		=> SET [3]
		v1[3] = SSEE(val4,0);
		v2[3] = SSEE(val4,1);
		v3[3] = SSEE(val4,2);
		v4[3] = SSEE(val4,3);


		val3 = _mm_mul_ps(val1, px); // px * px * z_sqr * gx
		val4 = _mm_add_ps(gx, val3); // gx + px * px * z_sqr * gx
		val3 = _mm_mul_ps(val2, px); // px * py * z_sqr * gy
		val4 = _mm_add_ps(val4, val3); // gx + px * px * z_sqr * gx + px * py * z_sqr * gy
		//v[4] = px * px * z_sqr * gx +
		//	   px * py * z_sqr * gy +
		//	   gx;				=> SET [4]
		v1[4] = SSEE(val4,0);
		v2[4] = SSEE(val4,1);
		v3[4] = SSEE(val4,2);
		v4[4] = SSEE(val4,3);

		// step 6: integrate into A and b:
		ls.update(v1, *(buf_warped_residual+i+0), *(buf_weight_p+i+0));

		if(i+1>=buf_warped_size) break;
		ls.update(v2, *(buf_warped_residual+i+1), *(buf_weight_p+i+1));

		if(i+2>=buf_warped_size) break;
		ls.update(v3, *(buf_warped_residual+i+2), *(buf_weight_p+i+2));

		if(i+3>=buf_warped_size) break;
		ls.update(v4, *(buf_warped_residual+i+3), *(buf_weight_p+i+3));
	}
	Vector6 result;

	// solve ls
	ls.finish();
	ls.solve(result);

	return result;
}
#endif


#if defined(ENABLE_NEON)
Vector6 SE3Tracker::calculateWarpUpdateNEON(
		NormalEquationsLeastSquares &ls)
{
//	weightEstimator.reset();
//	weightEstimator.estimateDistributionNEON(buf_warped_residual, buf_warped_size);
//	weightEstimator.calcWeightsNEON(buf_warped_residual, buf_warped_weights, buf_warped_size);

	ls.initialize(width*height);
	
	float* cur_buf_warped_z = buf_warped_z;
	float* cur_buf_warped_x = buf_warped_x;
	float* cur_buf_warped_y = buf_warped_y;
	float* cur_buf_warped_dx = buf_warped_dx;
	float* cur_buf_warped_dy = buf_warped_dy;
	Vector6 v1,v2,v3,v4;
	float* v1_ptr;
	float* v2_ptr;
	float* v3_ptr;
	float* v4_ptr;
	for(int i=0;i<buf_warped_size;i+=4)
	{
		v1_ptr = &v1[0];
		v2_ptr = &v2[0];
		v3_ptr = &v3[0];
		v4_ptr = &v4[0];
	
		__asm__ __volatile__
		(
			"vldmia   %[buf_warped_z]!, {q10}            \n\t" // pz(q10)
			"vrecpe.f32 q10, q10                         \n\t" // z(q10)
			
			"vldmia   %[buf_warped_dx]!, {q11}           \n\t" // gx(q11)
			"vmul.f32 q0, q10, q11                       \n\t" // q0 = z*gx // = v[0]
			
			"vldmia   %[buf_warped_dy]!, {q12}           \n\t" // gy(q12)
			"vmul.f32 q1, q10, q12                       \n\t" // q1 = z*gy // = v[1]
			
			"vldmia   %[buf_warped_x]!, {q13}            \n\t" // px(q13)
			"vmul.f32 q5, q13, q12                       \n\t" // q5 = px * gy
			"vmul.f32 q5, q5, q10                        \n\t" // q5 = q5 * z = px * gy * z
			
			"vldmia   %[buf_warped_y]!, {q14}            \n\t" // py(q14)
			"vmul.f32 q3, q14, q11                       \n\t" // q3 = py * gx
			"vmls.f32 q5, q3, q10                        \n\t" // q5 = px * gy * z - py * gx * z // = v[5] (vmls: multiply and subtract from result)
			
			"vmul.f32 q10, q10, q10                      \n\t" // q10 = 1/(pz*pz)
		
			"vmul.f32 q6, q13, q11                       \n\t"
			"vmul.f32 q6, q6, q10                        \n\t" // q6 = val1 in SSE version = px * z_sqr * gx
			
			"vmul.f32 q7, q14, q12                       \n\t"
			"vmul.f32 q7, q7, q10                        \n\t" // q7 = val2 in SSE version = py * z_sqr * gy
			
			"vadd.f32 q2, q6, q7                         \n\t"
			"vneg.f32 q2, q2                             \n\t" // q2 = -px * z_sqr * gx -py * z_sqr * gy // = v[2]
			
			"vmul.f32 q8, q6, q14                        \n\t" // val3(q8) = px * z_sqr * gx * py
			"vadd.f32 q9, q12, q8                        \n\t" // val4(q9) = gy + px * z_sqr * gx * py
			"vmul.f32 q8, q7, q14                        \n\t" // val3(q8) = py * py * z_sqr * gy
			"vadd.f32 q9, q8, q9                         \n\t" // val4(q9) = gy + px * z_sqr * gx * py + py * py * z_sqr * gy
			"vneg.f32 q3, q9                             \n\t" // q3 = v[3]
			
			"vst4.32 {d0[0], d2[0], d4[0], d6[0]}, [%[v1]]! \n\t" // store v[0] .. v[3] for 1st value and inc pointer
			"vst4.32 {d0[1], d2[1], d4[1], d6[1]}, [%[v2]]! \n\t" // store v[0] .. v[3] for 2nd value and inc pointer
			"vst4.32 {d1[0], d3[0], d5[0], d7[0]}, [%[v3]]! \n\t" // store v[0] .. v[3] for 3rd value and inc pointer
			"vst4.32 {d1[1], d3[1], d5[1], d7[1]}, [%[v4]]! \n\t" // store v[0] .. v[3] for 4th value and inc pointer
			
			"vmul.f32 q8, q6, q13                        \n\t" // val3(q8) = px * px * z_sqr * gx
			"vadd.f32 q9, q11, q8                        \n\t" // val4(q9) = gx + px * px * z_sqr * gx
			"vmul.f32 q8, q7, q13                        \n\t" // val3(q8) = px * py * z_sqr * gy
			"vadd.f32 q4, q9, q8                         \n\t" // q4 = v[4]
			
			"vst2.32 {d8[0], d10[0]}, [%[v1]]               \n\t" // store v[4], v[5] for 1st value
			"vst2.32 {d8[1], d10[1]}, [%[v2]]               \n\t" // store v[4], v[5] for 2nd value
			"vst2.32 {d9[0], d11[0]}, [%[v3]]               \n\t" // store v[4], v[5] for 3rd value
			"vst2.32 {d9[1], d11[1]}, [%[v4]]               \n\t" // store v[4], v[5] for 4th value

        : /* outputs */ [buf_warped_z]"+r"(cur_buf_warped_z),
		                [buf_warped_x]"+r"(cur_buf_warped_x),
		                [buf_warped_y]"+r"(cur_buf_warped_y),
		                [buf_warped_dx]"+r"(cur_buf_warped_dx),
		                [buf_warped_dy]"+r"(cur_buf_warped_dy),
		                [v1]"+r"(v1_ptr),
		                [v2]"+r"(v2_ptr),
		                [v3]"+r"(v3_ptr),
		                [v4]"+r"(v4_ptr)
        : /* inputs  */ 
        : /* clobber */ "memory", "cc", // TODO: is cc necessary?
	                    "q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7", "q8", "q9", "q10", "q11", "q12", "q13", "q14"
		);
		

		// step 6: integrate into A and b:
		if(!(i+3>=buf_warped_size))
		{
			ls.update(v1, *(buf_warped_residual+i+0), *(buf_weight_p+i+0));
			ls.update(v2, *(buf_warped_residual+i+1), *(buf_weight_p+i+1));
			ls.update(v3, *(buf_warped_residual+i+2), *(buf_weight_p+i+2));
			ls.update(v4, *(buf_warped_residual+i+3), *(buf_weight_p+i+3));
		}
		else
		{
			ls.update(v1, *(buf_warped_residual+i+0), *(buf_weight_p+i+0));

			if(i+1>=buf_warped_size) break;
			ls.update(v2, *(buf_warped_residual+i+1), *(buf_weight_p+i+1));

			if(i+2>=buf_warped_size) break;
			ls.update(v3, *(buf_warped_residual+i+2), *(buf_weight_p+i+2));

			if(i+3>=buf_warped_size) break;
			ls.update(v4, *(buf_warped_residual+i+3), *(buf_weight_p+i+3));
		}
	}
	Vector6 result;

	// solve ls
	ls.finish();
	ls.solve(result);

	return result;
}
#endif

// 计算雅克比矩阵J　线性方程系数A,和偏置b，使用LDLT矩阵分解求解方程得到　李代数更新量 dse3
// 利用误差函数对误差向量求导数，求得各个点的误差之间的　协方差矩阵，也是雅克比矩阵Ｊ
// 是计算雅可比以及最小二乘方程的系数
// 对于参考帧中的一个3D点pi位置处的残差求雅可比，这里需要使用链式求导法则 
// Ji =      1/z' *dx *fx + 0* dy *fy
//                 0*dx *fx +  1/z' *dy *fy
//   -1 *   - x'/z'^2 *dx*fx  - y'/z'^2 *dy*fy 
//            - x'*y'/z'^2 *dx*fx - (1+y'^2/z'^2) *dy*fy
//             (1+x'^2/z'^2)*dx*fx + x'*y'/z'^2*dy*fy
//              - y'/z' *dx*fx + x'/z' * dy*fy
// A = J *J转置*(*(buf_weight_p+i))
// b = -J转置*(*(buf_weight_p+i))*lastErr
// Vector6 inc = A.ldlt().solve(b);// LDLT矩阵分解求解　dse3 李代数更新量
Vector6 SE3Tracker::calculateWarpUpdate(
		NormalEquationsLeastSquares &ls)
{
//	weightEstimator.reset();
//	weightEstimator.estimateDistribution(buf_warped_residual, buf_warped_size);
//	weightEstimator.calcWeights(buf_warped_residual, buf_warped_weights, buf_warped_size);
//
	ls.initialize(width*height);
	for(int i=0;i<buf_warped_size;i++)
	{
		float px = *(buf_warped_x+i);//x'
		float py = *(buf_warped_y+i);//y'
		float pz = *(buf_warped_z+i);//z'
		float r =  *(buf_warped_residual+i);//误差
		float gx = *(buf_warped_dx+i);// dx*fx
		float gy = *(buf_warped_dy+i);//dy*fx
		// step 3 + step 5 comp 6d error vector

		float z = 1.0f / pz;// 1/z'
		float z_sqr = 1.0f / (pz*pz);//1/z'^2
		Vector6 v;
		v[0] = z*gx + 0;
		v[1] = 0 +         z*gy;
		v[2] = (-px * z_sqr) * gx +
			  (-py * z_sqr) * gy;
		v[3] = (-px * py * z_sqr) * gx +
			  (-(1.0 + py * py * z_sqr)) * gy;
		v[4] = (1.0 + px * px * z_sqr) * gx +
			  (px * py * z_sqr) * gy;
		v[5] = (-py * z) * gx +
			  (px * z) * gy;

		// step 6: integrate into A and b:
			  // J转置*J * dse3 = -J转置*w*error
			  // v为求得的雅克比矩阵J  r为误差  *(buf_weight_p+i)为误差权重 
		ls.update(v, r, *(buf_weight_p+i));// 这里计算的是　A 和　b的和　以及加权误差平方和　error 
	}
// 欧式变换矩阵se3 更新后的量　
	Vector6 result;
	// solve ls
	// 对求得的　A 和　b的和以及加权误差平方和　error 　求均值
	ls.finish();
	// LDLT求解线性方程组 A*x=b    x = A.ldlt().solve(b);
	ls.solve(result);
	return result;
 }

}

