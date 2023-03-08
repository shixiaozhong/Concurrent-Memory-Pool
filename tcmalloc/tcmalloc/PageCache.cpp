#include"PageCache.h"


PageCache PageCache::_sInst;

// 获取一个k页的span
Span* PageCache::NewSpan(size_t k)
{
	assert(k > 0);

	if (k > NPAGES - 1)
	{
		// 大于128页就向堆申请 
		void* ptr = SystemAlloc(k);
		//Span* span = new Span;
		Span* span = _spanPool.New();	// 使用定长内存池来替换掉new
		span->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
		span->_n = k;
		_idSpanMap[span->_pageId] = span;	// 将起始页号和span关联起来
		return span;
	}

	// 先检查第k页的spanList中有没有span
	if (!_spanLists[k].Empty())
	{
		// 存在直接返回
		Span* kSpan =  _spanLists[k].PopFront();
		// 建立id与span的映射，方便central cache回收小块内存时，查找对应的span
		for (PAGE_ID i = 0; i < kSpan->_n; i++)
		{
			_idSpanMap[kSpan->_pageId + i] = kSpan;
		}
		return kSpan;
	}
	// 检查后面的桶有没有span，如果有可以进行切分，然后接到不同的spanlist下
	for (size_t i = k + 1; i < NPAGES; i++)
	{
		// 找到第一个不为空的桶就可以开始切分，切分为一个k页的span和一个i-k页的span，然后将切剩下来的span挂到对应的spanlist下
		if (!_spanLists[i].Empty())
		{
			// 弹出一个span
			Span* nSpan = _spanLists[i].PopFront();
			
			//Span* kSpan = new Span;
			Span* kSpan = _spanPool.New();

			// 在nspan的头部切下一个kspan
			kSpan->_pageId = nSpan->_pageId;
			kSpan->_n = k;

			// 修改页号即可完成切分
			nSpan->_pageId += k;
			nSpan->_n -= k;

			// 将切剩下来的span挂到对应的spanlist下面
			_spanLists[nSpan->_n].PushFront(nSpan);

			// 存储nSpan的首尾页号跟nSpan的映射，方便回收内存时，进行合并查找
			_idSpanMap[nSpan->_pageId] = nSpan;
			_idSpanMap[nSpan->_pageId + nSpan->_n - 1] = nSpan;

			// 建立id与span的映射，方便central cache回收小块内存时，查找对应的span
			for (PAGE_ID i = 0; i < kSpan->_n; i++)
			{
				_idSpanMap[kSpan->_pageId + i] = kSpan;
			}

			return kSpan;
		}
	}

	// 后面所有的spanlist都没有span，需要向堆申请空间，要一个128页的span
	//Span* bigSpan = new Span;
	Span* bigSpan = _spanPool.New();

	// 向堆中申请空间，直接申请一个128页的span
	void* ptr = SystemAlloc(NPAGES - 1);
	bigSpan->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
	bigSpan->_n = NPAGES - 1;
	// 插入到对应的spanlist中
	_spanLists[bigSpan->_n].PushFront(bigSpan);

	// 递归调用自己
	return NewSpan(k);
}


Span* PageCache::MapObjectToSpan(void* obj)
{
	PAGE_ID id = (PAGE_ID)obj >> PAGE_SHIFT;
	std::unique_lock<std::mutex> lock(_pageMtx);	// 使用RAII的锁，出作用域自动解锁

	auto ret = _idSpanMap.find(id);
	if (ret != _idSpanMap.end())
	{
		return ret->second;
	}
	else
	{
		// 理论上来说，一定可以找的到，如果没找到肯定是程序有错误
		assert(false);
		return nullptr;
	}
}

void PageCache::ReleaseSpanToPageCache(Span* span)
{
	if (span->_n > NPAGES - 1)
	{
		// span的页数量大于128,不是找pagecache要的
		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		SystemFree(ptr);
		//delete span;
		_spanPool.Delete(span);
		return;
	}


	// 对span的前后页尝试进行合并，缓解内存碎片问题
	// 向前合并
	while (true)
	{
		PAGE_ID prev_id = span->_pageId - 1;
		auto ret = _idSpanMap.find(prev_id);
		// 不存在前面的页号
		if (ret == _idSpanMap.end())
		{
			break;
		}
		// 存在但是已经被使用了
		Span* prev_span = ret->second;
		if (prev_span->_isUse == true)
		{
			break;
		}
		// 合并的页超过128也不好管理
		if (prev_span->_n + span->_n > NPAGES - 1)
		{
			break;
		}
		span->_pageId = prev_span->_pageId;
		span->_n += prev_span->_n;
		_spanLists[prev_span->_n].Erase(prev_span);
		// 释放掉prev_span
		//delete prev_span;
		_spanPool.Delete(prev_span);
	}

	// 向后合并
	while (true)
	{
		// 找到后面span的起始页号
		PAGE_ID next_id = span->_pageId + span->_n;

		auto ret = _idSpanMap.find(next_id);
		if (ret == _idSpanMap.end())
		{
			break;
		}

		// 存在但是在使用
		Span* next_span = ret->second;
		if (next_span->_isUse == true)
		{
			break;
		}

		// 合并过后的页大于128页
		if (span->_n + next_span->_n > NPAGES - 1)
		{
			break;
		}

		span->_n += next_span->_n;
		_spanLists[next_span->_n].Erase(next_span);
		//delete next_span;
		_spanPool.Delete(next_span);
	}

	// 合并完毕后，将span挂到对应的页中
	_spanLists[span->_n].PushFront(span);
	span->_isUse = false;	// 标记为未使用
	_idSpanMap[span->_pageId] = span;
	_idSpanMap[span->_pageId + span->_n - 1] = span;

}