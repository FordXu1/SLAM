# TSN 结构 
[使用的 特定caffe](https://github.com/yjxiong/caffe/tree/9831a2b4d67e3f99418b6f2f99b6dde716853672)

[光流图像计算](https://github.com/yjxiong/dense_flow/tree/c9369a32ea491001db5298dfda1fa227a912d34f)

    two-stream 卷积网络对于长范围时间结构的建模无能为力，
    主要因为它仅仅操作一帧（空间网络）或者操作短片段中的单堆帧（时间网络），
    因此对时间上下文的访问是有限的。
    视频级框架TSN可以从整段视频中建模动作。

    和two-stream一样，TSN也是由空间流卷积网络和时间流卷积网络构成。
    但不同于two-stream采用单帧或者单堆帧，TSN使用从整个视频中稀疏地采样一系列短片段，
    每个片段都将给出其本身对于行为类别的初步预测，从这些片段的“共识”来得到视频级的预测结果。
    在学习过程中，通过迭代更新模型参数来优化视频级预测的损失值（loss value）。

    TSN网络示意图如下：

![](https://img-blog.csdn.net/20180319152830700)

    由上图所示，一个输入视频被分为 K 段（segment），一个片段（snippet）从它对应的段中随机采样得到。
    不同片段的类别得分采用段共识函数（The segmental consensus function）
    进行融合来产生段共识（segmental consensus），这是一个视频级的预测。
    然后对所有模式的预测融合产生最终的预测结果。

    具体来说，给定一段视频 V，把它按相等间隔分为 K 段 {S1,S2,⋯,SK}。
    接着，TSN按如下方式对一系列片段进行建模：
    TSN(T1,T2,⋯,TK)=H(G(F(T1;W),F(T2;W),⋯,F(TK;W)))
    
    其中：
    (T1,T2,⋯,TK) 代表片段序列，每个片段 Tk 从它对应的段 Sk 中随机采样得到。
    F(Tk;W) 函数代表采用 W 作为参数的卷积网络作用于短片段 Tk，函数返回 Tk 相对于所有类别的得分。
    段共识函数 G（The segmental consensus function）结合多个短片段的类别得分输出以获得他们之间关于类别假设的共识。
    基于这个共识，预测函数 H 预测整段视频属于每个行为类别的概率（本文 H 选择了Softmax函数）。
    结合标准分类交叉熵损失（cross-entropy loss）；
    网络结构
    一些工作表明更深的结构可以提升物体识别的表现。
    然而，two-stream网络采用了相对较浅的网络结构（ClarifaiNet）。
    本文选择BN-Inception (Inception with Batch Normalization)构建模块，
    由于它在准确率和效率之间有比较好的平衡。
    作者将原始的BN-Inception架构适应于two-stream架构，和原始two-stream卷积网络相同，
    空间流卷积网络操作单一RGB图像，时间流卷积网络将一堆连续的光流场作为输入。

    网络输入
    TSN通过探索更多的输入模式来提高辨别力。
    除了像two-stream那样，
    空间流卷积网络操作单一RGB图像，
    时间流卷积网络将一堆连续的光流场作为输入，
    作者提出了两种额外的输入模式：
    RGB差异（RGB difference）和
    扭曲的光流场（warped optical flow fields,idt中去除相机运动后的光流）。

[Temporal Segment Networks: Towards Good Practices for Deep Action Recognition](https://arxiv.org/pdf/1608.00859.pdf)

[caffe code](https://github.com/yjxiong/temporal-segment-networks)

[TSN（Temporal Segment Networks）代码实验](https://blog.csdn.net/zhang_can/article/details/79704084)

[(TSN)实验及错误日志](https://blog.csdn.net/cheese_pop/article/details/79958090)

[tensorFlow PyTorch 版本](https://github.com/yjxiong/tsn-pytorch)

    这篇文章是港中文Limin Wang大神的工作，他在这方面做了很多很棒的工作，
    可以followt他的主页：http://wanglimin.github.io/ 。

    这篇文章提出的TSN网络也算是spaital+temporal fusion，结构图见下图。

    这篇文章对如何进一步提高two stream方法进行了详尽的讨论，主要包括几个方面（完整内容请看原文）： 
        1. 据的类型：除去two stream原本的RGB image和 optical flow field这两种输入外，
           章中还尝试了RGB difference及 warped optical flow field两种输入。
            终结果是 RGB+optical flow+warped optical flow的组合效果最好。
        2. 构：尝试了GoogLeNet,VGGNet-16及BN-Inception三种网络结构，其中BN-Inception的效果最好。
        3. 包括 跨模态预训练，正则化，数据增强等。
        4. 果：UCF101-94.2%，HMDB51-69.4% 


        
# 安装 下载项目代码，并编译
## 安装编译
    git clone --recursive https://github.com/yjxiong/temporal-segment-networks
    bash build_all.sh; 
    或者多GPU并行：
    MPI_PREFIX=<root path to openmpi installation> 
    bash build_all.sh MPI_ON 
    build_all.sh文件会下载opencv 2.4.13，
    denseflow(用来截取视频帧和光流)，
    并且编译caffe-action (双流网络结构)
    这里有一点值得注意的是需要先clone代码，再编译。
    如果是从网上download的代码直接编译的话，会因为缺少部分文件导致编译失败。 
    
## 获取视频帧和光流
    论文中使用的数据库是HMDB-51和UCF-101，可以到他们的数据库官网中下载，并解压。 
    获取视频帧和光流代码： 
    bash scripts/extract_optical_flow.sh SRC_FOLDER OUT_FOLDER NUM_WORKER 
    各参数含义如下： 
    - SRC_FOLDER 数据集路径 
    - OUT_FOLDER 提取的rgb帧和光流帧 
    - NUM_WORKER 使用的gpu数量 >= 1

## 下载预训练好的模型
    bash scripts/get_reference_models.sh 
    模型比较大，网络连接不通顺或者有精力的话，可以自己直接复制链接，从网页上download。      
## 测试
    UCF101 split1部分：

    rgb流测试 部分
    python tools/eval_net.py ucf101 1 rgb /data3/UCF-all-in-one/ucf_frame/ \ 
    models/ucf101/tsn_bn_inception_rgb_deploy.prototxt \
    models/ucf101_split_1_tsn_rgb_reference_bn_inception.caffemodel \
    --num_worker 4 --save_scores rgb_score

    flow流测试 部分
    python tools/eval_net.py ucf101 1 flow /data3/UCF-all-in-one/ucf_transed/ \
    models/ucf101/tsn_bn_inception_flow_deploy.prototxt \
    models/ucf101_split_1_tsn_flow_reference_bn_inception.caffemodel \
    --num_worker 4 --save_scores ucf101/flow_score

    融合 fusion
    python tools/eval_scores.py ucf101/rgb_score.npz ucf101/flow_score.npz --score_weights 1 1.5 


# TSN改进版本之一  加权融合
    改进的地方主要在于fusion部分，不同的片段的应该有不同的权重，而这部分由网络学习而得，最后由SVM分类得到结果。
[Deep Local Video Feature for Action Recognition 【CVPR2017】](https://arxiv.org/pdf/1701.07368.pdf)

# TSN改进版本二  时间推理
    这篇是MIT周博磊大神的论文，作者是也是最近提出的数据集 Moments in time 的作者之一。
    该论文关注时序关系推理。
    对于哪些仅靠关键帧（单帧RGB图像）无法辨别的动作，如摔倒，其实可以通过时序推理进行分类。
    除了两帧之间时序推理，还可以拓展到更多帧之间的时序推理。
    通过对不同长度视频帧的时序推理，最后进行融合得到结果。
    该模型建立TSN基础上，在输入的特征图上进行时序推理。
    增加三层全连接层学习不同长度视频帧的权重，及上图中的函数g和h。

    除了上述模型外，还有更多关于时空信息融合的结构。
    这部分与connection部分有重叠，所以仅在这一部分提及。
    这些模型结构相似，区别主要在于融合module的差异，细节请参阅论文。
[Temporal Relational Reasoning in Videos](https://arxiv.org/pdf/1711.08496.pdf)
    
