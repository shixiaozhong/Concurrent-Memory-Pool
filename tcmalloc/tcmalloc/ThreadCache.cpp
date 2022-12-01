#define _CRT_SECURE_NO_WARNINGS 1

#include"ThreadCache.h"


void* ThreadCache::FecthFromCentralCache(size_t index, size_t size)
{
	// TODO
	return nullptr;
}


void* ThreadCache::Allocate(size_t size)
{
	assert(size <= 256 * 1024); // ÉêÇëÄÚ´æÐ¡ÓÚ256KB
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

