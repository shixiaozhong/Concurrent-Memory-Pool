#pragma once

#include<iostream>
#include<cassert>
#include<thread>
#include<mutex>

using std::endl;
using std::cout;

static const size_t MAX_BYTES = 256 * 1024; // 定义thread cache最大的申请字节数
static const size_t NFREE_LISTS = 208; // 空闲链表的数量

// 使用条件编译来确定页号的类型
#ifdef _WIN64
typedef size_t PAGE_ID;
#elif _WIN32
typedef unsigned long long PAGE_ID;
#else
	// Linux平台下
#endif


// 返回当前节点的下一个节点，引用返回
static void*& NextObj(void* obj)
{
	return *(void**)obj;
}

class FreeList
{
private:
	void* _freeList = nullptr;
	size_t _maxSize = 1;
public:
	// 头插
	void Push(void* obj)
	{
		assert(obj); // 插入的对象不能为空
		NextObj(obj) = _freeList;
		_freeList = obj;
	}

	// 插入一个范围，直接尾插
	void PushRange(void* start, void* end)
	{
		NextObj(end) = _freeList;
		_freeList = start;
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

	// 传引用返回maxSize
	size_t& MaxSize()
	{
		return _maxSize;
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

	// 一次从中心缓存获取多少个
	static size_t NumMoveSize(size_t size)
	{
		assert(size > 0);
		// [2, 512]，一次批量移动多少个对象的(慢启动)上限值
		// 小对象一次批量上限高
		// 小对象一次批量上限低
		int num = MAX_BYTES / size;
		if (num < 2)
			num = 2;
		if (num > 512)
			num = 512;
		return num;
	}
};

// Spang管理一个跨度的大块内存
struct Span
{
	PAGE_ID _pageId;	// 大块内存的起始页的页号
	size_t _n;			// 页的数量
	Span* _next;		// 双向链表的结构
	Span* _prev;
	size_t _useCount;	// 切好的小块内存的个数
	void* _freeList;	// 切好的小块内存的空闲链表
};

// 带头双向循环链表
class SpanList
{
private:
	Span* _head;		// 头节点
public:
	std::mutex _mtx;	// 互斥锁，桶锁
public:
	// 构造函数
	SpanList()
	{
		_head = new Span;
		_head->_next = nullptr;
		_head->_prev = nullptr;
	}

	// 在pos前插入
	void Insert(Span* pos, Span* newSpan)
	{
		assert(pos);
		assert(newSpan);
		Span* prev = pos->_prev;
		newSpan->_next = pos;
		newSpan->_prev = prev;
		prev->_next = newSpan;
		pos->_prev = newSpan;
	}

	// 删除pos
	void Erase(Span* pos)
	{
		assert(pos);
		assert(pos != _head);
		Span* next = pos->_next;
		Span* prev = pos->_prev;
		prev->_next = next;
		next->_prev = prev;
		// 不需要delete掉Span，只需要解除即可
	}
};