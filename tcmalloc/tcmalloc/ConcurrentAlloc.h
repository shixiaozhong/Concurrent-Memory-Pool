#pragma once
#include"Common.h"
#include"ThreadCache.h"
#include"PageCache.h"
#include"ObjectPool.h"

// �����ڴ�
static void* ConcurrentAlloc(size_t size)
{
	if (size > MAX_BYTES)
	{
		// �����ֽ�������256KB
		// ����
		size_t align_size = SizeClass::RoundUp(size);
		size_t kpage = align_size >> PAGE_SHIFT;

		// ����pagecache��Ҫ����
		PageCache::GetInstance()->_pageMtx.lock();
		Span* span = PageCache::GetInstance()->NewSpan(kpage);
		span->_objSize = size;	// С�����С�洢��span��
		PageCache::GetInstance()->_pageMtx.unlock();


		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		return ptr;
	}
	else
	{
		// ͨ��TLS����ʵ��ÿ���߳̿��������ķ����Լ���ThreadCache
		if (pTLSThreadCache == nullptr)
		{
			// ����Ϊ��̬�ģ���֤ȫ��ֻ��һ��
			static ObjectPool<ThreadCache> tcPool;
			pTLSThreadCache = tcPool.New();
			//pTLSThreadCache = new ThreadCache();
		}
		//std::cout << std::this_thread::get_id() << ":" << pTLSThreadCache << std::endl;
		return pTLSThreadCache->Allocate(size);
	}
}

// �ͷ��ڴ�
static void ConcurrentFree(void* ptr)
{
	Span* span = PageCache::GetInstance()->MapObjectToSpan(ptr);
	size_t size = span->_objSize;	// ��ȡ��span��С�����С 

	if (size > MAX_BYTES)
	{
		// ��������Ŀռ��ͷ�
		
		// �ҵ���Ӧ��span
		
		// ����
		PageCache::GetInstance()->_pageMtx.lock();
		PageCache::GetInstance()->ReleaseSpanToPageCache(span);
		PageCache::GetInstance()->_pageMtx.unlock();
	}
	else
	{
		assert(pTLSThreadCache);
		pTLSThreadCache->Deallocate(ptr, size);
	}
}