#pragma once
#include"Common.h"


class ThreadCache
{
private:
	FreeList _freeLists[NFREE_LISTS]; // 定义空闲链表的数组
public:
	void* Allocate(size_t size);	// 申请内存
	void Deallocate(void* ptr, size_t size); // 回收内存
	void* FecthFromCentralCache(size_t index, size_t size); // 从中心缓存获取
};

// TLS thread local storage
static _declspec(thread) ThreadCache* pTLSThreadCache = nullptr;
static _declspec(thread) int x = 10;