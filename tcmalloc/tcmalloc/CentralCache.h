#pragma once
#include"Common.h"

// 只存在一个Central Cache，所以可以设计为单例模式
class CentralCache
{
private:
	SpanList _spanLists[NFREE_LISTS];
private:
	static CentralCache _sInst;
	CentralCache() 
	{}
	CentralCache(const CentralCache&) = delete;
public:
	// 获取单例
	static CentralCache* getInstance()
	{
		return &_sInst;
	}
	Span* GetOneSpan(SpanList& list, size_t byte_size);
	size_t FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size);
};