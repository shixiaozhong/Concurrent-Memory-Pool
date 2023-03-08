#pragma once
#include"Common.h"
#include"ObjectPool.h"


class PageCache
{
private:
	SpanList _spanLists[NPAGES];
	static PageCache _sInst;
	std::unordered_map<PAGE_ID, Span*> _idSpanMap;	// ��һ��ҳ�ŵ�span��ӳ�䣬���ڻ����ڴ�Żص���Ӧ��span

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