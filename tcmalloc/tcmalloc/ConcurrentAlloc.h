#pragma once
#include"Common.h"
#include"ThreadCache.h"

// �����ڴ�
static void* ConcurrentAlloc(size_t size)
{
	// ͨ��TLS����ʵ��ÿ���߳̿��������ķ����Լ���ThreadCache
	if (pTLSThreadCache == nullptr)
	{
		pTLSThreadCache = new ThreadCache();
	}
	std::cout << std::this_thread::get_id() << ":" << pTLSThreadCache << std::endl;
	return pTLSThreadCache->Allocate(size);
}

// �ͷ��ڴ�
static void ConcurrentFree(void* ptr, size_t size)
{
	assert(pTLSThreadCache);
	pTLSThreadCache->Deallocate(ptr, size);
}