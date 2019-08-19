# 混有单精度与二值的神经网络BinaryConnect

[BinaryConnect: Training Deep Neural Networks with binary weights](https://arxiv.org/pdf/1511.00363.pdf)

[论文笔记](https://blog.csdn.net/weixin_37904412/article/details/80618102)

[BinaryConnect 代码](https://github.com/Ewenwan/BinaryConnect)


      首先点燃战火的是Matthieu Courbariaux，
      他来自深度学习巨头之一的Yoshua Bengio领导的蒙特利尔大学的研究组。
      他们的文章于2015年11月出现在arxiv.org上。
      与此前二值神经网络的实验不同，Matthieu只关心系数的二值化，
      并采取了一种混和的策略，
      构建了一个混有单精度与二值的神经网络BinaryConnect：
      当网络被用来学习时，系数是单精度的，因此不会受量化噪声影响；
      而当被使用时，系数从单精度的概率抽样变为二值，从而获得加速的好处。
      这一方法在街拍门牌号码数据集(SVHN)上石破天惊地达到超越单精度神经网络的预测准确率，
      同时超越了人类水平，打破了此前对二值网络的一般印象，并奠定了之后一系列工作的基础。
      然而由于只有系数被二值化，Matthieu的BinaryConnect只能消减乘法运算，
      在CPU和GPU上一般只有2倍的理论加速比，但在FPGA甚至ASIC这样的专用硬件上则有更大潜力。
