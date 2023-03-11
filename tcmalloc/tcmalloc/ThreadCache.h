#pragma once

#include"Common.h"

class ThreadCache
{
private:
	FreeList _freelists[NFREELISTS];	// һ��������
public:
	// �����ڴ�
	void* Allocate(size_t size);
	// �ͷ��ڴ�
	void Deallocate(void* ptr, size_t size);
	// �������������
	void ListTooLong(FreeList& list, size_t size);
	// ��CentralCache����
	void* FetchFromCentralCache(size_t index, size_t size);
};

// thread local stroage �ֲ߳̾��洢
static _declspec (thread) ThreadCache* pTLSThreadCache = nullptr;