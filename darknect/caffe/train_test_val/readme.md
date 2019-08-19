# 使用 caffe 训练 yolo



## 训练时用的 模型框架文件： gnet_train.prototxt
##  训练时用的 学习参数    ： gnet_solver.prototxt

## 测试集上使用的测试模型：  gnet_test.prototxt

## 检测识别模型：            gnet_deploy.prototxt


## 不调用检测识别模型文件 和 权重文件 得到识别结果 show_det.py

# 训练时使用的 bash脚本文件 : train.sh
      需要提供 一个 预训练的 模型权重文件
      bvlc_googlenet.caffemodel
      下载： http://dl.caffe.berkeleyvision.org/bvlc_googlenet.caffemodel

      ######  train.sh  ##############   
      #!/usr/bin/env sh
      # caffe main path
      CAFFE_HOME=../..
      # learning rate paramter
      SOLVER=./gnet_solver.prototxt
      # init weights 
      WEIGHTS=/home/wanyouwen/ewenwan/software/caffe-yolo/weights/bvlc_googlenet.caffemodel
      # training  --gpu=0,1,2,3,4,5,6,7
      $CAFFE_HOME/build/tools/caffe train \
          --solver=$SOLVER --weights=$WEIGHTS \
            --gpu=0,1,2,3 2>&1 | tee train_yolov1.log

      ###### gnet_solver.prototxt  #######
      net: "examples/yolo/gnet_train.prototxt"  # 网络配置文件
          # 在这四个train_net_param, train_net, net_param, net字段中至少需要出现一个，
          # 当出现多个时，就会按着(1) test_net_param, (2) test_net, (3) net_param/net 的顺序依次求解。
      test_iter: 4952        # 迭代器
      test_interval: 32000   # 是指测试间隔，每训练test_interval次，进行一次测试。
      test_initialization: false  # 是指在第一次迭代前，计算初始的loss以确保内存可用
      display: 20                 # 是信息显示间隔
      average_loss: 100           # 用于显示在上次average_loss迭代中的平均损失
      lr_policy: "multifixed"
      #stagelr: 0.001
      #stagelr: 0.01
      #stagelr: 0.001
      #stagelr: 0.0001
      #stageiter: 520
      #stageiter: 16000
      #stageiter: 24000
      #stageiter: 32000

      max_iter: 32000
      momentum: 0.9              # 动量 上次梯度更新的权重
      weight_decay: 0.0005       # 权重衰减，防止过拟合
      snapshot: 2000             # 快照  将训练出来的model和solver状态进行保存 
      snapshot_prefix: "./models/gnet_yolo"# 保存路径
      solver_mode: GPU# 运行模式


## 再一个 voc / coco 数据集 的图片需要转换成 数据库 lmdb/leveldb 文件 这样读写会比较快

      使用 ./convert.sh 转换 图片数据 到 lmdb/leveldb
        cd cafe/data/yolo
        ln -s /your/path/to/VOCdevkit/ .
        python ./get_list.py
        # change related path in script convert.sh
        ./convert.sh 
        
      问题：
      执行shell脚本时提示bad interpreter:No such file or directory的解决办法
      
      convert.sh 格式不正确
      dos 转成 unix
      notepad就可以转换
      
### 上述有需要改动的地方

      /////////////////////////////////
      convert.sh 
      ////////////
      #!/usr/bin/env sh
      # cafe main path
      CAFFE_ROOT=../..
      #/your/path/to/vocroot/
      ROOT_DIR=/home/wanyouwen/ewenwan/software/darknet/data/voc/VOCdevkit
      # string label Map to int label
      LABEL_FILE=$CAFFE_ROOT/data/yolo/label_map.txt

      # 2007 + 2012 trainval
      # source img
      LIST_FILE=$CAFFE_ROOT/data/yolo/trainval.txt
      # date base file
      LMDB_DIR=./lmdb/trainval_lmdb
      # shuff;e
      SHUFFLE=true

      # 2007 test
      # LIST_FILE=$CAFFE_ROOT/data/yolo/test_2007.txt
      # LMDB_DIR=./lmdb/test2007_lmdb
      # SHUFFLE=false
      # resize size
      RESIZE_W=448
      RESIZE_H=448

      # execute
      $CAFFE_ROOT/build/tools/convert_box_data  --resize_width=$RESIZE_W --resize_height=$RESIZE_H \
        --label_file=$LABEL_FILE $ROOT_DIR $LIST_FILE $LMDB_DIR --encoded=true --encode_type=jpg --shuffle=$SHUFFLE

      /////////////////////////////// 

#### 需要在 tools下新建 转换 voc标签框的文件 convert_box_data.cpp
      见 https://github.com/yeahkun/caffe-yolo/blob/master/tools/convert_box_data.cpp

      需要对
      caffe/util/io.hpp 
      caffe/src/caffe/util/io.cpp
      做一定修改 
## 训练
      ./train.sh
      数据 添加了 框标签   以下需要修改
         base_data_layer.cpp
         base_data_layer.cu
         box_data_layer.cpp
         
      出现的问题
      0519 02:13:01.861333  9687 layer_factory.hpp:77] Creating layer data
      I0519 02:13:01.861886  9687 net.cpp:84] Creating Layer data
      I0519 02:13:01.861901  9687 net.cpp:380] data -> data
      I0519 02:13:01.861933  9687 net.cpp:380] data -> label
      I0519 02:13:01.866948  9704 db_lmdb.cpp:35] Opened lmdb ../../data/yolo/trainval_lmdb
      I0519 02:13:01.873008  9687 box_data_layer.cpp:46] output data size: 64,3,448,448
      F0519 02:13:01.963363  9687 blob.cpp:133] Check failed: data_ 
      *** Check failure stack trace: ***
          @     0x7f85615eddaa  (unknown)
          @     0x7f85615edce4  (unknown)
          @     0x7f85615ed6e6  (unknown)
          @     0x7f85615f0687  (unknown)
          @     0x7f8561db39de  caffe::Blob<>::mutable_cpu_data()
          @     0x7f8561ca4dae  caffe::BasePrefetchingDataLayer<>::LayerSetUp()
          @     0x7f8561dacc35  caffe::Net<>::Init()
          @     0x7f8561daeb52  caffe::Net<>::Net()
          @     0x7f8561c7c92e  caffe::Solver<>::InitTrainNet()
          @     0x7f8561c7d973  caffe::Solver<>::Init()
          @     0x7f8561c7dc4f  caffe::Solver<>::Solver()
          @     0x7f8561c26fe1  caffe::Creator_SGDSolver<>()
          @           0x40dc6e  caffe::SolverRegistry<>::CreateSolver()
          @           0x40848d  train()
          @           0x405fec  main
          @     0x7f8560467f45  (unknown)
          @           0x40685b  (unknown)
          @              (nil)  (unknown)
      Aborted

      未解决


## 测试时使用的 bash脚本文件 :test.sh

## caffe需要做的修改
[caffe需要做的修改 ](caffe_should_change.md)
