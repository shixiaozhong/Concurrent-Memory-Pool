# Concurrent-Memory-Pool
一个高并发内存池的项目

[背景介绍](####背景介绍)

[整体框架设计](####整体框架设计)

[申请内存](####申请内存)

[回收内存](#回收内存)

[大于256KB内存申请](#大于256KB内存申请)

[使用定长内存池配合脱离使用new](#使用定长内存池配合脱离使用new)

[解决在释放过程中不需要传递对象大小的问题](#解决在释放过程中不需要传递对象大小的问题)

[使用基数树进行优化](#使用基数树进行优化)

#### 背景介绍

当前项目是实现一个高并发的内存池，他的原型是google的一个开源项目tcmalloc，tcmalloc全称Thread-Caching Malloc，即线程缓存的malloc，实现了高效的多线程内存管理，用于替代系统的内存分配相关的函数（malloc、free）。如果有看过侯捷老师的《STL源码剖析》的Allocator部分，有小部分其实有点相似，但是只有小部分，这个整体要复杂的多。

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

central cache也是一个哈希桶结构，他的哈希桶的映射关系跟thread cache是一样的。这样做的好处就是，当thread cache的某个桶中没有内存了，就可以直接到central cache中对应的哈希桶里去取内存就行了。不同的是他的每个哈希桶位置挂是SpanList链表结构，是一个带头双向循环链表，不过每个映射桶下面的span中的大内存块被按映射关系切成了一个个小内存块对象挂在span的自由链表中。

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

page cache与central cache一样，它们都是哈希桶的结构，并且page cache的每个哈希桶中里挂的也是一个个的span，这些span也是按照带头双向循环链表的结构链接起来的。

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

##### Thread Cache

对于thread cache的回收内存策略并不复杂，这里只考虑了自由链表过长的情况，就是当自由链表的长度超过了一次批量申请的长度时，就将一次批量的链表块从自由链表中截取下来，还给central cache的对应的span。

```c++
// 释放
void ThreadCache::Deallocate(void* ptr, size_t size)
{
	assert(ptr);
	assert(size <= MAX_BYTES);
	// 将释放的对象头插回对应的自由链表
	size_t index = SizeClass::Index(size);
	_freelists[index].Push(ptr);

	// 当链表的长度大于一次批量申请的长度时，就开始还一段给central cache
	if (_freelists[index].Size() >= _freelists[index].MaxSize())
	{
		ListTooLong(_freelists[index], size);
	}
}

void ThreadCache::ListTooLong(FreeList& list, size_t size)
{
	//将多余的块提取出来 
	void* start = nullptr;
	void* end = nullptr;
	list.PopRange(start, end, list.MaxSize());

	// 将这些内存还给central cache对应的span
	CentralCache::GetInstance()->RealaseListToSpans(start, size);
}
```

下面就是thread cache回收内存的主要代码实现，可以注意到在将链表中的数据截取出来的时候，其实整条链表都是被截取的，全部对象都没有了，不过因为poprange函数传了个size的参数，代表删除个数，如果不想全部删除，可以通过设置参数来确定，可修改性提高了。

这里的thread cache中只用了一个链表过长的判断来回收内存，其实还可以在增加一些判定的，例如增加一个内存大小的阈值来控制，超过了，就进行一部分回收，尽量避免一个线程占用较多内存，这个想法在tcmalloc中考虑到了的，实际的tcmalloc中考虑的很多。

##### Central Cache

在central cache下，回收的操作相对于thread cache要复杂一点，因为central cache下是以span为单位来维护内存的，所以从thread cache中还回的内存块，都需要还到对应的span中。而对于自由链表的块中，只知道的是地址，所以这里需要建立一个key-value数据结构，可以通过地址查找到对应的span，然后将其接到span的空闲链表中。

```c++
//将thread cache下的过长的链表返回给central cache
void CentralCache::RealaseListToSpans(void* start, size_t size)
{
	size_t index = SizeClass::Index(size);	// 获取对应spanlist的桶索引，因为central cache的结构与thread cache相同
	// 桶锁先加上
	_spanLists[index]._mtx.lock();
	// 遍历list
	while (start)
	{
		void* next = NextObj(start);
		Span* span = PageCache::GetInstance()->MapObjectToSpan(start);	// 获取内存块对应的span
		NextObj(start) = span->_freelist;	// 将该内存块头插到对应的span中的自由链表中。
		span->_freelist = start;
		span->_useCount--;	// span中的计数减少，如果减少到0，表示该span中切分出去的所有内存块都回来了
		// 所有的span都回来了，返回给page cache
		if (span->_useCount == 0)
		{
			// 说明span切分出去的所有小块内存都回来了，所以这个span的内存可以再回收给pagecache，pagecache可以再去做前后页的合并
			_spanLists[index].Erase(span);
			span->_freelist = nullptr;
			span->_next = nullptr;
			span->_prev = nullptr;

			// 将span的页归还给page cache
			 
			// 可以先将central cache的桶锁解了，因为下面是操作pagecache，不再涉及central cache
			_spanLists[index]._mtx.unlock();

			// 加上pagecache的大锁
			PageCache::GetInstance()->_pageMtx.lock();
			// 释放空闲的span返回给page cache
			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
			PageCache::GetInstance()->_pageMtx.unlock();

			// 将桶锁加上
			_spanLists[index]._mtx.lock();
		}
		start = next;
	}
	// 访问central cache完毕解除桶锁
	_spanLists[index]._mtx.unlock();
}
```

由于central cache所有的线程都可以访问，所以需要加锁。但是需要注意，单个线程只能访问一个桶，所以只需要加上桶锁，不需要加上一把大锁，反而影响其他线程访问其他的桶。在将thread cache中的内存块还给central cache中对应的桶过程中，检查该span是否所有的内存块都已经全部归坏，如果全部归还，就需要将central cache 中的该span还给page cache。此过程中，因为需要访问page cache，不需要访问central cache，所以可以将central cache的桶锁解除。

##### Page Cache

当central cache 中的span归还到page cache，page cache就需要进行对应的处理，主要将span添加到page cache下的spanlists中，但是因为span是分割出来，以页为单位的，所以需要考虑空闲页合并的情况，进行前后页的合并，形成更大的空闲页，因为在page cache下存在多个线程访问同一个spanlist的情况，就是进行大页切分和空闲页合并的情况，所以需要在page cache中加上一把大锁。

```c++
// 将空闲的span返回给page cache
void PageCache::ReleaseSpanToPageCache(Span* span)
{
	// 释放的空间大于128页
	if (span->_n > NPAGES - 1)
	{
		// span的页数量大于128,不是找pagecache要的
		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		// 使用系统提供的接口释放
		SystemFree(ptr);
		//delete span;
		_spanPool.Delete(span);
		return;
	}


	// 对span的前后页尝试进行合并，缓解内存碎片问题
	// 向前合并
	while (true)
	{
		PAGE_ID prev_id = span->_pageId - 1;
		//auto ret = _idSpanMap.find(prev_id);
		//// 不存在前面的页号
		//if (ret == _idSpanMap.end())
		//{
		//	break;
		//}
		Span* ret = (Span*)_idSpanMap.get(prev_id);// 通过key-value的数据结构来找到前面页对应的span
		if (ret == nullptr)
		{
			break;
		}

		// 存在但是已经被使用了
		Span* prev_span = ret;
		if (prev_span->_isUse == true)
		{
			break;
		}
		// 合并的页超过128也不好管理
		if (prev_span->_n + span->_n > NPAGES - 1)
		{
			break;
		}
		// 进行空闲页的合并
		span->_pageId = prev_span->_pageId; // 更新pageId
		span->_n += prev_span->_n;	// 更新大小
		_spanLists[prev_span->_n].Erase(prev_span);	// 删除前面的被合并的span
		// 释放掉prev_span
		//delete prev_span;
		_spanPool.Delete(prev_span);	// 释放掉前面的span的结构
	}

	// 向后合并
	while (true)
	{
		// 找到后面span的起始页号
		PAGE_ID next_id = span->_pageId + span->_n;

		/*auto ret = _idSpanMap.find(next_id);
		if (ret == _idSpanMap.end())
		{
			break;
		}*/
		Span* ret =  (Span*)_idSpanMap.get(next_id);// 通过key-value的数据结构来找到前面页对应的span 
		if (ret == nullptr)
			break;

		// 存在但是在使用
		Span* next_span = ret;
		if (next_span->_isUse == true)
		{
			break;
		}

		// 合并过后的页大于128页
		if (span->_n + next_span->_n > NPAGES - 1)
		{
			break;
		}
		// 进行后页的合并
		span->_n += next_span->_n;	// 更新页的数量，这里不需要更新页号，因为是合并后面的页
		_spanLists[next_span->_n].Erase(next_span);	// 删除前面的被合并的span 
		//delete next_span;
		_spanPool.Delete(next_span); // 释放掉后面的span的结构
	}

	_spanLists[span->_n].PushFront(span);	// 合并完毕后，将对应大小的span挂到对应的spanlist中
	span->_isUse = false;	// 标记为未使用

	//_idSpanMap[span->_pageId] = span;
	_idSpanMap.set(span->_pageId, span);	// 更新key-value的结构，建立首页与span的映射
	//_idSpanMap[span->_pageId + span->_n - 1] = span;
	_idSpanMap.set(span->_pageId + span->_n - 1, span);	// 更新key-value结构，建立尾页与span的映射
}
```

需要注意的是，在向前或向后进行合并的过程中：

* 如果没有通过页号获取到其对应的span，说明对应到该页的内存块还未申请，此时需要停止合并。
* 如果通过页号获取到了其对应的span，但该span处于被使用的状态，那我们也必须停止合并。
* 如果合并后大于128页则不能进行本次合并，因为page cache无法对大于128页的span进行管理。

至此一个完整的回收内存过程就结束了。

#### 大于256KB内存申请

对于之前的结构，申请的内存的大小该向谁要做个总结

* x<=256kb(32页)：向thread cache要。
* 32页<x<=128页：直接向page cache要。
* x > 128页：向堆申请。

超过256kb的申请直接按页进行对齐，需要在对应的地方添加大块内存申请。超过256kb的内存申请，统一去page cache处理。

```c++
// 申请内存
static void* ConcurrentAlloc(size_t size)
{
	// 申请字节数超过256KB
	if (size > MAX_BYTES)
	{
		// 对齐
		size_t align_size = SizeClass::RoundUp(size);
		size_t kpage = align_size >> PAGE_SHIFT;

		// 访问pagecache需要加锁
		PageCache::GetInstance()->_pageMtx.lock();
		// 调用NewSpan来申请大块内存，对于大块内存的申请和释放都是统一放在page cache中处理的
		Span* span = PageCache::GetInstance()->NewSpan(kpage);
		span->_objSize = size;	// 小对象大小存储在span中
		PageCache::GetInstance()->_pageMtx.unlock();


		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);	// 页号反推出地址
		return ptr;
	}
	else
	{
		// 通过TLS可以实现每个线程可以无锁的访问自己的ThreadCache
		if (pTLSThreadCache == nullptr)
		{
			// 设置为静态的，保证全局只有一个
			static ObjectPool<ThreadCache> tcPool;
			pTLSThreadCache = tcPool.New();
			//pTLSThreadCache = new ThreadCache();
		}
		//std::cout << std::this_thread::get_id() << ":" << pTLSThreadCache << std::endl;
		return pTLSThreadCache->Allocate(size);
	}
}
```

```c++
// 获取一个k页的span
Span* PageCache::NewSpan(size_t k)
{
	assert(k > 0);

	// 申请的页数超过128
	if (k > NPAGES - 1)
	{
		// 大于128页就向堆申请 
		void* ptr = SystemAlloc(k);
		//Span* span = new Span;
		Span* span = _spanPool.New();	// 使用定长内存池来替换掉new
		span->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
		span->_n = k;
		//_idSpanMap[span->_pageId] = span;	// 将起始页号和span关联起来
		_idSpanMap.set(span->_pageId, span);
		return span;
	}
```

超过128页直接向堆申请，小于等于128页从page cache中取出。同样的内存的释放也是在page cache中处理的，超过128页直接释放给堆，小于等于128页就直接归还给page cache。

```c++
// 将空闲的span返回给page cache
void PageCache::ReleaseSpanToPageCache(Span* span)
{
	// 释放的空间大于128页
	if (span->_n > NPAGES - 1)
	{
		// span的页数量大于128,不是找pagecache要的
		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		// 使用系统提供的接口释放
		SystemFree(ptr);
		//delete span;
		_spanPool.Delete(span);
		return;
	}

```

#### 使用定长内存池配合脱离使用new

因为在new中底层也是使用malloc来申请内存空间的，所以需要脱离掉new的使用，使用一种方式来代替掉new。这里使用定长内存池来替换掉new。

```c++
// 定长内存池
template<class T>
class ObjectPool
{
public:
	T* New()
	{
		T* obj = nullptr;

		// 优先把还回来内存块对象，再次重复利用
		if (_freeList)
		{
			// 头删一块
			void* next = *((void**)_freeList);
			obj = (T*)_freeList;
			_freeList = next;
		}
		else
		{
			// 剩余内存不够一个对象大小时，则重新开大块空间
			if (_remainBytes < sizeof(T))
			{
				_remainBytes = 128 * 1024;	// 申请128kb
				//_memory = (char*)malloc(_remainBytes);
				_memory = (char*)SystemAlloc(_remainBytes >> 13);// 直接向堆申请
				if (_memory == nullptr)
				{
					throw std::bad_alloc();	// 申请失败抛出异常
				}
			}

			obj = (T*)_memory;	// 从新申请的内存中取出一块
			size_t objSize = sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T);
			_memory += objSize;
			_remainBytes -= objSize;
		}

		// 定位new，显示调用T的构造函数初始化
		new(obj)T;

		return obj;
	}

	void Delete(T* obj)
	{
		// 显示调用析构函数清理对象
		obj->~T();

		// 头插
		*(void**)obj = _freeList;
		_freeList = obj;
	}

private:
	char* _memory = nullptr; // 指向大块内存的指针
	size_t _remainBytes = 0; // 大块内存在切分过程中剩余字节数

	void* _freeList = nullptr; // 还回来过程中链接的自由链表的头指针
};
```

#### 解决在释放过程中不需要传递对象大小的问题

在上面释放内存时是不可以像free一样进行释放内存的，只需要传递一个指针，还需要传递一个对象的大小，这也是一个需要解决的问题，但是并不难解决，只需要在切分span之前标记好对象的大小就行。

<img src="http://shixiaozhong.oss-cn-hangzhou.aliyuncs.com/img/image-20230316142149560.png" alt="image-20230316142149560" style="zoom:67%;" />

<img src="http://shixiaozhong.oss-cn-hangzhou.aliyuncs.com/img/image-20230316142318866.png" alt="image-20230316142318866" style="zoom:67%;" />

#### 使用基数树进行优化

由于需要使用key-value的结构来维护页号到span的映射关系，所以第一下想到的就是使用STL中的map，但是map底层使用的是malloc来申请空间，所以需要想到一个替换方法，就是使用基数树来替换掉map，构建一个key-value的映射结构。

一层基数树

单层基数树实际采用的就是直接定址法，每一个页号对应span的地址就存储数组中在以该页号为下标的位置。32位下一次开辟2^19* 2^2空间大小的数组，用来维护key-value结构。

```c++
// Single-level array
template <int BITS>
class TCMalloc_PageMap1 {
private:
	static const int LENGTH = 1 << BITS;// 设置映射关系的数目，例如32位下，每页8kb，最多有2^19个页
	void** array_;

public:
	typedef uintptr_t Number; // uintptr_t是unsiged int typedef的

	// 构造函数
	explicit TCMalloc_PageMap1() {
		size_t size = sizeof(void*) << BITS;		// 需要开辟数组的大小
		size_t alignSize = SizeClass::_RoundUp(size, 1 << PAGE_SHIFT);	// 按页对齐，一页是8kb
		array_ = (void**)SystemAlloc(alignSize >> PAGE_SHIFT);	// 向堆申请内存
		memset(array_, 0, sizeof(void*) << BITS);	// 将数组初始化为0
	}

	// Return the current value for KEY.  Returns NULL if not yet set,
	// or if k is out of range.
	// 在映射的表中查找
	void* get(Number k) const 
	{
		if ((k >> BITS) > 0) // 不是合法页号，例如32平台下，页号只需要19位来存储，右移19位如果大于0，表示不是合法页号
		{
			return NULL;
		}
		return array_[k];
	}

	// REQUIRES "k" is in range "[0,2^BITS-1]".
	// REQUIRES "k" has been ensured before.
	//
	// Sets the value 'v' for key 'k'.
	// 传入页号和span，构建映射关系
	void set(Number k, void* v)
	{
		array_[k] = v;
	}
};
```

二层基数树

二层基数树实在一层的基础上加了一层，将页号分为前五位作为第一层索引和后面的若干位作为第二层索引。

```c++
// Two-level radix tree
template <int BITS>
class TCMalloc_PageMap2 {
private:
	// Put 32 entries in the root and (2^BITS)/32 entries in each leaf.
	static const int ROOT_BITS = 5;
	static const int ROOT_LENGTH = 1 << ROOT_BITS; // 32 

	static const int LEAF_BITS = BITS - ROOT_BITS;	// 14位
	static const int LEAF_LENGTH = 1 << LEAF_BITS;	// 2^14

	// root层中每个单位存储的元素类型
	struct Leaf {
		void* values[LEAF_LENGTH];
	};

	Leaf* root_[ROOT_LENGTH];             // root层的数组

public:
	typedef uintptr_t Number;

	// 构造函数
	explicit TCMalloc_PageMap2() {
		memset(root_, 0, sizeof(root_));	// 将root数组置为NULL
		PreallocateMoreMemory();
	}

	void* get(Number k) const 
	{
		const Number i1 = k >> LEAF_BITS;			// 获取对应的root层对应的下标
		const Number i2 = k & (LEAF_LENGTH - 1);	// 获取对应的第二层的下标
		//页号不在范围内或者没有建立映射关系
		if ((k >> BITS) > 0 || root_[i1] == NULL) 
		{
			return NULL;
		}
		return root_[i1]->values[i2];
	}

	void set(Number k, void* v)
	{
		const Number i1 = k >> LEAF_BITS;			// 获取对应的root层对应的下标
		const Number i2 = k & (LEAF_LENGTH - 1);	// 获取对应的第二层的下标
		assert(i1 < ROOT_LENGTH);	// 断言是否在root的大小之内
		root_[i1]->values[i2] = v;	// 建立映射关系
	}

	bool Ensure(Number start, size_t n) {
		for (Number key = start; key <= start + n - 1;) 
		{
			const Number i1 = key >> LEAF_BITS;	// 获取root层对应的下标

			// 超过了root层的长度
			if (i1 >= ROOT_LENGTH)
				return false;

			// Make 2nd level node if necessary
			// 第一层i1指向的空间未开辟
			if (root_[i1] == NULL) {
				static ObjectPool<Leaf>	leafPool;
				Leaf* leaf = (Leaf*)leafPool.New();	// 使用定长内存池开辟空间

				memset(leaf, 0, sizeof(*leaf));		// 将leaf置为NULL
				root_[i1] = leaf;					// 将leaf保存到root中
			}

			// Advance key past whatever is covered by this leaf node
			key = ((key >> LEAF_BITS) + 1) << LEAF_BITS;	// 继续向后检查，对应的位++
		}
		return true;
	}

	void PreallocateMoreMemory() {
		// Allocate enough to keep track of all possible pages
		Ensure(0, 1 << BITS);
	}
};
```
