#pragma once
#include"Common.h"
#include"ObjectPool.h"
#include"PageMap.h"

class PageCache
{
private:
	SpanList _spanLists[NPAGES];	// page cache下的spanlist数组
	static PageCache _sInst;

	// 不适用STL，因为底层使用的还是malloc，需要脱离malloc
	//std::unordered_map<PAGE_ID, Span*> _idSpanMap;	// 做一个页号到span的映射，便于回收内存放回到对应的span
	TCMalloc_PageMap1<32 - PAGE_SHIFT> _idSpanMap;	// 使用基数树提供的哈希映射
	
	ObjectPool<Span> _spanPool;
private:
	PageCache() {};
	PageCache(const PageCache&) = delete;
public:
	std::mutex _pageMtx;

public:
	static PageCache* GetInstance()
	{
		return &_sInst;
	}

	// 获取从对象到span的映射
	Span* MapObjectToSpan(void* obj);

	// 获取一个k页的span
	Span* NewSpan(size_t k);

	// 释放空闲的span回pagecache，并且合并相邻的span
	void ReleaseSpanToPageCache(Span* span);
};