#define _CRT_SECURE_NO_WARNINGS 1

#include"ThreadCache.h"
#include"CentralCache.h"

void* ThreadCache::FecthFromCentralCache(size_t index, size_t size)
{
	// 控制申请的范围，慢开始反馈调节算法
	// 1.最开始不会向Central Cache要太多，可能造成浪费
	// 2.如果不断有内存需求，batchNum会一直增长，直到上限
	// 3.size越大，一次向Central Cache要的就越少，size越小，要的就越多
	size_t batchNum = min(SizeClass::NumMoveSize(size), _freeLists[index].MaxSize());
	if (batchNum == _freeLists[index].MaxSize())
		_freeLists[index].MaxSize() += 1;

	void* start = nullptr;
	void* end = nullptr;
	size_t actualNum = CentralCache::getInstance()->FetchRangeObj(start, end, batchNum, size);
	
	assert(actualNum > 1);
	if (actualNum == 1)
	{
		assert(start == end);
	}
	else
	{
		// 插入空闲链表
		_freeLists[index].PushRange(NextObj(start), end);
	}
	return start;
}


void* ThreadCache::Allocate(size_t size)
{
	assert(size <= 256 * 1024); // 申请内存小于256KB
	size_t align_size = SizeClass::RoundUp(size);
	size_t index = SizeClass::Index(size);
	if (!_freeLists[index].Empty())
	{
		return _freeLists[index].Pop();
	}
	else
	{
		return FecthFromCentralCache(index, size);
	}
}

void ThreadCache::Deallocate(void* ptr, size_t size)
{
	assert(ptr);
	assert(size <= MAX_BYTES);
	size_t index = SizeClass::Index(size);
	_freeLists[index].Push(ptr);
}

