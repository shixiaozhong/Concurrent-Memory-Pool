#include"PageCache.h"

PageCache PageCache::_sInst;

// ��ȡһ��kҳ��span
Span* PageCache::NewSpan(size_t k)
{
	assert(k > 0 && k < NPAGES);

	// �ȼ���kҳ��spanList����û��span
	if (!_spanLists[k].Empty())
	{
		// ����ֱ�ӷ���
		return _spanLists[k].PopFront();
	}
	// �������Ͱ��û��span������п��Խ����з֣�Ȼ��ӵ���ͬ��spanlist��
	for (size_t i = k + 1; i < NPAGES; i++)
	{
		// �ҵ���һ����Ϊ�յ�Ͱ�Ϳ��Կ�ʼ�з֣��з�Ϊһ��kҳ��span��һ��i-kҳ��span��Ȼ����ʣ������span�ҵ���Ӧ��spanlist��
		if (!_spanLists[i].Empty())
		{
			// ����һ��span
			Span* nSpan = _spanLists[i].PopFront();
			Span* kSpan = new Span;

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
	Span* bigSpan = new Span;
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
		delete prev_span;
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
		delete next_span;
	}

	// �ϲ���Ϻ󣬽�span�ҵ���Ӧ��ҳ��
	_spanLists[span->_n].PushFront(span);
	span->_isUse = false;	// ���Ϊδʹ��
	_idSpanMap[span->_pageId] = span;
	_idSpanMap[span->_pageId + span->_n - 1] = span;

}