#pragma once
#include"Common.h"
#include"ThreadCache.h"

// 申请内存
static void* ConcurrentAlloc(size_t size)
{
	// 通过TLS可以实现每个线程可以无锁的访问自己的ThreadCache
	if (pTLSThreadCache == nullptr)
	{
		pTLSThreadCache = new ThreadCache();
	}
	std::cout << std::this_thread::get_id() << ":" << pTLSThreadCache << std::endl;
	return pTLSThreadCache->Allocate(size);
}

// 释放内存
static void ConcurrentFree(void* ptr, size_t size)
{
	assert(pTLSThreadCache);
	pTLSThreadCache->Deallocate(ptr, size);
}