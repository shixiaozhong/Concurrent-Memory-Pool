#include"ThreadCache.h"
#include"CentralCache.h"


// ��CentralCache����
void* ThreadCache::FetchFromCentralCache(size_t index, size_t size)
{
	// ����ʼ���������㷨
	// �ʼ������central cacheҪ̫�࣬����Ҫ�Ĵ����������ӣ�ֱ������
	size_t batch_num = min(_freelists[index].MaxSize(), SizeClass::NumMoveSize(size));
	if (_freelists[index].MaxSize() == batch_num)
	{
		_freelists[index].MaxSize() += 1;
	}
	void* start = nullptr;
	void* end = nullptr;
	// ʵ�ʴ�central cache��ȡ���ĸ���
	size_t actual_num = CentralCache::GetInstance()->FetchRangeObj(start, end, batch_num, size);
	// ���ٸ�һ��
	assert(actual_num > 0);

	// �����ֻ��һ��
	if (actual_num == 1)
	{
		assert(start == end);
		// ֱ�ӽ��������
		return start;
	}
	else
	{
		// �������ͽ����˵�һ��������ȫ�����뵽����������
		_freelists[index].PushRange(NextObj(start), end, actual_num - 1);
		return start;
	}
	return nullptr;
}


// ����
void* ThreadCache::Allocate(size_t size)
{
	assert(size <= MAX_BYTES);
	// �������
	size_t align_size = SizeClass::RoundUp(size);
	// Ѱ�Ҷ�Ӧ��Ͱ
	size_t index = SizeClass::Index(size);
	if (!_freelists[index].Empty())
	{
		return _freelists[index].Pop();
	}
	else
	{
		// ��Ӧ����������û�����ݣ�����CentralCache����
		return FetchFromCentralCache(index, align_size);
	}
}

// �ͷ�
void ThreadCache::Deallocate(void* ptr, size_t size)
{
	assert(ptr);
	assert(size <= MAX_BYTES);
	// ���ͷŵĶ���ͷ��ض�Ӧ����������
	size_t index = SizeClass::Index(size);
	_freelists[index].Push(ptr);

	// ������ĳ��ȴ���һ����������ĳ���ʱ���Ϳ�ʼ��һ�ε�ַ��central cache
	if (_freelists[index].Size() >= _freelists[index].MaxSize())
	{
		ListTooLong(_freelists[index], size);
	}

}

void ThreadCache::ListTooLong(FreeList& list, size_t size)
{
	//������Ŀ���ȡ���� 
	void* start = nullptr;
	void* end = nullptr;
	list.PopRange(start, end, list.MaxSize());

	// ����Щ�ڴ滹��central cache��Ӧ��span
	CentralCache::GetInstance()->RealaseListToSpans(start, size);
}