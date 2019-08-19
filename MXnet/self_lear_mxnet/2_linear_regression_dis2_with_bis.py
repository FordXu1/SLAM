#!/usr/bin/env python
#-*- coding:utf-8 -*-
## y[i] = 2 * X[i][0] - 3.4 * X[i][1] + 4.2 + noise
# https://zh.gluon.ai/chapter_supervised-learning/linear-regression-scratch.html
## 测试 
##所以我们的第一个教程是如何只利用ndarray和autograd来实现一个线性回归的训练。
from mxnet import ndarray as nd
from mxnet import autograd
import random

 
#### 准备输入数据 ###
num_inputs = 2##数据维度
num_examples = 1000##样例大小

## 真实的需要估计的参数
true_w = [2, -3.4]##权重
true_b = 4.2##偏置

X = nd.random_normal(shape=(num_examples, num_inputs))## 1000*2
y = true_w[0] * X[:, 0] + true_w[1] * X[:, 1] + true_b## 1000*1
y += .01 * nd.random_normal(shape=y.shape)##加入 噪声 服从均值0和标准差为0.01的正态分布

## 读取数据
batch_size = 10
def data_iter():
    # 产生一个随机索引
    idx = list(range(num_examples))
    random.shuffle(idx)##打乱
    for i in range(0, num_examples, batch_size):##0 10 20 ...
        j = nd.array(idx[i:min(i+batch_size,num_examples)])##随机抽取10个样例
        yield nd.take(X, j), nd.take(y, j)##样例和标签 我们通过python的yield来构造一个迭代器。
##读取第一个随机数据块
for data, label in data_iter():
    print(data, label)
    break


###初始化模型参数
w = nd.random_normal(shape=(num_inputs, 1))## 2*1权重
b = nd.zeros((1,))##偏置
params = [w, b]
#之后训练时我们需要对这些参数求导来更新它们的值，所以我们需要创建它们的梯度。
for param in params:
    param.attach_grad()

### 定义模型
# 线性模型就是将输入和模型做乘法再加上偏移
def net(X):
    return nd.dot(X, w) + b

### 损失函数
# 使用常见的平方误差来衡量预测目标和真实目标之间的差距
def square_loss(yhat, y):
    # 注意这里我们把y变形成yhat的形状来避免自动广播
    return (yhat - y.reshape(yhat.shape)) ** 2

### 优化
# 这里通过随机梯度下降来求解 
# 模型参数沿着梯度的反方向走特定距离，这个距离一般叫学习率
def SGD(params, lr):
    for param in params:
        param[:] = param - lr * param.grad

### 训练
epochs = 5##训练迭代数据次数
learning_rate = .001##学习率
for e in range(epochs):
    total_loss = 0
    for data, label in data_iter():
        with autograd.record():##自动微分
            output = net(data)
            loss = square_loss(output, label)
        loss.backward()## 反向传播
        SGD(params, learning_rate)##求解梯度
        total_loss += nd.sum(loss).asscalar()
    print("Epoch %d, average loss: %f" % (e, total_loss/num_examples))

## 查看训练结果
print true_w, w
print true_b, b





