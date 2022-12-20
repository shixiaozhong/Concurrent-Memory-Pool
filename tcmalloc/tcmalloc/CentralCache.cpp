#include"CentralCache.h"

CentralCache CentralCache::_sInst;	

// ��ȡһ���ǿյ�Span
Span* CentralCache::GetOneSpan(SpanList& list, size_t byte_size)
{
	//TODO
	return nullptr;
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
