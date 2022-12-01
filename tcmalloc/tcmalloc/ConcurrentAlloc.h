#pragma once
#include"ThreadCache.h"
#include"Common.h"

static void* ConcurrentAlloc(size_t size)
{
	// ͨ��ÿ���߳�ר����TLS�����Ļ�ȡר����ThreadCache����
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