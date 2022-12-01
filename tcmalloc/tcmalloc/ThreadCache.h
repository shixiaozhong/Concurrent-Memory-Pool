#pragma once
#include"Common.h"


class ThreadCache
{
private:
	FreeList _freeLists[NFREE_LISTS]; // ����������������
public:
	void* Allocate(size_t size);	// �����ڴ�
	void Deallocate(void* ptr, size_t size); // �����ڴ�
	void* FecthFromCentralCache(size_t index, size_t size); // �����Ļ����ȡ
};

// TLS thread local storage
static _declspec(thread) ThreadCache* pTLSThreadCache = nullptr;
static _declspec(thread) int x = 10;