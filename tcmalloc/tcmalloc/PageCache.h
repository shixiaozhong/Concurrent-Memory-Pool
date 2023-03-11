#pragma once
#include"Common.h"
#include"ObjectPool.h"
#include"PageMap.h"

class PageCache
{
private:
	SpanList _spanLists[NPAGES];	// page cache�µ�spanlist����
	static PageCache _sInst;

	// ������STL����Ϊ�ײ�ʹ�õĻ���malloc����Ҫ����malloc
	//std::unordered_map<PAGE_ID, Span*> _idSpanMap;	// ��һ��ҳ�ŵ�span��ӳ�䣬���ڻ����ڴ�Żص���Ӧ��span
	TCMalloc_PageMap1<32 - PAGE_SHIFT> _idSpanMap;	// ʹ�û������ṩ�Ĺ�ϣӳ��
	
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

	// ��ȡ�Ӷ���span��ӳ��
	Span* MapObjectToSpan(void* obj);

	// ��ȡһ��kҳ��span
	Span* NewSpan(size_t k);

	// �ͷſ��е�span��pagecache�����Һϲ����ڵ�span
	void ReleaseSpanToPageCache(Span* span);
};