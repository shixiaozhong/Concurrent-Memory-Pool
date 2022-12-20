#include"CentralCache.h"

CentralCache CentralCache::_sInst;	

// 获取一个非空的Span
Span* CentralCache::GetOneSpan(SpanList& list, size_t byte_size)
{
	//TODO
	return nullptr;
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
