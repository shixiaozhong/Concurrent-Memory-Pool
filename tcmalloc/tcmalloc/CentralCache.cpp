#include"CentralCache.h"
#include"PageCache.h"


// 在cpp文件中定义
CentralCache CentralCache::_sInst;

// 获取一个非空的Span
Span* CentralCache::GetOneSpan(SpanList& list, size_t size)
{
	// 查看当前的spanlist是否存在非空的span
	Span* it = list.Begin();
	while (it != list.End())
	{
		// 如果当前span有对象，直接返回
		if (it->_freelist)
		{
			return it;
		}
		it = it->_next;
	}

	//先把central cache的桶锁解掉，这样如果其他线程会释放内存，不会阻塞
	list._mtx.unlock();

	// 没有非空的span，就去找page cache要

	// 访问pagecache加锁
	PageCache::GetInstance()->_pageMtx.lock();

	Span* span = PageCache::GetInstance()->NewSpan(SizeClass::NumMovePage(size));
	span->_isUse = true;	// 只要是从pagecache中分过来的span，就代表已经被使用了
	span->_objSize = size;	// 保存小对象的size，用于后续释放空间
	// 解锁
	PageCache::GetInstance()->_pageMtx.unlock();


	// 对span进行切分不需要加锁，不存在竞争问题
	// 计算span的起始地址
	char* start = (char*)(span->_pageId << PAGE_SHIFT);
	// 计算span的大块内存的字节数
	size_t bytes = span->_n << PAGE_SHIFT;
	// 结尾地址
	char* end = start + bytes;

	// 将大块内存切为自由链表连接起来
	// 1. 先切一块下来做头，方便尾插
	span->_freelist = start;
	start += size;
	void* tail = span->_freelist;
	// 尾插
	while (start < end)
	{
		// 前四个字节的地址存储下一个块的地址，形成连接关系
		NextObj(tail) = start;
		tail = start;
		start += size;
	}
	// 最后一个要指向空
	NextObj(tail) = nullptr;

	// 这里要切好span以后，需要将切分好的span接到spanlist中，需要加锁
	list._mtx.lock();

	list.PushFront(span);

	return span;
}

// 从central cache中获取一定数量给thread cache
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t batch_num, size_t size)
{
	// 算出对应在哪个SpanList中拿
	size_t index = SizeClass::Index(size);
	// 加锁
	_spanLists[index]._mtx.lock();
	// 获取一个非空的span
	Span* span = GetOneSpan(_spanLists[index], size);
	assert(span);
	assert(span->_freelist);

	// 从span中获取batch_num个对象，如果没有batch_num个，有多少拿多少	
	start = span->_freelist;
	end = start;
	size_t actual_num = 1;
	size_t i = 0;
	while (i < batch_num - 1 && NextObj(end))
	{
		end = NextObj(end);
		i++;
		actual_num++;
	}
	span->_freelist = NextObj(end);
	NextObj(end) = nullptr;
	span->_useCount += actual_num;

	// 解锁
	_spanLists[index]._mtx.unlock();
	return actual_num;
}


// 
void CentralCache::RealaseListToSpans(void* start, size_t size)
{
	size_t index = SizeClass::Index(size);
	// 桶锁先加上
	_spanLists[index]._mtx.lock();
	// 遍历list
	while (start)
	{
		void* next = NextObj(start);

		Span* span = PageCache::GetInstance()->MapObjectToSpan(start);
		NextObj(start) = span->_freelist;
		span->_freelist = start;
		span->_useCount--;

		if (span->_useCount == 0)
		{
			// 说明span切分出去的所有小块内存都回来了，所以这个span的内存可以再回收给pagecache，pagecache可以再去做前后页的合并
			_spanLists[index].Erase(span);
			span->_freelist = nullptr;
			span->_next = nullptr;
			span->_prev = nullptr;

			// 将span的页归还给page cache
			// 
			// 可以先将central cache的桶锁解了，因为下面是操作pagecache，不再设计central cache
			_spanLists[index]._mtx.unlock();

			// 加上pagecache的大锁
			PageCache::GetInstance()->_pageMtx.lock();
			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
			PageCache::GetInstance()->_pageMtx.unlock();

			_spanLists[index]._mtx.lock();
		}
		start = next;
	}
	_spanLists[index]._mtx.unlock();
}