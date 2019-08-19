
# 总结：
[1](http://blog.csdn.net/linolzhang/article/details/54344350)
[2](http://blog.csdn.net/phdat101/article/details/53000036)

	这篇主要介绍Object Detection一些经典的网络结构。
	顺序是RCNN->SPP->Fast RCNN->Faster RCNN->YOLO->SSD->YOLO2->Mask RCNN->YOLO3。
	这里只粗糙地介绍网络构型变化。更多细节强烈推荐阅读原文。


[视频-斯坦福李飞飞-深度学习计算机视觉   网易云课堂](http://study.163.com/course/introduction.htm?courseId=1003223001)

	##########################################
	#############################################
# RCNN
	http://blog.csdn.net/cyiano/article/details/69950331
	 1 ss算法产生候选区域
	 ( 颜色差值加权无向图区域差异分割 +
	  基于距离差异和颜色差异的 层次聚类
	 )
[参考1](https://blog.csdn.net/guoyunfei20/article/details/78723646)
[参考2](https://blog.csdn.net/guoyunfei20/article/details/78727972)
[图分割论文代码](http://cs.brown.edu/people/pfelzens/segment/)

	OpenCV 3.3 实现了selective search
	在OpenCV的contrib模块中实现了selective search算法。类定义为：
	cv::ximgproc::segmentation::SelectiveSearchSegmentation  
	
	opencv3.3 有 图分割的实现
	/opencv_contrib/modules/ximgproc/include/opencv2/ximgproc/segmentation.hpp 

	 2 对每个候选区域（大小归一化 crop/warp reshape成 27*227）用CNN网络提取特征  输入 227*227  输出 4096维特征
	 3 3.1 SVM对特征向量进行分类
	   3.2 通过边界回归(独立)（bounding-box regression) 得到精确的目标区域，由于实际目标会产生多个子区域，
	       旨在对完成分类的前景目标进行精确的定位与合并，避免多个检出。

### RCNN存在三个明显的问题：
	    1）多个候选区域对应的图像需要预先提取，占用较大的磁盘空间；
	    2）针对传统CNN需要固定尺寸的输入图像，crop/warp（归一化）产生物体截断或拉伸，会导致输入CNN的信息丢失；
	    3）每一个ProposalRegion都需要进入CNN网络计算，上千个Region存在大量的范围重叠，重复的特征提取带来巨大的计算浪费。


	#####################################################
	################################################
	SPP网络 网络
	http://blog.csdn.net/xjz18298268521/article/details/52681966
	 智者善于提出疑问，既然CNN的特征提取过程如此耗时（大量的卷积计算），
	 为什么要对每一个候选区域独立计算，而不是提取整体特征，仅在分类之前做一次Region截取呢？
	 智者提出疑问后会立即付诸实践，于是SPP-Net诞生了。

	 1 ss 算法产出候选区域 ROI
	 2 对整张图 使用CNN网络 提取特征图
	 3 ROI对应的特征区域 进入SPP网络
	  （基于1算法生成的候选区域框在特征图上裁剪出来  (x,y)=(S*x’,S*y’)  区域映射
	   分别在每个区域最大值池化  
	   使用空间金字塔结构 提取 4*4 + 2*2 + 1 = 21 组特征
	   21*256 维度   256 为CNN卷积层 卷积核个数） 
	 4 SVM对特征向量进行分类  Bounding Box回归独立训练

	 SPP-Net在RCNN的基础上做了实质性的改进：

	    1）取消了crop/warp图像归一化过程，解决图像变形导致的信息丢失以及存储问题；
	    2）采用空间金字塔池化（SpatialPyramid Pooling ）替换了 全连接层之前的最后一个池化层（上图top），
	       翠平说这是一个新词，我们先认识一下它。
	    为了适应不同分辨率的特征图，定义一种可伸缩的池化层，不管输入分辨率是多大，都可以划分成m*n个部分。
	这是SPP-net的第一个显著特征，它的输入是conv5特征图 以及特征图候选框（原图候选框 通过stride映射得到），
	输出是固定尺寸（m*n）特征；
	还有金字塔呢？通过多尺度增加所提取特征的鲁棒性，这并不关键，在后面的Fast-RCNN改进中该特征已经被舍弃；
	最关键的是SPP的位置，它放在所有的卷积层之后，有效解决了卷积层的重复计算问题（测试速度提高了24~102倍），这是论文的核心贡献。


	 尽管SPP-Net贡献很大，仍然存在很多问题：
	    1）和RCNN一样，训练过程仍然是隔离的，提取候选框 | 计算CNN特征| SVM分类 | Bounding Box回归独立训练，
	       大量的中间结果需要转存，无法整体训练参数；
	    2）SPP-Net在无法同时Tuning在SPP-Layer两边的卷积层和全连接层，很大程度上限制了深度CNN的效果；
	    3）在整个过程中，Proposal Region(ss算法)仍然很耗时。



	########
	以上基于SVM进行分类
	#######
	以下直接使用网络 进行分类

	#####################################################
	########################################
	Fast RCNN
	http://blog.csdn.net/cyiano/article/details/70141957
	 Fast-RCNN主要贡献在于对RCNN进行加速，快是我们一直追求的目标（来个山寨版的奥运口号- 更快、更准、更鲁棒），
	   问题在以下方面得到改进：
		1）卖点1 - 借鉴SPP思路，提出简化版的ROI池化层（注意，没用金字塔，替换spp），同时加入了候选框映射功能，
		   使得网络能够反向传播，解决了SPP的整体网络训练问题；
		2）卖点2 - 多任务Loss层
		A）SoftmaxLoss代替了SVM，证明了softmax比SVM更好的效果；
		B）SmoothL1Loss取代Bouding box回归。
		   将分类和边框回归进行合并（又一个开创性的思路），通过多任务Loss层进一步整合深度网络，
		   统一了训练过程，从而提高了算法准确度。
		3）全连接层通过SVD加速
		   这个大家可以自己看，有一定的提升但不是革命性的。
		4）结合上面的改进，模型训练时可对所有层进行更新，除了速度提升外（训练速度是SPP的3倍，测试速度10倍），
		   得到了更好的检测效果（VOC07数据集mAP为70，注：mAP，mean Average Precision）。


	算法步骤：
	   1 ss 算法产出候选区域（region of interest, ROI）
	   2 对整张图像使用 CNN 提取出提取出feature map
	   3 ROI对应的特征区域 进入Fast RCNN网络
	    （先通过 ROI pooling层 框定（固定）尺寸， 
	      通过一些全连接层得到ROI feature vector，
	      ROI feature vector 分别进如两个不同的全连接层，
	      一个得到softmax 类别概率 分类结果，
	      一个得到regression回归bounding box 边界框）

	注意：
	 【1】ROI pooling层为亮点，功能 将任意大小的矩形区域（特征图map）
	     通过池化转变为统一的大小。
	     假如输出固定尺寸是H * W，则上一层的任何一个矩形框ROI都被平均分割为H * W个小块，
	     每个小块进行一次max pooling操作，选出小块中的最大值。 
	 【2】多任务损失函数 Mutil_Task_Loss = L_class + L_loction
	L_muti =  L_class(class_predict预测的类别,class_true真实类别)  +  
		  正则项 × L_loction(bb_predict预测的边界框，bb_true真实的边界框)

	 1）类别预测损失：L_class(class_predict预测的类别 p , class_true真实类别 u) 
	    http://blog.csdn.net/google19890102/article/details/49738427
	    多分类 softmax回归器 L_class(p,u)=
	     j为类别数 1，2，...,K
	    p（y(i)=j）|X(i);cet) = exp(cet(j)*X(j))/(sum(exp(cet(j)*X(j)))) 
	   总结就是对于每一个输出 X(j) 对于每一类都有一个输出 exp(cet(j)*X(j))
	   那么 对于是j类的概率就为 exp(cet(j)*X(j))/(sum(exp(cet(j)*X(j))))

	  误差就是  sum（log(p（y(i)=j）|X(i);cet)(预测不正确的)))）

	此处就是不断调整cet的参数

	2）边界框预测损失
	  L_loction =  sum(g(smooth_L1(bb_predict,bb_true)))| 四对参数
	  g(x) = 0.5*x^2 , |x|<1
			 |x|-0.5 , 其他     
	 即在正确分类的情况下，回归框与Label框之间的误差（Smooth L1）， 对应描述边框的4个参数（上下左右or平移缩放）
	 g对应单个参数的差异，|x|>1 时，变换为线性以降低离群噪声：

	细心的小伙伴可能发现了，我们提到的SPP的第三个问题还没有解决，依然是耗时的候选框提取过程
	（忽略这个过程，Fast-RCNN几乎达到了实时），那么有没有简化的方法呢？
	 必须有，搞学术一定要有这种勇气。



	################################################
	###################################################
	###  以上给予 ss算法产生候选框
	###  以下基于 RPN(Region Proposal Network) 代替之前的 Selective Search  产生候选框


	##########################
	#################################
	Faster RCNN
	http://blog.csdn.net/cyiano/article/details/70161959
	    对于提取候选框最常用的SelectiveSearch方法，提取一副图像大概需要2s的时间，
	改进的EdgeBoxes算法将效率提高到了0.2s，但是这还不够。
	    候选框提取不一定要在原图上做，特征图上同样可以，低分辨率特征图意味着更少的计算量，
	基于这个假设，MSRA的任少卿等人提出RPN（RegionProposal Network），完美解决了这个问题，我们先来看一下网络拓扑。


	   1 对整张图像使用 CNN 提取出提取出feature map
	   1 feature map----> RPN(Region Proposal Network) 代替之前的 Selective Search 算法 产生候选区域 ROI
	     切RPN网络与检测网络共享特征
	   3 ROI对应的特征区域 进入Fast RCNN网络
	    （先通过 ROI pooling层 框定（固定）尺寸， 
	      通过一些全连接层得到ROI feature vector，
	      ROI feature vector 分别进如两个不同的全连接层，
	      一个得到softmax 类别概率 分类结果，
	      一个得到regression回归bounding box 边界框）


	image ————> 卷积网络CNN ————> 特征图Feature Map ————> 
	区域提取网络 RPN ————>ROI Polling层得到---> 256维 特征向量 Feature Vector ————>
	 |————> 类别层     分类 得分
	 |————> 边界框层   区域坐标 


	通过添加额外的RPN分支网络，将候选框提取合并到深度网络中，这正是Faster-RCNN里程碑式的贡献。


	可以发现，候选区域的提取从预处理（SS）变成了后续处理（RPN）。
	RPN完全包含在整个卷积神经网络之中，模型大大得到了简化。

	CNN部分
	    CNN部分采用在分类方面效果良好的网络就足够了。作者选择了两种网络：ZF 和 VGG。
	   前者有5层可以共享参数的卷积层，而后者有13层。CNN部分的输出应该是一个W*H*R的特征图。

	RPN部分

	  RPN网络的特点在于通过滑动窗口的方式实现候选框的提取，
	  每个滑动窗口位置生成9个候选窗口（不同尺度、不同宽高），提取对应9个候选窗口（anchor）的特征，
	  用于目标分类和边框回归，与FastRCNN类似。
	  目标分类只需要区分候选框内特征为前景或者背景。
	  边框回归确定更精确的目标位置，
	 在conv feature map 上划窗Sliding Window
	 每个中心点预测 k=9个 不同尺寸 长宽比的 框
	 每个框 有四个框参数（中心点,长，宽） 以及2个类别参数 是物体、不是物体 


	http://blog.csdn.net/sloanqin/article/details/51545125
	详细过程

	共享网络
	CNN 5层 卷积池化网络

	input   224*224*3   大小224像素×224像素  RGB三通道
	第一层：
	    卷积层： 7*7*3*96  7*7 的卷积核尺寸 卷积滑动是步幅为2 输入3个通道 共有 96个卷积核
		     输出 110*110*96      (224-7+pad填充)/2 + 1  除以2 应为步幅为2  
		     每个卷积核对3上一层3通道图像卷积后平均 得到96个输出   pad数量不定 为了整除，进行填充
	    池化层： 池化核大小 3*3 步幅为2 所以输出为  55*55*96
		     (110-3+pad)/2 + 1 =55

	第二层：
	    卷积层：5*5*96*256  5*5卷积核尺寸 卷积滑动是步幅为2 输入96个通道 共有 256个卷积核
		   输出： 26*26*256
	    池化层：池化核大小 3*3 步幅为2 所以输出为  13*13*256   （26-3+pad）/2+1=13

	第三层：
	    卷积层: 3*3*256*384 3*3卷积核尺寸 卷积滑动是步幅为1 输入256个通道 共有384个卷积核
		   输出 13*13*384

	第四层：
	    卷积层: 3*3*384*384 3*3卷积核尺寸 卷积滑动是步幅为1 输入384个通道 共有384个卷积核
		   输出 13*13*384

	第五层：
	    卷积层: 3*3*384*256 3*3卷积核尺寸 卷积滑动是步幅为1 输入384个通道 共有256个卷积核
		   输出 13*13*256

	前五层卷积池化层 分类网络和 rpn网络共享

	#################################################
	    第五层的卷积层 输出直接进入 RPN网络 提取候选框
	##################################################

	    池化层：池化核大小 3*3 步幅为2 所以输出为  6*6*256   （13-3+pad）/2+1=6


	  ### 一下为 ROI Polling层  
	第六层：
	    全连接层：
	    6*6*256-->  4096???  4*4*256
	第七层：
	    全连接层：
	    6*6*256

	第八层：
	    softmax分类 输出类别
	    SmoothL1    输出边界框

	RPN网络：
	输入：13*13*256
	    第一层：
		卷积层： 3*3*256*256       3*3卷积核尺寸 卷积滑动是步幅为1 pad=1 输入256个通道 共有256个卷积核
		      输出 12*12*256     (13-3+1）/1 +1 = 12
		非线性激活层


	    第二 1层（类别预测层）： 
		上接第一层卷积层输出 12*12*256  256个Feature map 每个feature map 12*12
		每个像素点 是物体、不是物体=2  共提取9种框  2*9=18个输出
		卷积层： 1*1*256*18的卷积核 1*1卷积核尺寸 卷积滑动是步幅为1 pad=0 输入256个通道 共有18个卷积核

		输出 12*12*18  就是每个点 预测9个框 对应的类别

	注意：9种框 3中尺寸（128、256、512）×三种长宽比（1:1 1:2 2:1）

	    第二 2层（框预测层）
	       上接第一层卷积层输出 12*12*256
	       每个框 4个参数  每个点预测9种框  4*9=36
	       卷积层： 1*1*256*36的卷积核 1*1卷积核尺寸 卷积滑动是步幅为1 pad=0 输入256个通道 共有36个卷积核

		输出 12*12*36  就是每个点 预测9个框 对应的位置
	   第三层
	      类别预测层__重变形 reshape

	训练过程中，涉及到的候选框选取，选取依据：
	    1）丢弃跨越边界的anchor；
	    2）与样本重叠区域大于0.7的anchor标记为前景，重叠区域小于0.3的标定为背景；
	       对于每一个位置，通过两个全连接层（目标分类+边框回归）对每个候选框（anchor）进行判断，
	       并且结合概率值进行舍弃（仅保留约300个anchor），没有显式地提取任何候选窗口，完全使用网络自身完成判断和修正。
	从模型训练的角度来看，通过使用共享特征交替训练的方式，达到接近实时的性能，交替训练方式描述为：
	    1）根据现有网络初始化权值w，训练RPN；
	    2）用RPN提取训练集上的候选区域，用候选区域训练FastRCNN，更新权值w；
	    3）重复1、2，直到收敛。


	Faster实现了端到端的检测，并且几乎达到了效果上的最优，速度方向的改进仍有余地，于是YOLO诞生了。

	########################################
	##################################################
	YOLO

	https://zhuanlan.zhihu.com/p/25236464
	http://nooverfit.com/wp/%E6%9C%BA%E5%99%A8%E8%A7%86%E8%A7%89-%E7%9B%AE%E6%A0%87%E6%A3%80%E6%B5%8B%E8%A1%A5%E4%B9%A0%E8%B4%B4%E4%B9%8Byolo-you-only-look-once/

	YOLO来自于“YouOnly Look Once”，你只需要看一次，不需要类似RPN的候选框提取，直接进行整图回归就可以了，简单吧？

	YOLO将物体检测作为回归问题求解。基于一个单独的end-to-end网络，完成从原始图像的输入到物体位置和类别的输出。
	 从网络设计上，YOLO与rcnn、fast rcnn及faster rcnn的区别如下：

	1] YOLO训练和检测均是在一个单独网络中进行。YOLO没有显示地求取region proposal的过程。
	   而rcnn/fast rcnn 采用分离的模块（独立于网络之外的selective search方法）求取候选框（可能会包含物体的矩形区域），
	   训练过程因此也是分成多个模块进行。Faster rcnn使用RPN（region proposal network）卷积网络替代
	   rcnn/fast rcnn的selective search模块，将RPN集成到fast rcnn检测网络中，得到一个统一的检测网络。
	   尽管RPN与fast rcnn共享卷积层，但是在模型训练过程中，
	   需要反复训练RPN网络和fast rcnn网络（注意这两个网络核心卷积层是参数共享的）。

	2] YOLO将物体检测作为一个回归问题进行求解，输入图像经过一次inference，
	   便能得到图像中所有物体的位置和其所属类别及相应的置信概率。
	   而rcnn/fast rcnn/faster rcnn将检测结果分为两部分求解：
	   物体类别（分类问题），物体位置即bounding box（回归问题）。

	448*448*3 输入图像
	——————> 7*7 卷积核  数量64 步幅度为2  这里卷积输出未平均
		2*2 最大值池化 步幅度为2 
	输出112*112*(3*64)=112*112*193    （448-7 + pad）/2=224 卷积后    池化后(224-2+pad)/2 =112   
	——————> 3*3 卷积核  数量192 步幅度为1  这里卷积输出未平均
		2*2 最大值池化 步幅度为2 


	其中，卷积层用来提取图像特征，全连接层用来预测图像位置和类别概率值。
	YOLO网络借鉴了GoogLeNet分类网络结构。不同的是，YOLO未使用inception
	module，而是使用1x1卷积层（此处1x1卷积层的存在是为了跨通道信息整合）+3x3卷积层简单替代。


	 算法描述为：
	    1）将图像划分为固定的网格（比如7*7），如果某个样本Object中心落在对应网格，该网格负责这个Object位置的回归；
	    2）每个网格预测包含Object位置与置信度信息，这些信息编码为一个向量；
	    3）网络输出层即为每个Grid的对应结果，由此实现端到端的训练。
	 YOLO算法的问题有以下几点：
	    1）7*7的网格回归特征丢失比较严重，缺乏多尺度回归依据；
	    2）Loss计算方式无法有效平衡（不管是加权或者均差），Loss收敛变差，导致模型不稳定。

	   Object（目标分类+回归）<=等价于=>背景（目标分类）
	   导致Loss对目标分类+回归的影响，与背景影响一致，部分残差无法有效回传；

	   整体上YOLO方法定位不够精确，贡献在于提出给目标检测一个新的思路，让我们看到了目标检测在实际应用中真正的可能性。
	  这里备注一下，直接回归可以认为最后一层即是对应7*7个网格的特征结果，
	  每一个网格的对应向量代表了要回归的参数（比如pred、cls、xmin、ymin、xmax、ymax），参数的含义在于Loss函数的设计。

	7*7*30   （4 + 1）×2 +20 =30
	每个格子输出B个bounding box（包含物体的矩形区域）信息，以及C个物体属于某种类别的概率信息。
	Bounding box信息包含5个数据值，分别是x,y,w,h,和confidence。
	其中x,y是指当前格子预测得到的物体的bounding box的中心位置的坐标。
	w,h是bounding box的宽度和高度。
	注意：实际训练过程中，w和h的值使用图像的宽度和高度进行归一化到[0,1]区间内；
	x，y是bounding box中心位置相对于当前格子位置的偏移值，并且被归一化到[0,1]。

	YOLO网络最终的全连接层的输出维度是 S*S*(B*5 + C)。
	YOLO论文中，作者训练采用的输入图像分辨率是448x448，S=7，B=2；
	采用VOC 20类标注物体作为训练数据，C=20。因此输出向量为7*7*(20 + 2*5)=1470维。


	注：
	*由于输出层为全连接层，因此在检测时，YOLO训练模型只支持与训练图像相同的输入分辨率。
	*虽然每个格子可以预测B个bounding box，但是最终只选择只选择IOU最高的bounding box作为物体检测输出，
	即每个格子最多只预测出一个物体。当物体占画面比例较小，如图像中包含畜群或鸟群时，
	每个格子包含多个物体，但却只能检测出其中一个。这是YOLO方法的一个缺陷。



	Loss函数定义

	YOLO使用均方和误差作为loss函数来优化模型参数，即网络输出的S*S*(B*5 + C)维向量与
	真实图像的对应S*S*(B*5 + C)维向量的均方和误差。
	如下式所示。其中，coordError、iouError和classError分别代表预测数据与标定数据之间的坐标误差、IOU误差和分类误差。

	LOSS = SUM(coordError + iouError + classError) 1 to S*S


	综上，YOLO具有如下优点：
	   【1】 快。YOLO将物体检测作为回归问题进行求解，整个检测网络pipeline简单。
		 在titan x GPU上，在保证检测准确率的前提下（63.4% mAP，VOC 2007 test set），可以达到45fps的检测速度。
	   【2】背景误检率低。YOLO在训练和推理过程中能‘看到’整张图像的整体信息，
		 而基于region proposal的物体检测方法（如rcnn/fast rcnn），在检测过程中，只‘看到’候选框内的局部图像信息,
		 因此，若当图像背景（非物体）中的部分数据被包含在候选框中送入检测网络进行检测时，容易被误检测成物体。
		 测试证明，YOLO对于背景图像的误检率低于fast rcnn误检率的一半。

	   【3】通用性强。YOLO对于艺术类作品中的物体检测同样适用。它对非自然图像物体的检测率远远高于DPM和RCNN系列检测方法。


	但相比RCNN系列物体检测方法，YOLO具有以下缺点：
	    识别物体位置精准性差。
	    召回率低。

	改进
	  为提高物体定位精准性和召回率，YOLO作者提出了YOLO9000，提高训练图像的分辨率，
	  引入了faster rcnn中anchor box的思想，对各网络结构及各层的设计进行了改进，
	  输出层使用卷积层替代YOLO的全连接层，联合使用coco物体检测标注数据和imagenet物体分类标注数据训练物体检测模型。
	  相比YOLO，YOLO9000在识别种类、精度、速度、和定位准确性等方面都有大大提升。（yolo9000详解有空给出）

	实践

	使用YOLO训练自己的物体识别模型也非常方便，只需要将配置文件中的20类，更改为自己要识别的物体种类个数即可。
	训练时，建议使用YOLO提供的检测模型（使用VOC 20类标注物体训练得到）去除最后的全连接层初始化网络。


	1.B个bounding box中选confidence最大的那个作为最终的目标。
	  预测B个bounding box而不是1个bounding box的目的是定位更准确些（虽然最后定位精准性仍是yolov1的缺陷） 
	2. yolo给出了extraction的工具，你可以用它的工具截取你想要的网络层（yolo源代码里此处有个坑 读写weight文件时的，要小心）。
	   我记得darknet给出的预训练模型是用分类网络训练出来的，而yolo模型是在这个pretrained的模型上训练出来的识别模型。



	#######################################################
	##########################################################
	SSD
	http://blog.csdn.net/muyouhang/article/details/77727381
	https://zhuanlan.zhihu.com/p/24954433?refer=xiaoleimlnote

	基于“Proposal + Classification” 的 Object Detection 的方法，
	R-CNN 系列（R-CNN、SPPnet、Fast R-CNN 以及 Faster R-CNN），取得了非常好的结果，
	但是在速度方面离实时效果还比较远在提高 mAP 的同时兼顾速度，逐渐成为 Object Detection 未来的趋势。

	 YOLO 虽然能够达到实时的效果，但是其 mAP 与刚面提到的 state of art 的结果有很大的差距。

	由于YOLO本身采用的SingleShot基于最后一个卷积层实现，对目标定位有一定偏差，也容易造成小目标的漏检。
	借鉴Faster-RCNN的Anchor机制，SSD（Single Shot MultiBox Detector）在一定程度上解决了这个问题，我们先来看下SSD的结构对比图。

	YOLO 有一些缺陷：每个网格只预测一个物体，容易造成漏检；
	对于物体的尺度相对比较敏感，对于尺度变化较大的物体泛化能力较差。
	针对 YOLO 中的这些不足，该论文提出的方法 SSD 在这两方面都有所改进，同时兼顾了 mAP 和实时性的要求。
	在满足实时性的条件下，接近 state of art 的结果。
	对于输入图像大小为 300*300 在 VOC2007 test 上能够达到 58 帧每秒( Titan X 的 GPU )，72.1% 的 mAP。
	输入图像大小为 500 *500 , mAP 能够达到 75.1%。
	作者的思路就是Faster R-CNN+YOLO，利用YOLO的思路和Faster R-CNN的anchor box的思想


	关键点：

	关键点1：网络结构

		该论文采用 VGG16 的基础网络结构，使用前面的前 5 层，然后利用 astrous 算法将 fc6 和 fc7 层转化成两个卷积层。
		再格外增加了 3 个卷积层，和一个 average pool层。
		不同层次的 feature map 分别用于 default box 的偏移以及不同类别得分的预测
		（惯用思路：使用通用的结构(如前 5个conv 等)作为基础网络，然后在这个基础上增加其他的层），
		最后通过 nms得到最终的检测结果。


		这些增加的卷积层的 feature map 的大小变化比较大，允许能够检测出不同尺度下的物体： 
		在低层的feature map,感受野比较小，高层的感受野比较大，在不同的feature map进行卷积，可以达到多尺度的目的。
		观察YOLO，后面存在两个全连接层，全连接层以后，每一个输出都会观察到整幅图像，并不是很合理。
		但是SSD去掉了全连接层，每一个输出只会感受到目标周围的信息，包括上下文。这样来做就增加了合理性。
		并且不同的feature map,预测不同宽高比的图像，这样比YOLO增加了预测更多的比例的box。（下图横向的流程）

	关键点2：多尺度feature map得到 default boxs及其 4个位置偏移和21个类别置信度
		对于不同尺度feature map（ 上图中 38x38x512，19x19x512, 10x10x512, 5x5x512, 3x3x512, 1x1x256）
	      的上的所有特征点： 以5x5x256为例 它的#defalut_boxes = 6

	   按照不同的 scale 和 ratio 生成，k 个 default boxes，这种结构有点类似于 Faster R-CNN 中的 Anchor。
	   (此处k=6所以：5*5*6 = 150 boxes)

	 基于多尺度特征的Proposal，SSD达到了效率与效果的平衡，从运算速度上来看，
	能达到接近实时的表现，从效果上看，要比YOLO更好。
		对于目标检测网络的探索仍在一个快速的过程中，
	有些基于Faster-RCNN的变种准确度已经刷到了87%以上，
	而在速度的改进上，YOLO2也似乎会给我们带来一定的惊喜，“未来已来”，我们拭目以待！



	#######################################################
	#####################################################
	YOLO2/YOLO9000
	http://blog.csdn.net/jesse_mx/article/details/53925356
	https://zhuanlan.zhihu.com/p/25167153

	YOLO2主要有两个大方面的改进：
	    使用一系列的方法对YOLO进行了改进，在保持原有速度的同时提升精度得到YOLOv2。
	    提出了一种目标分类与检测的联合训练方法，同时在COCO和ImageNet数据集中进行训练得到YOLO9000，实现9000多种物体的实时检测。



	##############################################
	################################################
	Mask RCNN
	http://blog.csdn.net/cyiano/article/details/73571678
	https://www.leiphone.com/news/201703/QU1einPqSPJEffog.html
	https://www.leiphone.com/news/201608/vhqwt5eWmUsLBcnv.html
	http://blog.csdn.net/linolzhang/article/details/71774168

	http://blog.csdn.net/zhangjunhit/article/details/64920075?locationNum=6&fps=1

	tensorFlow代码
	https://github.com/CharlesShang/FastMaskRCNN

	Mask RCNN in PyTorch 
	https://github.com/felixgwu/mask_rcnn_pytorch

	Mask-RCNN implementation in MXNet

	xilaili
	https://github.com/xilaili/maskrcnn.mxnet

	TuSimple
	https://github.com/TuSimple/mx-maskrcnn 


	本文主要将 Faster R-CNN 拓展到图像分割上，提出了 Mask R-CNN 简单快捷的解决 Instance segmentation，
	什么是 Instance segmentation 语义分割，就是将一幅图像中所有物体框出来，并将物体进行像素级别的分割提取。

	2 相关工作
	R-CNN: 基于候选区域的物体检测成为目标检测算法中最流行的的，尤其是 Faster R-CNN 效果很好。

	目标实例分割 的框架（ Object instance segmentation ）

	Instance Segmentation 语义分割： 受到 R-CNN 的影响，大家纷纷采用R-CNN 思路来做 分割，
	 文献【8】提出的 fully convolutional instance segmentation (FCN) 是效果最好的。

	3 Mask R-CNN 
	Mask R-CNN在概念上是很简单：对于每一个候选区域 Faster R-CNN 有两个输出，
	一个类别标签，一个矩形框坐标信息。
	这里我们加了第三个分支用于输出 object mask即分割出物体。

	Mask R-CNN 也是采用了两个步骤，第一个步骤就是 RPN 提取候选区域，
	在第二个步骤，平行于(使用 RoIPool 对候选区域提取特征进行类别分类和坐标回归)，对于每个 RoI, Mask R-CNN 输出一个二值 mask。
	这与当前大部分系统不一样，当前这些系统的类别分类依赖于 mask 的预测。我们还是沿袭了 Fast R-CNN 的精神，
	它将矩形框分类和坐标回归并行的进行，这么做很大的简化了R-CNN的流程。


	在训练阶段，我们对每个样本的 RoI 定义了多任务损失函数 L = L_cls + L_box + L_mask ,
	其中 L_cls 和 L_box 的定义和Fast R-CNN 是一样的。
	在 mask 分支中对每个 RoI 的输出是 K*m*m，表示K个 尺寸是 m*m的二值 mask，K是物体类别数目。
	这里我们使用了 per-pixel sigmoid，将 的损失函数定义为 L_mask average binary cross-entropy，
	我们的 L_mask 只定义对应类别的 mask损失，其他类别的mask输出不会影响该类别的 loss。


	Mask Representation: 对于每个 RoI 我们使用 一个 FCN 网络来预测 m*m mask。
	m*m是一个小的特征图尺寸，如何将这个小的特征图很好的映射到原始图像上？
	为此我们提出了一个 叫 RoIAlign 的网络层来解决该问题，它在 mask 预测中扮演了重要的角色。

	RoIAlign: RoIPool 是一个标准的提特征运算，它从每个 RoI 提取出一个小的特征（ 7×7），
	RoIPool 首先对浮点的 RoI 进行量化，然后再提取分块直方图，最后通过 最大池化 组合起来。
	这种分块直方图对于分类没有什么大的影响，但是对像素级别精度的 mask 有很大影响。


	为了解决上述问题，我们提出了一个 RoIAlign 网络层 解决 RoIPool 量化引入的问题，
	将提取的特征和输入合适的对应起来。
	我们的改变也是很简单的：我们避免对 RoI 的边界或 bins 进行量化。
	使用线性差值精确计算每个 RoI bin 最后组合起来。



	第一个分支是原始的Faster R-CNN结构，用于对候选bounding box进行分类和bbox坐标回归
	第二个分支对每个ROI区域预测分割mask, 它的结构实质上是一个小的FCN
	两个分支是平行/并行的

	Faster R-CNN：对于每个候选物体，输出是类别标签和bbox。

	    第一步：RPN: 给出候选区域的bbox
	    第二步：通过RoIPooling, 在各个候选框中进行分类和bbox的回归
	Mask R-CNN：采用和Faster R-CNN类似的两步过程
	    第一步：RPN: 给出候选区域的bbox
	    第二步：RoIPooling—————> RoIAlign
		   分支一：各个候选框中进行分类和bbox offset。
		   分支二：对每个RoI输出binary mask

	RoIAlign: 是对RoI Pooling的改进。RoI Pooling在 Pooling时可能会有misalignment。
	解决方法：参考 Spatial Transformer Networks (NIPS2015) https://arxiv.org/abs/1506.02025, 
	使用双线性插值，再做聚合。
	(个人理解：misalignment在做分类时问题不大，可是当需要做pixel级别的mask时就不得不考虑其影响了。
	比如8 * 7的数据原先使用ROI Pooling时要pooling到7 * 7，会有misalignment。
	当使用ROIAlign后，先双线性插值到14 * 14，再pooling到7 * 7)



	作者将网络分成两个部分：
	1 用于提取特征的“backbone”，
	2 和用于classification、regression、mask prediction的“head”。

	“backbone”方面，作者用了两种不同深度的残差网络：
	50层的ResNet和101层的ResNeXt。
	这几个残差可以分成4段，特征将在最后的卷积层C4提取出来。
	另外，作者还使用了Feature Pyramid Network（FPN）。
	FPN会从不同等级的特征金字塔中提取ROI特征，实验也表明，在backbone中用FPN效果更好。

	“head”方面，根据之前的backbone是Resnet还是FPN有两种形式： 


	https://github.com/TuSimple/mx-maskrcnn 
	代码运行记录：
	软件依赖
	Ubuntu 16.04, Python 2.7
	numpy(1.12.1), cv2(2.4.9), PIL(4.3), matplotlib(2.1.0), cython(0.26.1), easydict



	1 下载数据集：
	   城市地形数据集 Cityscapes data,
	       官方下载  https://www.cityscapes-dataset.com/
	       (gtFine_trainvaltest.zip, leftImg8bit_trainvaltest.zip)
	   下载后解压到data/cityscape/

	├── leftImg8bit/
	│   ├── train/
	│   ├── val/
	│   └── test/
	├── gtFine/
	│   ├── train/
	│   ├── val/
	│   └── test/
	└── imglists/
	    ├── train.lst
	    ├── val.lst
	    └── test.lst


	2 下载预训练的模型 Resnet-50 参差网络 
