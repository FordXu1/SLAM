#!/usr/bin/env python
#-*- coding:utf-8 -*-
# Python yield 使用浅析 
# https://www.ibm.com/developerworks/cn/opensource/os-cn-python-yield/


###############################################
#简单输出斐波那契數列前 N 个数
# 调用  python      import python_yield_fibonacci as fb
# fb.fab(8)
## 用 print 打印数字会导致该函数可复用性较差，因为 fab 函数返回 None，
#其他函数无法获得该函数生成的数列
def fab(max): 
   n, a, b = 0, 0, 1 
   while n < max: 
       print b 
       a, b = b, a + b 
       n = n + 1

##########################################################
##### 返回LIST以供调用
# 调用  python      import python_yield_fibonacci as fb
# fb.fab_list(8)
def fab_list(max): 
   n, a, b = 0, 0, 1 
   L = [] 
   while n < max: 
       L.append(b) 
       a, b = b, a + b 
       n = n + 1 
   return L
## 该函数在运行中占用的内存会随着参数 max 的增大而增大，
## 如果要控制内存占用，最好不要用 List来保存中间结果，而是通过 iterable 对象来迭代





##############################################
# 返回一个 iterable 对象
# for i in xrange(1000): pass不会生成一个 1000 个元素的 List，
# 而是在每次迭代中返回下一个数值，内存空间占用很小。
# 调用  python      import python_yield_fibonacci as fb
#>>> for n in fb.fab_iter(8):
#...     print n

class fab_iter(object): 
 
   def __init__(self, max): 
       self.max = max 
       self.n, self.a, self.b = 0, 0, 1 
 
   def __iter__(self): 
       return self 
 
   def next(self): #通过 next() 不断返回数列的下一个数，内存占用始终为常数
       if self.n < self.max: 
           r = self.b 
           self.a, self.b = self.b, self.a + self.b 
           self.n = self.n + 1 
           return r 
       raise StopIteration()



##################################
# yield  生成对象 generator object
'''
然而，使用 class 改写的这个版本，代码远远没有第一版的 fab 函数来得简洁。
如果我们想要保持第一版 fab 函数的简洁性，同时又要获得 iterable 的效果，
yield 就派上用场了
'''
# 调用  python      import python_yield_fibonacci as fb
#>>> for n in fb.fab_yield(8):
#        print n
def fab_yield(max): 
    n, a, b = 0, 0, 1 
    while n < max: 
        yield b 
        # print b 
        a, b = b, a + b 
        n = n + 1 



################################
# 使用 yield 文件读取 
# 直接对文件对象调用 read() 方法，会导致不可预测的内存占用
# 利用固定长度的缓冲区来不断读取文件内容
def read_file(fpath): 
   BLOCK_SIZE = 1024 #固定长度的缓冲区
   with open(fpath, 'rb') as f: # 以二进制形式读取 
       while True: 
           block = f.read(BLOCK_SIZE) 
           if block: #还有内容
               yield block #返回本次的一个block
           else: 
               return#没有内容则返回







