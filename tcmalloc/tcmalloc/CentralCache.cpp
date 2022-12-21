#include"CentralCache.h"
#include"PageCache.h"


CentralCache CentralCache::_sInst;	

// ��ȡһ���ǿյ�Span
Span* CentralCache::GetOneSpan(SpanList& list, size_t size)
{
	// ����SpanList,�Ȳ鿴��ǰ��spanlist���Ƿ���δ��������span
	Span* it = list.Begin();
	while (it != list.End())
	{
		if (it->_freeList != nullptr)
		{
			return it;
		}
		else
		{
			it = it->_next;
		}
	}

	//�Ȱ�CentralCache��Ͱ�������������������߳��ͷ��ڴ�����ˣ���������ס
	list._mtx.unlock();

	// û�п���Span��,����PageCacheҪ
	PageCache::GetInstance()->_pageMtx.lock();
	Span* span = PageCache::GetInstance()->NewSpan(SizeClass::NumMovePage(size));
	PageCache::GetInstance()->_pageMtx.unlock();

	// �������ڴ�span����ʼ��ַ�ʹ�С(�ֽ���)
	char* start = (char*)(span->_pageId << PAGE_SHIFT);
	size_t bytes = span->_n << PAGE_SHIFT;
	char* end = start + bytes;
	// ������ڴ��гɿ�����������������
	// ����һ������
	span->_freeList = start;
	start += size;
	void* tail = span->_freeList;
	while (start < end)
	{
		NextObj(tail) = start;
		tail = start;
		start += size;
	}

	// �к��Ժ���Ҫ��span�ҵ�Ͱ��ʱ����Ҫ����
	list._mtx.lock();
	list.PushFront(span);
	return span;
}

size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size)
{
	size_t index = SizeClass::Index(size);
	_spanLists[index]._mtx.lock();	// ����
	       
	Span* span = GetOneSpan(_spanLists[index], size);
	assert(span);          
	assert(span->_freeList);
	// ��span�л�ȡbatchNum������
	// �������batchNum�����ж��پ��ö���
	start = span->_freeList;
	end = start;
	size_t i = 0;
	size_t actualNum = 1;	// ʵ�ʻ�ȡ��������
	while (i < batchNum   - 1 && NextObj(end) != nullptr)
	{
		end = NextObj(end);
		i++;
		actualNum++;
	}
	span->_freeList = NextObj(end);
	NextObj(end) = nullptr;

	_spanLists[index]._mtx.unlock();	// ����

	return actualNum;
}
