#pragma once
#include"ThreadCache.h"
#include"Common.h"

static void* ConcurrentAlloc(size_t size)
{
	// 通过每个线程专属的TLS无锁的获取专属的ThreadCache对象
	if (pTLSThreadCache == nullptr)
	{
		pTLSThreadCache = new ThreadCache;
	}

	cout << std::this_thread::get_id() << ":" << pTLSThreadCache << endl;

	return pTLSThreadCache->Allocate(size);
}

static void ConcurrentFree(void* ptr, size_t size)
{
	assert(pTLSThreadCache);
	pTLSThreadCache->Deallocate(ptr, size);
}