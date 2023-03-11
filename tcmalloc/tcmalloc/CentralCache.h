#pragma once

#include"Common.h"

// ��֤ȫ��ֻ����һ��CentralCache���������Ϊ����ģʽ
class CentralCache
{
private:
	static CentralCache _sInst;	// ��������
	SpanList _spanLists[NFREELISTS];	// central cache�µĿ�����������

	// ����Ϳ�����������Ϊ˽�У�ʵ�ֵ���ģʽ
	CentralCache() {};
	CentralCache(const CentralCache&) = delete;
public:
	// ����
	static CentralCache* GetInstance()
	{
		return &_sInst;
	}

	// ��central cache�л�ȡһ��������thread cache
	size_t FetchRangeObj(void*& start, void*& end, size_t n, size_t size);

	// ��ȡһ���ǿյ�Span
	Span* GetOneSpan(SpanList& list, size_t size);

	//��thread cache�µĹ����������ظ�central cache
	void RealaseListToSpans(void* start, size_t size);
};