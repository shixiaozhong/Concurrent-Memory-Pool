#define _CRT_SECURE_NO_WARNINGS 1

#include"ThreadCache.h"
#include"CentralCache.h"

void* ThreadCache::FecthFromCentralCache(size_t index, size_t size)
{
	// ��������ķ�Χ������ʼ���������㷨
	// 1.�ʼ������Central CacheҪ̫�࣬��������˷�
	// 2.����������ڴ�����batchNum��һֱ������ֱ������
	// 3.sizeԽ��һ����Central CacheҪ�ľ�Խ�٣�sizeԽС��Ҫ�ľ�Խ��
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
		// �����������
		_freeLists[index].PushRange(NextObj(start), end);
	}
	return start;
}


void* ThreadCache::Allocate(size_t size)
{
	assert(size <= 256 * 1024); // �����ڴ�С��256KB
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

