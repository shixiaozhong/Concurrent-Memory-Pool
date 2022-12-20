#pragma once
#include"Common.h"

// ֻ����һ��Central Cache�����Կ������Ϊ����ģʽ
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
	// ��ȡ����
	static CentralCache* getInstance()
	{
		return &_sInst;
	}
	Span* GetOneSpan(SpanList& list, size_t byte_size);
	size_t FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size);
};