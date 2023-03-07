# Concurrent-Memory-Pool
一个高并发内存池的项目

[TOC]



#### 背景介绍

当前项目是实现一个高并发的内存池，他的原型是google的一个开源项目tcmalloc，tcmalloc全称Thread-Caching Malloc，即线程缓存的malloc，实现了高效的多线程内存管理，用于替代系统的内存分配相关的函数（malloc、free）。
我们这个项目是把tcmalloc最核心的框架简化后拿出来，模拟实现出一个自己的高并发内存池，目的就是学习tcamlloc的精华。如果有同学看过侯捷老师的《STL源码剖析》的Allocator部分，有小部分其实有点相似，但是只有小部分，这个整体要复杂的多。

**前置知识**

这个项目会用到C/C++、数据结构（链表、哈希桶）、操作系统内存管理、单例模式、多线程、互斥锁等等方面的知识。

**目的**

内存池主要解决的当然是效率的问题，其次如果作为系统的内存分配器的角度，还需要解决一下内存碎片的问题。那么什么是内存碎片呢？贴一张csapp的书一段，一般操作系统的书都会讲到内存碎片的概念的。

<img src="http://shixiaozhong.oss-cn-hangzhou.aliyuncs.com/img/image-20221129111616226.png" alt="image-20221129111616226" style="zoom:50%;" />

碎片：造成堆利用率很低的主要原因是一种称为**碎片** (fragmentation) 的现象，**当虽然有未使用的内存但不能用来满足分配请求时，就发生这种现象**。有两种形式的碎片：**内部碎片**(internal fragmentation) 和**外部碎片** (external fragmentation) 。

* **内部碎片**：内部碎片是在一个已分配块比有效载荷大时发生的。
* **外部碎片**：外部碎片是当空闲内存合计起来足够满足一个分配请求，但是没有一个单独的空闲块足够大可以来处理这个请求时发生的。

内部碎片一般是由于内存对齐分配造成的，造成一些分配出去的空间没有被利用。外部碎片一般是由于一些内存释放导致的，一块连续的内存分配给不同的对象，一些对象提前释放了内存，就容易导致外部碎片的出现。

