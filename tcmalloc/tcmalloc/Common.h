#pragma once

#include<iostream>
#include<cassert>
#include<thread>

using std::endl;
using std::cout;

static const size_t MAX_BYTES = 256 * 1024; // 定义thread cache最大的申请字节数
static const size_t NFREE_LISTS = 208; // 空闲链表的数量



// 返回当前节点的下一个节点，引用返回
static void*& NextObj(void* obj)
{
	return *(void**)obj;
}

class FreeList
{
private:
	void* _freeList = nullptr;
public:
	// 头插
	void Push(void* obj)
	{
		assert(obj); // 插入的对象不能为空
		NextObj(obj) = _freeList;
		_freeList = obj;
	}
	// 头删
	void* Pop()
	{
		assert(_freeList);
		void* obj = _freeList;
		_freeList = NextObj(_freeList);
		return obj;
	}
	// 判空
	bool Empty()
	{
		return _freeList == nullptr;
	}
};

class SizeClass
{
	// 整体控制在最多10%左右的内碎片浪费
	// [1,128]					8byte对齐			freelist[0,16)
	// [128+1,1024]				16byte对齐			freelist[16,72)
	// [1024+1,8*1024]			128byte对齐			freelist[72,128)
	// [8*1024+1,64*1024]		1024byte对齐			freelist[128,184)
	// [64*1024+1,256*1024]		8*1024byte对齐		freelist[184,208)
public:
	// 常规写法
	/*static inline size_t _RoundUp(size_t bytes, size_t align_num)
	{
		if (bytes % align_num == 0)
		{
			return bytes;
		}
		else
		{
			return (bytes / align_num + 1) * align_num;	
		}
	}*/
	// 高效写法
	static inline size_t _RoundUp(size_t size, size_t align_num)
	{
		return (size + align_num - 1) & (~(align_num - 1));
	}

	static inline size_t RoundUp(size_t size)
	{
		if (size <= 128)
		{
			return _RoundUp(size, 8);
		}
		else if (size <= 1024)
		{
			return _RoundUp(size, 16);
		}
		else if (size <= 8 * 1024)
		{
			return _RoundUp(size, 128);
		}
		else if (size <= 64 * 1024)
		{
			return _RoundUp(size, 1024);
		}
		else if (size <= 256 * 1024)
		{
			return _RoundUp(size, 8 * 1024);
		}
		else
		{
			assert(-1);
			return -1;
		}
		
	}

	// 查找桶的索引，常规写法
	/*static inline size_t _Index(size_t bytes, size_t align_num)
	{
		if (bytes % align_num == 0)
			return bytes / align_num - 1;
		else
			return bytes / align_num;
	}*/
	// 查找桶的索引， 技巧写法
	static inline size_t _Index(size_t bytes, size_t align_shift)
	{
		return ((bytes + (1 << align_shift) - 1) >> align_shift) - 1;
	}

	// 计算映射的哪一个自由链表桶
	static inline size_t Index(size_t bytes)
	{
		assert(bytes <= MAX_BYTES);
		// 每个区间有多少个链
		static int group_array[4] = { 16, 56, 56, 56 };
		if (bytes <= 128) {
			return _Index(bytes, 3);
		}
		else if (bytes <= 1024) {
			return _Index(bytes - 128, 4) + group_array[0];
		}
		else if (bytes <= 8 * 1024) {
			return _Index(bytes - 1024, 7) + group_array[1] + group_array[0];
		}
		else if (bytes <= 64 * 1024) {
			return _Index(bytes - 8 * 1024, 10) + group_array[2] + group_array[1]
				+ group_array[0];
		}
		else if (bytes <= 256 * 1024) {
			return _Index(bytes - 64 * 1024, 13) + group_array[3] +
				group_array[2] + group_array[1] + group_array[0];
		}
		else {
			assert(false);
		}
		return -1;
	}
};