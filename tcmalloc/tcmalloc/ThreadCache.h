#pragma once

#include"Common.h"

class ThreadCache
{
private:
	FreeList _freelists[NFREELISTS];	// 一条长链表
public:
	// 申请内存
	void* Allocate(size_t size);
	// 释放内存
	void Deallocate(void* ptr, size_t size);
	// 链表过长，回收
	void ListTooLong(FreeList& list, size_t size);
	// 向CentralCache申请
	void* FetchFromCentralCache(size_t index, size_t size);
};

// thread local stroage 线程局部存储
static _declspec (thread) ThreadCache* pTLSThreadCache = nullptr;