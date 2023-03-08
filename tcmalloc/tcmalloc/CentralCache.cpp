#include"CentralCache.h"
#include"PageCache.h"


// ��cpp�ļ��ж���
CentralCache CentralCache::_sInst;

// ��ȡһ���ǿյ�Span
Span* CentralCache::GetOneSpan(SpanList& list, size_t size)
{
	// �鿴��ǰ��spanlist�Ƿ���ڷǿյ�span
	Span* it = list.Begin();
	while (it != list.End())
	{
		// �����ǰspan�ж���ֱ�ӷ���
		if (it->_freelist)
		{
			return it;
		}
		it = it->_next;
	}

	//�Ȱ�central cache��Ͱ�������������������̻߳��ͷ��ڴ棬��������
	list._mtx.unlock();

	// û�зǿյ�span����ȥ��page cacheҪ

	// ����pagecache����
	PageCache::GetInstance()->_pageMtx.lock();

	Span* span = PageCache::GetInstance()->NewSpan(SizeClass::NumMovePage(size));
	span->_isUse = true;	// ֻҪ�Ǵ�pagecache�зֹ�����span���ʹ����Ѿ���ʹ����
	span->_objSize = size;	// ����С�����size�����ں����ͷſռ�
	// ����
	PageCache::GetInstance()->_pageMtx.unlock();


	// ��span�����зֲ���Ҫ�����������ھ�������
	// ����span����ʼ��ַ
	char* start = (char*)(span->_pageId << PAGE_SHIFT);
	// ����span�Ĵ���ڴ���ֽ���
	size_t bytes = span->_n << PAGE_SHIFT;
	// ��β��ַ
	char* end = start + bytes;

	// ������ڴ���Ϊ����������������
	// 1. ����һ��������ͷ������β��
	span->_freelist = start;
	start += size;
	void* tail = span->_freelist;
	// β��
	while (start < end)
	{
		// ǰ�ĸ��ֽڵĵ�ַ�洢��һ����ĵ�ַ���γ����ӹ�ϵ
		NextObj(tail) = start;
		tail = start;
		start += size;
	}
	// ���һ��Ҫָ���
	NextObj(tail) = nullptr;

	// ����Ҫ�к�span�Ժ���Ҫ���зֺõ�span�ӵ�spanlist�У���Ҫ����
	list._mtx.lock();

	list.PushFront(span);

	return span;
}

// ��central cache�л�ȡһ��������thread cache
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t batch_num, size_t size)
{
	// �����Ӧ���ĸ�SpanList����
	size_t index = SizeClass::Index(size);
	// ����
	_spanLists[index]._mtx.lock();
	// ��ȡһ���ǿյ�span
	Span* span = GetOneSpan(_spanLists[index], size);
	assert(span);
	assert(span->_freelist);

	// ��span�л�ȡbatch_num���������û��batch_num�����ж����ö���	
	start = span->_freelist;
	end = start;
	size_t actual_num = 1;
	size_t i = 0;
	while (i < batch_num - 1 && NextObj(end))
	{
		end = NextObj(end);
		i++;
		actual_num++;
	}
	span->_freelist = NextObj(end);
	NextObj(end) = nullptr;
	span->_useCount += actual_num;

	// ����
	_spanLists[index]._mtx.unlock();
	return actual_num;
}


// 
void CentralCache::RealaseListToSpans(void* start, size_t size)
{
	size_t index = SizeClass::Index(size);
	// Ͱ���ȼ���
	_spanLists[index]._mtx.lock();
	// ����list
	while (start)
	{
		void* next = NextObj(start);

		Span* span = PageCache::GetInstance()->MapObjectToSpan(start);
		NextObj(start) = span->_freelist;
		span->_freelist = start;
		span->_useCount--;

		if (span->_useCount == 0)
		{
			// ˵��span�зֳ�ȥ������С���ڴ涼�����ˣ��������span���ڴ�����ٻ��ո�pagecache��pagecache������ȥ��ǰ��ҳ�ĺϲ�
			_spanLists[index].Erase(span);
			span->_freelist = nullptr;
			span->_next = nullptr;
			span->_prev = nullptr;

			// ��span��ҳ�黹��page cache
			// 
			// �����Ƚ�central cache��Ͱ�����ˣ���Ϊ�����ǲ���pagecache���������central cache
			_spanLists[index]._mtx.unlock();

			// ����pagecache�Ĵ���
			PageCache::GetInstance()->_pageMtx.lock();
			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
			PageCache::GetInstance()->_pageMtx.unlock();

			_spanLists[index]._mtx.lock();
		}
		start = next;
	}
	_spanLists[index]._mtx.unlock();
}