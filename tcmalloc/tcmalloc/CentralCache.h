#pragma once

#include"Common.h"

// 保证全局只存在一个CentralCache，所以设计为单例模式
class CentralCache
{
private:
	static CentralCache _sInst;	// 单例对象
	SpanList _spanLists[NFREELISTS];	// central cache下的空闲链表数组

	// 构造和拷贝构造设置为私有，实现单例模式
	CentralCache() {};
	CentralCache(const CentralCache&) = delete;
public:
	// 单例
	static CentralCache* GetInstance()
	{
		return &_sInst;
	}

	// 从central cache中获取一定数量给thread cache
	size_t FetchRangeObj(void*& start, void*& end, size_t n, size_t size);

	// 获取一个非空的Span
	Span* GetOneSpan(SpanList& list, size_t size);

	//将thread cache下的过长的链表返回给central cache
	void RealaseListToSpans(void* start, size_t size);
};