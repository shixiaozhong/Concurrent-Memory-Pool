#include"PageCache.h"


PageCache PageCache::_sInst;

// ��ȡһ��kҳ��span
Span* PageCache::NewSpan(size_t k)
{
	assert(k > 0);

	if (k > NPAGES - 1)
	{
		// ����128ҳ��������� 
		void* ptr = SystemAlloc(k);
		//Span* span = new Span;
		Span* span = _spanPool.New();	// ʹ�ö����ڴ�����滻��new
		span->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
		span->_n = k;
		_idSpanMap[span->_pageId] = span;	// ����ʼҳ�ź�span��������
		return span;
	}

	// �ȼ���kҳ��spanList����û��span
	if (!_spanLists[k].Empty())
	{
		// ����ֱ�ӷ���
		Span* kSpan =  _spanLists[k].PopFront();
		// ����id��span��ӳ�䣬����central cache����С���ڴ�ʱ�����Ҷ�Ӧ��span
		for (PAGE_ID i = 0; i < kSpan->_n; i++)
		{
			_idSpanMap[kSpan->_pageId + i] = kSpan;
		}
		return kSpan;
	}
	// �������Ͱ��û��span������п��Խ����з֣�Ȼ��ӵ���ͬ��spanlist��
	for (size_t i = k + 1; i < NPAGES; i++)
	{
		// �ҵ���һ����Ϊ�յ�Ͱ�Ϳ��Կ�ʼ�з֣��з�Ϊһ��kҳ��span��һ��i-kҳ��span��Ȼ����ʣ������span�ҵ���Ӧ��spanlist��
		if (!_spanLists[i].Empty())
		{
			// ����һ��span
			Span* nSpan = _spanLists[i].PopFront();
			
			//Span* kSpan = new Span;
			Span* kSpan = _spanPool.New();

			// ��nspan��ͷ������һ��kspan
			kSpan->_pageId = nSpan->_pageId;
			kSpan->_n = k;

			// �޸�ҳ�ż�������з�
			nSpan->_pageId += k;
			nSpan->_n -= k;

			// ����ʣ������span�ҵ���Ӧ��spanlist����
			_spanLists[nSpan->_n].PushFront(nSpan);

			// �洢nSpan����βҳ�Ÿ�nSpan��ӳ�䣬��������ڴ�ʱ�����кϲ�����
			_idSpanMap[nSpan->_pageId] = nSpan;
			_idSpanMap[nSpan->_pageId + nSpan->_n - 1] = nSpan;

			// ����id��span��ӳ�䣬����central cache����С���ڴ�ʱ�����Ҷ�Ӧ��span
			for (PAGE_ID i = 0; i < kSpan->_n; i++)
			{
				_idSpanMap[kSpan->_pageId + i] = kSpan;
			}

			return kSpan;
		}
	}

	// �������е�spanlist��û��span����Ҫ�������ռ䣬Ҫһ��128ҳ��span
	//Span* bigSpan = new Span;
	Span* bigSpan = _spanPool.New();

	// ���������ռ䣬ֱ������һ��128ҳ��span
	void* ptr = SystemAlloc(NPAGES - 1);
	bigSpan->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
	bigSpan->_n = NPAGES - 1;
	// ���뵽��Ӧ��spanlist��
	_spanLists[bigSpan->_n].PushFront(bigSpan);

	// �ݹ�����Լ�
	return NewSpan(k);
}


Span* PageCache::MapObjectToSpan(void* obj)
{
	PAGE_ID id = (PAGE_ID)obj >> PAGE_SHIFT;
	std::unique_lock<std::mutex> lock(_pageMtx);	// ʹ��RAII���������������Զ�����

	auto ret = _idSpanMap.find(id);
	if (ret != _idSpanMap.end())
	{
		return ret->second;
	}
	else
	{
		// ��������˵��һ�������ҵĵ������û�ҵ��϶��ǳ����д���
		assert(false);
		return nullptr;
	}
}

void PageCache::ReleaseSpanToPageCache(Span* span)
{
	if (span->_n > NPAGES - 1)
	{
		// span��ҳ��������128,������pagecacheҪ��
		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		SystemFree(ptr);
		//delete span;
		_spanPool.Delete(span);
		return;
	}


	// ��span��ǰ��ҳ���Խ��кϲ��������ڴ���Ƭ����
	// ��ǰ�ϲ�
	while (true)
	{
		PAGE_ID prev_id = span->_pageId - 1;
		auto ret = _idSpanMap.find(prev_id);
		// ������ǰ���ҳ��
		if (ret == _idSpanMap.end())
		{
			break;
		}
		// ���ڵ����Ѿ���ʹ����
		Span* prev_span = ret->second;
		if (prev_span->_isUse == true)
		{
			break;
		}
		// �ϲ���ҳ����128Ҳ���ù���
		if (prev_span->_n + span->_n > NPAGES - 1)
		{
			break;
		}
		span->_pageId = prev_span->_pageId;
		span->_n += prev_span->_n;
		_spanLists[prev_span->_n].Erase(prev_span);
		// �ͷŵ�prev_span
		//delete prev_span;
		_spanPool.Delete(prev_span);
	}

	// ���ϲ�
	while (true)
	{
		// �ҵ�����span����ʼҳ��
		PAGE_ID next_id = span->_pageId + span->_n;

		auto ret = _idSpanMap.find(next_id);
		if (ret == _idSpanMap.end())
		{
			break;
		}

		// ���ڵ�����ʹ��
		Span* next_span = ret->second;
		if (next_span->_isUse == true)
		{
			break;
		}

		// �ϲ������ҳ����128ҳ
		if (span->_n + next_span->_n > NPAGES - 1)
		{
			break;
		}

		span->_n += next_span->_n;
		_spanLists[next_span->_n].Erase(next_span);
		//delete next_span;
		_spanPool.Delete(next_span);
	}

	// �ϲ���Ϻ󣬽�span�ҵ���Ӧ��ҳ��
	_spanLists[span->_n].PushFront(span);
	span->_isUse = false;	// ���Ϊδʹ��
	_idSpanMap[span->_pageId] = span;
	_idSpanMap[span->_pageId + span->_n - 1] = span;

}