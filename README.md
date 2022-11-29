# Concurrent-Memory-Pool
一个高并发内存池的项目

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

#### Thread Cache

thread cache是哈希桶结构，每个桶是一个按桶位置映射大小的内存块对象的自由链表。每个线程都会有一个thread cache对象，这样每个线程在这里获取对象和释放对象时是无锁的。

**申请内存：**

1. 当内存申请size<=256KB，先获取到线程本地存储的thread cache对象，计算size映射的哈希桶自由链表下标i。
2. 如果自由链表_freeLists[i]中有对象，则直接Pop一个内存对象返回。
3. 如果_freeLists[i]中没有对象时，则批量从central cache中获取一定数量的对象，插入到自由链表并返回一个对象。

**释放内存：**

1. 当释放内存小于256kb时将内存释放回thread cache，计算size映射自由链表桶位置i，将对象Push到_freeLists[i]。
2. 当链表的长度过长，则回收一部分内存对象到central cache。

![image-20221129110224440](http://shixiaozhong.oss-cn-hangzhou.aliyuncs.com/img/image-20221129110224440.png)

**TLS--thread local storage**：

线程局部存储，因为每个线程都需要私有一个thread cache，所以需要给每个线程定义一个，并且是每个线程私有的，不允许其他线程访问，， 同时也就不需要加锁来维护，所以需要使用TLS技术来实现。TLS的使用可以参考这篇博客 [线程局部存储tls的使用](https://developer.aliyun.com/article/614746)。

---

#### Central Cache
