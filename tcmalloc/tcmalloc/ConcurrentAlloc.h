#pragma once
#include"Common.h"
#include"ThreadCache.h"
#include"PageCache.h"
#include"ObjectPool.h"

// 申请内存
static void* ConcurrentAlloc(size_t size)
{
	if (size > MAX_BYTES)
	{
		// 申请字节数超过256KB
		// 对齐
		size_t align_size = SizeClass::RoundUp(size);
		size_t kpage = align_size >> PAGE_SHIFT;

		// 访问pagecache需要加锁
		PageCache::GetInstance()->_pageMtx.lock();
		Span* span = PageCache::GetInstance()->NewSpan(kpage);
		span->_objSize = size;	// 小对象大小存储在span中
		PageCache::GetInstance()->_pageMtx.unlock();


		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		return ptr;
	}
	else
	{
		// 通过TLS可以实现每个线程可以无锁的访问自己的ThreadCache
		if (pTLSThreadCache == nullptr)
		{
			// 设置为静态的，保证全局只有一个
			static ObjectPool<ThreadCache> tcPool;
			pTLSThreadCache = tcPool.New();
			//pTLSThreadCache = new ThreadCache();
		}
		//std::cout << std::this_thread::get_id() << ":" << pTLSThreadCache << std::endl;
		return pTLSThreadCache->Allocate(size);
	}
}

// 释放内存
static void ConcurrentFree(void* ptr)
{
	Span* span = PageCache::GetInstance()->MapObjectToSpan(ptr);
	size_t size = span->_objSize;	// 获取该span的小对象大小 

	if (size > MAX_BYTES)
	{
		// 堆中申请的空间释放
		
		// 找到对应的span
		
		// 加锁
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