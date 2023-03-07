#pragma once
#include"Common.h"

class PageCache
{
private:
	SpanList _spanLists[NPAGES];
	static PageCache _sInst;
	std::unordered_map<PAGE_ID, Span*> _idSpanMap;	// 做一个页号到span的映射，便于回收内存放回到对应的span
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