#pragma once
#include"Common.h"


class PageCache
{
private:
	SpanList _spanLists[NPAGES];
	static PageCache _sInst;
public:
	std::mutex _pageMtx;
private:
	PageCache()
	{}
	PageCache(const PageCache&) = delete;
public:
	static PageCache* GetInstance()
	{
		return &_sInst;
	}

	// »ñÈ¡kÒ³µÄspan
	Span* NewSpan(size_t k);
};