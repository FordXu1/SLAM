#!/usr/bin/env python
#-*- coding:utf-8 -*-
# 多类别逻辑回归 使用gluon
# 服饰 FashionMNIST 识别 逻辑回归
# 多类模型跟线性回归的主要区别在于输出节点从一个变成了多个
# 简单逻辑回归模型  y=a*X + b
from mxnet import ndarray as nd
from mxnet import autograd
import random
from mxnet import gluon

import sys
sys.path.append('..')
import utils #包含了自己定义的一些通用函数 如下载 载入数据集等
#import matplotlib.pyplot as plt#画图

##########################################################
#### 准备输入数据 ###
#一个稍微复杂点的数据集，它跟MNIST非常像，但是内容不再是分类数字，而是服饰
## 准备 训练和测试数据集
batch_size = 256#每次训练 输入的图片数量
train_data, test_data = utils.load_data_fashion_mnist(batch_size)



###########################################################
### 定义模型 ##################
# 使用gluon.nn.Sequential()定义一个空的模型
net = gluon.nn.Sequential()
with net.name_scope():#照例我们不需要制定每层输入的大小，gluon会做自动推导
    net.add(gluon.nn.Flatten())#使用Flatten层将输入数据转成 batch_size x ? 的矩阵
    net.add(gluon.nn.Dense(10))#然后输入到10个输出节点的全连接层
net.initialize()



#############################################
### Softmax和交叉熵损失函数
# softmax 回归实现  exp(Xi)/(sum(exp(Xi))) 归一化概率 使得 10类概率之和为1
# 交叉熵损失函数  将两个概率分布的负交叉熵作为目标值，最小化这个值等价于最大化这两个概率的相似度
# 我们需要定义一个针对预测为概率值的损失函数
# 它将两个概率分布的负交叉熵作为目标值，最小化这个值等价于最大化这两个概率的相似度
# 就是 使得 预测的 1*10的概率分布  尽可能 和 标签 1*10的概率分布相等  减小两个概率分布间 的混乱度(熵)
# 具体 真实标签 y=1 , y_lab=[0, 1, 0, 0, 0, 0, 0, 0, 0, 0]  
# 那么交叉熵就是y_lab[0]*log(y_pre[0])+...+y_lab[n]*log(y_pre[n])
# 注意到yvec里面只有一个1，那么前面等价于log(y_pre[y])
'''
如果你做了上一章的练习，那么你可能意识到了分开定义Softmax和交叉熵会有数值不稳定性。
因此gluon提供一个将这两个函数合起来的数值更稳定的版本
'''
softmax_cross_entropy = gluon.loss.SoftmaxCrossEntropyLoss()


###############################################
### 优化模型 #################################
trainer = gluon.Trainer(net.collect_params(), 'sgd', {'learning_rate': 0.1})

#############################################
### 训练 #######################
#learning_rate = .1#学习率
epochs = 7##训练迭代 次数 训练整个训练即的次数
for epoch in range(epochs):
    train_loss = 0.# 损失
    train_acc = 0. #准确度
    for data, label in train_data:#训练数据集 样本和标签
        with autograd.record():#自动微分
            output = net(data) #网络输出
            loss = softmax_cross_entropy(output, label)#损失
        loss.backward()#向后传播
        # 将梯度做平均，这样学习率会对batch size不那么敏感
        #SGD(params, learning_rate/batch_size)
        trainer.step(batch_size)
        train_loss += nd.mean(loss).asscalar()#损失
        train_acc += utils.accuracy(output, label)  #准确度

    test_acc = utils.evaluate_accuracy(test_data, net)#验证数据集的准确度
    print("训练次数 %d. 损失Loss: %f, 训练准确度Train acc %f, 测试准确度Test acc %f" % (
        epoch, train_loss/len(train_data), train_acc/len(train_data), test_acc))







