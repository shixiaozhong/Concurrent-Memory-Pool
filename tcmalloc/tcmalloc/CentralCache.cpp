#include"CentralCache.h"
#include"PageCache.h"


CentralCache CentralCache::_sInst;	

// 获取一个非空的Span
Span* CentralCache::GetOneSpan(SpanList& list, size_t size)
{
	// 遍历SpanList,先查看当前的spanlist中是否有未分配对象的span
	Span* it = list.Begin();
	while (it != list.End())
	{
		if (it->_freeList != nullptr)
		{
			return it;
		}
		else
		{
			it = it->_next;
		}
	}

	//先把CentralCache的桶锁解掉，这样如果其他线程释放内存回来了，不会阻塞住
	list._mtx.unlock();

	// 没有空闲Span了,就找PageCache要
	PageCache::GetInstance()->_pageMtx.lock();
	Span* span = PageCache::GetInstance()->NewSpan(SizeClass::NumMovePage(size));
	PageCache::GetInstance()->_pageMtx.unlock();

	// 计算大块内存span的起始地址和大小(字节数)
	char* start = (char*)(span->_pageId << PAGE_SHIFT);
	size_t bytes = span->_n << PAGE_SHIFT;
	char* end = start + bytes;
	// 将大块内存切成空闲链表并且连接起来
	// 先切一块下来
	span->_freeList = start;
	start += size;
	void* tail = span->_freeList;
	while (start < end)
	{
		NextObj(tail) = start;
		tail = start;
		start += size;
	}

	// 切好以后需要把span挂到桶上时，需要加锁
	list._mtx.lock();
	list.PushFront(span);
	return span;
}

size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size)
{
	size_t index = SizeClass::Index(size);
	_spanLists[index]._mtx.lock();	// 加锁
	       
	Span* span = GetOneSpan(_spanLists[index], size);
	assert(span);          
	assert(span->_freeList);
	// 从span中获取batchNum个对象
	// 如果不够batchNum个，有多少就拿多少
	start = span->_freeList;
	end = start;
	size_t i = 0;
	size_t actualNum = 1;	// 实际获取到的数量
	while (i < batchNum   - 1 && NextObj(end) != nullptr)
	{
		end = NextObj(end);
		i++;
		actualNum++;
	}
	span->_freeList = NextObj(end);
	NextObj(end) = nullptr;

	_spanLists[index]._mtx.unlock();	// 解锁

	return actualNum;
}
