#pragma once

#include<cassert>
#include<iostream>
#include<vector>
#include<unordered_map>
#include<thread>
#include<mutex>


#ifdef _WIN32
#include<windows.h>
#else
// Linux下
#endif // _WIN32


static const size_t MAX_BYTES = 256 * 1024;		// 可申请的最大字节数
static const size_t NFREELISTS = 208;			// 链表长度
static const size_t NPAGES = 129;				// page cache的最大页数
static const size_t PAGE_SHIFT = 13;			// 字节数转换为页数，一页为8KB


//定义页号变量的类型，64位和32位下大小差距较大
#ifdef _WIN64
typedef long long PAGE_ID;
#elif _WIN32
typedef size_t PAGE_ID;
#else
	// Linux下
#endif // _WIN64

// 直接去堆上按页申请内存空间
inline static void* SystemAlloc(size_t kpage)
{
#ifdef _WIN32
	void* ptr = VirtualAlloc(0, kpage << 13, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#elif
	// Linux下brk/mmap等
#endif // _WIN32

	// 申请失败
	if (ptr == nullptr)
		throw std::bad_alloc();
	return ptr;
}

// 释放堆中内存
inline static void SystemFree(void* ptr)
{
#ifdef _WIN32
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
	// sbrk, ummap等
#endif
}

// 获取前4个或者8个字节的值作为next指针，引用返回
static void*& NextObj(void* obj)
{
	return *(void**)obj;
}

// 管理切分好的小对象
class FreeList
{
private:
	void* _freeList = nullptr;
	size_t _maxSize = 1;
	size_t _size = 0;
public:
	void Push(void* obj)
	{
		assert(obj);
		// 头插
		// 取前8个字节作为next指针
		*(void**)obj = _freeList;
		_freeList = obj;

		_size++;
	}
	void* Pop()
	{
		assert(_freeList);
		// 头删
		void* obj = _freeList;
		_freeList = NextObj(_freeList);
		_size--;
		return obj;
	}

	// 一下插入批量
	void PushRange(void* start, void* end, size_t n)
	{
		NextObj(end) = _freeList;
		_freeList = start;
		_size += n;
	}

	// 一下删除批量
	void PopRange(void*& start, void*& end, size_t n)
	{
		assert(n >= _size);
		start = _freeList;
		end = start;
		for (size_t i = 0; i < n - 1; i++)
		{
			end = NextObj(end);
		}
		_freeList = NextObj(end);
		NextObj(end) = nullptr;
		_size -= n;
	}
	bool Empty()
	{
		return _freeList == nullptr;
	}
	size_t& MaxSize()
	{
		return _maxSize;
	}
	size_t& Size()
	{
		return _size;
	}
};


// 管理多个连续页大块内存的跨度结构
struct Span
{
	PAGE_ID _pageId = 0;	// 大块内存的起始页号
	size_t _n = 0;			// 页的数量

	Span* _prev = nullptr;
	Span* _next = nullptr;

	size_t _useCount = 0;	//切好的小块内存，被分配给ThreadCache的计数
	void* _freelist = nullptr;	// 切好的小块内存的自由链表
	bool _isUse = false;	// 是否在被使用

	size_t _objSize = 0;	//切好的小对象的大小
};

// 双向带头循环链表
class SpanList
{
private:
	Span* _head;
public:
	std::mutex _mtx;	// CentralCache下访问同一个SpanList需要加锁,存在并发问题
public:
	SpanList()
	{
		_head = new Span;
		_head->_next = _head;
		_head->_prev = _head;
	}

	Span* Begin()
	{
		return _head->_next;
	}
	Span* End()
	{
		return _head;
	}

	// 在pos的前面插入一个新的节点
	void Insert(Span* pos, Span* new_span)
	{
		assert(pos);
		assert(new_span);
		// 找到pos的前一个节点
		Span* prev = pos->_prev;
		new_span->_next = pos;
		new_span->_prev = prev;
		prev->_next = new_span;
		pos->_prev = new_span;
	}
	// 头插
	void PushFront(Span* span)
	{
		Insert(Begin(), span);
	}

	//头删
	Span* PopFront()
	{
		Span* front = _head->_next;
		Erase(front);
		return front;
	}

	// 删除pos位置的Span
	void Erase(Span* pos)
	{
		assert(pos);
		// 不可以删除哨兵位的头节点
		assert(_head);

		Span* prev = pos->_prev;
		Span* next = pos->_next;
		prev->_next = next;
		next->_prev = prev;
		// 只需要解除链接关系，不需要释放
	}

	// 判空
	bool Empty()
	{
		return _head->_next == _head;
	}
};


class SizeClass
{
private:

public:
	// 整体控制在10%左右的内部碎片浪费
	// [1,128]					8byte对齐			freelist[0,16)
	// [128+1,1024]				16byte对齐			freelist[16,72)
	// [1024+1,8*1024]			128byte对齐			freelist[72,128)
	// [8*1024+1,64*1024]		1024byte对齐			freelist[128,184)
	// [64*1024+1,256*1024]		8*1024byte对齐		freelist[184,208)


	// 常规写法
	//size_t _RoundUp(size_t size, size_t align_num)
	//{
	//	size_t align_size = 0;
	//	if (size % align_num != 0)
	//	{
	//		// 提升为对齐数的倍数
	//		align_size = (size / align_num + 1) * align_num;
	//	}
	//	else
	//	{
	//		//等于0代表就已经是了对齐数的倍数
	//		align_size = size;
	//	}
	//	return align_size;
	//}

	// 技巧写法
	static inline size_t _RoundUp(size_t size, size_t align_num)
	{
		return ((size + align_num - 1) & ~(align_num - 1));
	}

	static inline size_t RoundUp(size_t size)
	{
		if (size <= 128)
		{
			// 按照8字节对齐
			return _RoundUp(size, 8);
		}
		else if (size <= 1024)
		{
			// 按照16字节对齐
			return _RoundUp(size, 16);
		}
		else if (size <= 8 * 1024)
		{
			// 按照128字节对齐
			return _RoundUp(size, 128);
		}
		else if (size <= 64 * 1024)
		{
			// 按照1024字节对齐
			return _RoundUp(size, 1024);
		}
		else if (size <= 256 * 1024)
		{
			// 按照8 * 1024字节对齐
			return _RoundUp(size, 8 * 1024);
		}
		else
		{
			// 超过256KB
			return _RoundUp(size, 1 << PAGE_SHIFT);
		}
	}

	// 常规写法
	/*size_t _Index(size_t size, size_t align_num)
	{
		if (size % align_num == 0)
		{
			return size / align_num - 1;
		}
		else
		{
			return size / align_num;
		}
	}*/

	// 技巧写法
	// 获取在哪个桶取数据
	static inline size_t _Index(size_t size, size_t align_shift)
	{
		return ((size + (1 << align_shift) - 1) >> align_shift) - 1;
	}

	// 计算映射的哪一个自由链表桶
	static inline size_t Index(size_t bytes)
	{
		assert(bytes <= MAX_BYTES);
		// 每个区间有多少个链
		static int group_array[4] = { 16, 56, 56, 56 };
		if (bytes <= 128) {
			// 按照8字节对齐
			return _Index(bytes, 3);
		}
		else if (bytes <= 1024) {
			// 按照16字节对齐
			return _Index(bytes - 128, 4) + group_array[0];
		}
		else if (bytes <= 8 * 1024) {
			// 按照128字节对齐
			return _Index(bytes - 1024, 7) + group_array[1] + group_array[0];
		}
		else if (bytes <= 64 * 1024) {
			// 按照1024对齐
			return _Index(bytes - 8 * 1024, 10) + group_array[2] + group_array[1] + group_array[0];
		}
		else if (bytes <= 256 * 1024) {
			// 按照8 * 1024对齐
			return _Index(bytes - 64 * 1024, 13) + group_array[3] + group_array[2] + group_array[1] + group_array[0];
		}
		else {
			assert(false);
		}
		return -1;
	}


	// thread cache从central cache中申请的个数
	static size_t NumMoveSize(size_t size)
	{
		assert(size > 0);
		// 每次提取[2, 512]，小对象给多点，大对象给少一点
		size_t num = MAX_BYTES / size;
		if (num < 2)
			num = 2;
		if (num > 512)
			num = 512;
		return num;
	}


	// 计算一次向系统获取几页
	static size_t NumMovePage(size_t size)
	{
		size_t num = NumMoveSize(size);
		size_t npage = num * size;
		npage >>= PAGE_SHIFT;
		// 至少给一页
		if (npage == 0)
			npage = 1;
		return npage;
	}
};