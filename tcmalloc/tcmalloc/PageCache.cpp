#include"PageCache.h"

PageCache PageCache::_sInst;


Span* PageCache::NewSpan(size_t k)
{
	assert(k > 0 && k < NPAGES);

	// 先检查第k个桶里有没有span
	if (_spanLists[k].Empty())
	{
		return _spanLists[k].PopFront();
	}
	// 检查后面的桶里有没有span
	for (size_t i = k + 1; i < NPAGES; i++)
	{
		if (!_spanLists[i].Empty())
		{
			Span* nSpan = _spanLists[i].PopFront();
			Span* kSpan = new Span;
			// 在nSpan的头部切一个k页下来
			kSpan->_pageId = nSpan->_pageId;
			kSpan->_n = k;
			nSpan->_pageId += k;
			nSpan->_n -= k;

			_spanLists[nSpan->_n].PushFront(nSpan);
			return kSpan;
		}
	}

	// 后面都没有了，这时就去找堆要一个128页的
	Span* bigSpan = new Span;
	void* ptr = SystemAlloc(NPAGES - 1);
	bigSpan->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
	bigSpan->_n = NPAGES - 1;
	_spanLists[bigSpan->_n].PushFront(bigSpan);

	return NewSpan(k);
}
