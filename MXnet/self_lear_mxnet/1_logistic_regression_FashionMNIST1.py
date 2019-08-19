#!/usr/bin/env python
#-*- coding:utf-8 -*-
# 多类别逻辑回归
# 服饰 FashionMNIST 识别 逻辑回归
# 多类模型跟线性回归的主要区别在于输出节点从一个变成了多个
# 简单逻辑回归模型  y=a*X + b
from mxnet import ndarray as nd
from mxnet import autograd
import random
from mxnet import gluon

import matplotlib.pyplot as plt#画图

##########################################################
#### 准备输入数据 ###
#一个稍微复杂点的数据集，它跟MNIST非常像，但是内容不再是分类数字，而是服饰
#我们通过gluon的data.vision模块自动下载这个数据
def transform(data, label):
    return data.astype('float32')/255, label.astype('float32')
#下载数据
mnist_train = gluon.data.vision.FashionMNIST(train=True, transform=transform)
mnist_test = gluon.data.vision.FashionMNIST(train=False, transform=transform)
#mnist_train = gluon.data.vision.MNIST(train=True, transform=transform)
#mnist_test = gluon.data.vision.MNIST(train=False, transform=transform)
# 打印一个样本的形状和它的标号
data, label = mnist_train[0]#('example shape: ', (28, 28, 1), 'label:', 2.0)
print data.shape, label
# 我们画出前几个样本的内容，和对应的文本标号
def show_images(images):
    n = images.shape[0]#图像总数
    _, figs = plt.subplots(1, n, figsize=(15, 15))##图像总数个子图 1行 n列
    for i in range(n):
        figs[i].imshow(images[i].reshape((28, 28)).asnumpy())##显示图像 1*784 ————>28*28
        figs[i].axes.get_xaxis().set_visible(False)#
        figs[i].axes.get_yaxis().set_visible(False)
    plt.show()
#label 0~9 对应的 服饰名字
def get_text_labels(label):
    text_labels = [
        't-shirt', 'trouser', 'pullover', 'dress,', 'coat',
        'sandal', 'shirt', 'sneaker', 'bag', 'ankle boot'
    ]
    return [text_labels[int(i)] for i in label]
#前10个样本 和 对于的标号
data, label = mnist_train[0:10]
show_images(data)#图像
print(get_text_labels(label))##标号
## 准备 训练和测试数据集
# 这个DataLoader是一个iterator对象类(每次只载入一个banch的数据进入内存)，非常适合处理规模较大的数据集
batch_size = 256#每次训练 输入的图片数量
train_data = gluon.data.DataLoader(mnist_train, batch_size, shuffle=True)#按照 batch_size 分割成 每次训练时的数据
test_data = gluon.data.DataLoader(mnist_test, batch_size, shuffle=False) #测试数据


###########################################################
### 定义模型 ##################
#@@@@初始化模型参数 权重和偏置@@@@
num_inputs = 784#输入数据维度  1*784
num_outputs = 10#输出标签维度 1*10
W = nd.random_normal(shape=(num_inputs, num_outputs))
 # 256*784  ×  784*10 ——————> 256*10 每张图像预测10个类别的概率 每次共256张图片
b = nd.random_normal(shape=num_outputs)#偏置
params = [W, b]#权重和偏置 参数
 # 模型参数附上梯度 自动微分时 会计算梯度信息
for param in params:
    param.attach_grad()

#@@@@@定义模型@@@@@@
## softmax 回归实现  exp(Xi)/(sum(exp(Xi))) 归一化概率 使得 10类概率之和为1
def softmax(X):
    exp = nd.exp(X)
    # 假设exp是矩阵，这里对行进行求和，并要求保留axis 1，
    # 就是返回 (nrows, 1) 形状的矩阵
    partition = exp.sum(axis=1, keepdims=True)
    return exp / partition
## 测试softmax
# 我们将每个元素变成了非负数，而且每一行加起来为1
X = nd.random_normal(shape=(2,5))
X_prob = softmax(X)
print(X_prob)
print(X_prob.sum(axis=1))#按行求和

## 定义模型
def net(X):
    return softmax(nd.dot(X.reshape((-1,num_inputs)), W) + b)# y = softmax( a*X + b ) 

#############################################
### 损失函数
# 交叉熵损失函数 
# 我们需要定义一个针对预测为概率值的损失函数
# 它将两个概率分布的负交叉熵作为目标值，最小化这个值等价于最大化这两个概率的相似度
# 就是 使得 预测的 1*10的概率分布  尽可能 和 标签 1*10的概率分布相等  减小两个概率分布间 的混乱度(熵)
# 具体 真实标签 y=1 , y_lab=[0, 1, 0, 0, 0, 0, 0, 0, 0, 0]  
# 那么交叉熵就是y_lab[0]*log(y_pre[0])+...+y_lab[n]*log(y_pre[n])
# 注意到yvec里面只有一个1，那么前面等价于log(y_pre[y])

def cross_entropy(yhat, y):#yhat为预测 y为真实标签
    return - nd.pick(nd.log(yhat), y)#注意为 负交叉熵

##################################################
##### 准确度
# 我们将预测概率最高的那个类作为预测的类，然后通过比较真实标号我们可以计算精度
def accuracy(output, label):#预测输出 output 真实标签label
    return nd.mean(output.argmax(axis=1)==label).asscalar()
# 评估数据集上的准确度
def evaluate_accuracy(data_iterator, net):
    acc = 0.
    for data, label in data_iterator:# 数据集里的 样本和标签
        output = net(data)#网络输出
        acc += accuracy(output, label)
    return acc / len(data_iterator)#平均准确度
#因为我们随机初始化了模型，所以这个模型的精度应该大概是1/num_outputs = 0.1
print evaluate_accuracy(test_data, net)

###############################################
### 优化模型 #################################
# 这里通过随机梯度下降来求解 
# 模型参数沿着梯度的反方向走特定距离，这个距离一般叫学习率
def SGD(params, lr):
    for param in params:
        param[:] = param - lr * param.grad#更新每个参数

#############################################
### 训练 #######################
learning_rate = .1#学习率
epochs = 7##训练迭代 次数
for epoch in range(epochs):
    train_loss = 0.# 损失
    train_acc = 0. #准确度
    for data, label in train_data:#训练数据集 样本和标签
        with autograd.record():#自动微分
            output = net(data) #网络输出
            loss = cross_entropy(output, label)##损失
        loss.backward()#向后传播
        # 将梯度做平均，这样学习率会对batch size不那么敏感
        SGD(params, learning_rate/batch_size)

        train_loss += nd.mean(loss).asscalar()#损失
        train_acc += accuracy(output, label)  #准确度

    test_acc = evaluate_accuracy(test_data, net)#验证数据集的准确度
    print("训练次数 %d. 损失Loss: %f, 训练准确度Train acc %f, 测试准确度Test acc %f" % (
        epoch, train_loss/len(train_data), train_acc/len(train_data), test_acc))


## 查看训练结果

data, label = mnist_test[0:10]#测试数据集前10个数据
show_images(data)#图片实例
print('true labels')#真实标签
print(get_text_labels(label))

predicted_labels = net(data).argmax(axis=1)#将预测概率最高的那个类作为预测的类
print('predicted labels')#预测标签 
print(get_text_labels(predicted_labels.asnumpy()))





