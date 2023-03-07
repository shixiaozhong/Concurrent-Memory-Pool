#include"ThreadCache.h"
#include"CentralCache.h"


// 向CentralCache申请
void* ThreadCache::FetchFromCentralCache(size_t index, size_t size)
{
	// 慢开始反馈调节算法
	// 最开始不会向central cache要太多，随着要的次数，逐渐增加，直到上限
	size_t batch_num = min(_freelists[index].MaxSize(), SizeClass::NumMoveSize(size));
	if (_freelists[index].MaxSize() == batch_num)
	{
		_freelists[index].MaxSize() += 1;
	}
	void* start = nullptr;
	void* end = nullptr;
	// 实际从central cache中取到的个数
	size_t actual_num = CentralCache::GetInstance()->FetchRangeObj(start, end, batch_num, size);
	// 至少给一个
	assert(actual_num > 0);

	// 申请的只有一个
	if (actual_num == 1)
	{
		assert(start == end);
		// 直接将这个返回
		return start;
	}
	else
	{
		// 申请多个就将除了第一个其他的全部插入到自由链表中
		_freelists[index].PushRange(NextObj(start), end, actual_num - 1);
		return start;
	}
	return nullptr;
}


// 申请
void* ThreadCache::Allocate(size_t size)
{
	assert(size <= MAX_BYTES);
	// 处理对齐
	size_t align_size = SizeClass::RoundUp(size);
	// 寻找对应的桶
	size_t index = SizeClass::Index(size);
	if (!_freelists[index].Empty())
	{
		return _freelists[index].Pop();
	}
	else
	{
		// 对应自由链表中没有数据，就向CentralCache申请
		return FetchFromCentralCache(index, align_size);
	}
}

// 释放
void ThreadCache::Deallocate(void* ptr, size_t size)
{
	assert(ptr);
	assert(size <= MAX_BYTES);
	// 将释放的对象头插回对应的自由链表
	size_t index = SizeClass::Index(size);
	_freelists[index].Push(ptr);

	// 当链表的长度大于一次批量申请的长度时，就开始还一段地址给central cache
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