![image-20221129113737094](http://shixiaozhong.oss-cn-hangzhou.aliyuncs.com/img/image-20221129113737094.png)

---

#### 整体框架设计

现代很多的开发环境都是多核多线程，在申请内存的场景下，必然存在激烈的锁竞争问题。malloc本身其实已经很优秀，那么我们项目的原型tcmalloc就是在多线程高并发的场景下更胜一筹，所以这次我们实现的内存池需要考虑以下几方面的问题。

* 性能问题
* 多线程环境下锁竞争的问题
* 内存碎片的问题

**concurrent memory pool**主要由以下3个部分构成：

1. **thread cache**：线程缓存是每个线程独有的，用于小于256KB的内存的分配，线程从这里申请内存不需要加锁，每个线程独享一个cache，这也就是这个并发线程池高效的地方。
2. **central cache**：中心缓存是所有线程所共享，thread cache是按需从central cache中获取的对象。central cache合适的时机回收thread cache中的对象，避免一个线程占用了太多的内存，而其他线程的内存吃紧，达到内存分配在多个线程中更均衡的按需调度的目的。central cache是存在竞争的，所以从这里取内存对象是需要加锁，首先这里用的是桶锁，其次只有thread cache的没有内存对象时才会找central cache，所以这里竞争不会很激烈。
3. **page cache**：页缓存是在central cache缓存上面的一层缓存，存储的内存是以页为单位存储及分配的，central cache没有内存对象时，从page cache分配出一定数量的page，并切割成定长大小的小块内存，分配给central cache。当一个span的几个跨度页的对象都回收以后，page cache会回收central cache满足条件的span对象，并且合并相邻的页，组成更大的页，缓解内存碎片的问题。

![image-20221129102359365](http://shixiaozhong.oss-cn-hangzhou.aliyuncs.com/img/image-20221129102359365.png)

---

#### 申请内存

##### Thread Cache

thread cache是哈希桶结构，每个桶是一个按桶位置映射大小的内存块对象的自由链表。每个线程都会私有一个thread cache对象，这样每个线程在这里获取对象和释放对象时是无锁的。

**申请内存：**

1. 当内存申请size<=256KB，先获取到线程本地存储的thread cache对象，计算size映射的哈希桶自由链表下标i。
2. 如果自由链表_freeLists[i]中有对象，则直接Pop一个内存对象返回。
3. 如果_freeLists[i]中没有对象时，则批量从central cache中获取一定数量的对象，插入到自由链表并返回一个对象。

先看看thread  cache的基本结构：

<img src="http://shixiaozhong.oss-cn-hangzhou.aliyuncs.com/img/image-20230305140631461.png" alt="image-20230305140631461" style="zoom: 67%;" />

有一个自由链表的数组，就是申请空间的来源，每一次申请空间就从对应的自由链表中取出一块。所以如何设计自由链表的结构便于申请空间就非常重要。做法为设计成字节对齐的格式，减少内部碎片的问题。

![image-20230305141145587](http://shixiaozhong.oss-cn-hangzhou.aliyuncs.com/img/image-20230305141145587.png)

设计为此结构，可以整体控制在10%的内部碎片浪费。所以thread cache的结构如下图所示。

<img src="http://shixiaozhong.oss-cn-hangzhou.aliyuncs.com/img/image-20230305143630246.png" alt="image-20230305143630246" style="zoom:67%;" />

构建成这样结构后，也需要处理好哈希桶与申请对象的大小的对应关系，例如，申请1B,就将其对齐到8B，直接分配一个8B的对象。

**TLS--thread local storage**：

线程局部存储，因为每个线程都需要私有一个thread cache，所以需要给每个线程定义一个，并且是每个线程私有的，不允许其他线程访问， 同时也就不需要加锁来维护，所以需要使用TLS技术来实现。TLS的使用可以参考这篇博客 [线程局部存储tls的使用](https://developer.aliyun.com/article/614746)。

---

##### Central Cache

central cache也是一个哈希桶结构，他的哈希桶的映射关系跟thread cache是一样的。这样做的好处就是，当thread cache的某个桶中没有内存了，就可以直接到central cache中对应的哈希桶里去取内存就行了。不同的是他的每个哈希桶位置挂是SpanList链表结构，不过每个映射桶下面的span中的大内存块被按映射关系切成了一个个小内存块对象挂在span的自由链表中。

**申请内存：**

1. 当thread cache中没有内存时，就会批量向central cache申请一些内存对象，这里的批量获取对象的数量使用了类似网络tcp协议拥塞控制的慢开始算法；central cache也有一个哈希映射的spanlist，spanlist中挂着span，**从span中取出对象给thread cache，这个过程是需要加锁的，不过这里使用的是一个桶锁，尽可能提高效率。**
2. central cache映射的spanlist中所有span的都没有内存以后，则**需要向page cache申请一个新的span对象**，拿到span以后将span管理的内存按大小切好作为自由链表链接到一起。然后从span中取对象给thread cache。
3. central cache的中挂的span中use_count记录分配了多少个对象出去，分配一个对象给threadcache，就++use_count。

**与thread cache的区别**

1. threadcache是每个线程私有的，在访问过程中不需要加锁，但是central cache是共享的，所以在访问中需要加锁，但是因为是哈希桶的结构，并且每个线程只会访问一个桶，所以只需要桶锁即可，即对每个spanList加锁。
2. thread cache中，每条自由链表都是切好的小块内存，但是central cache中是一个一个的大块内存span，当在thread cache需要时，才会进行对应的切分。

<img src="http://shixiaozhong.oss-cn-hangzhou.aliyuncs.com/img/image-20230305151632953.png" alt="image-20230305151632953" style="zoom:67%;" />

---

##### PageCache

page cache与central cache一样，它们都是哈希桶的结构，并且page cache的每个哈希桶中里挂的也是一个个的span，这些span也是按照双链表的结构链接起来的。

**申请内存：**

1. 当central cache向page cache申请内存时，page cache先检查对应位置有没有span，如果没有则向更大页寻找一个span，如果找到则分裂成两个。比如：申请的是4页page，4页page后面没有挂span，则向后面寻找更大的span，假设在10页page位置找到一个span，则将10页pagespan分裂为一个4页page span和一个6页page span。
2. 如果找到_spanList[128]都没有合适的span，则向系统使用mmap、brk或者是VirtualAlloc等方式申请128页page span挂在自由链表中，再重复1中的过程。
3. 需要注意的是central cache和page cache 的核心结构都是spanlist的哈希桶，但是他们是有本质区别的，central cache中哈希桶，是按跟thread cache一样的大小对齐关系映射的，他的spanlist中挂的span中的内存都被按映射关系切好链接成小块内存的自由链表。

**与上两层结构的不同之处：**

1. pagecache的映射规则和threadcache，central cache不同， 是针对申请的页数，范围为[1, 128]，每一个数字对应一个spanlist，每个spanlist下连接着n页的span，1下面连接的都是1页大小的span，10下面都连接着10页大小的span。一直到128页。
2. 加锁的过程不同，thread cache不需要加锁，central cache是桶锁，但是page cache是一把大锁，因为在申请span时，会存在向上寻找大页span的过程，一个线程可能会访问多个桶，就存在线程之间的竞争问题，如果是以桶锁的方式来加锁，就可能存在频繁的加锁解锁，效率不高，还不如直接一把大锁解决；

<img src="http://shixiaozhong.oss-cn-hangzhou.aliyuncs.com/img/image-20230305152416707.png" alt="image-20230305152416707" style="zoom:67%;" />

---

#### 回收内存



