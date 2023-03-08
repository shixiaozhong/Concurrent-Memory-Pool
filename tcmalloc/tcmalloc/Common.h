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
// Linux��
#endif // _WIN32


static const size_t MAX_BYTES = 256 * 1024;		// �����������ֽ���
static const size_t NFREELISTS = 208;			// ������
static const size_t NPAGES = 129;				// page cache�����ҳ��
static const size_t PAGE_SHIFT = 13;			// �ֽ���ת��Ϊҳ����һҳΪ8KB


//����ҳ�ű��������ͣ�64λ��32λ�´�С���ϴ�
#ifdef _WIN64
typedef long long PAGE_ID;
#elif _WIN32
typedef size_t PAGE_ID;
#else
	// Linux��
#endif // _WIN64

// ֱ��ȥ���ϰ�ҳ�����ڴ�ռ�
inline static void* SystemAlloc(size_t kpage)
{
#ifdef _WIN32
	void* ptr = VirtualAlloc(0, kpage << 13, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#elif
	// Linux��brk/mmap��
#endif // _WIN32

	// ����ʧ��
	if (ptr == nullptr)
		throw std::bad_alloc();
	return ptr;
}

// �ͷŶ����ڴ�
inline static void SystemFree(void* ptr)
{
#ifdef _WIN32
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
	// sbrk, ummap��
#endif
}

// ��ȡǰ4������8���ֽڵ�ֵ��Ϊnextָ�룬���÷���
static void*& NextObj(void* obj)
{
	return *(void**)obj;
}

// �����зֺõ�С����
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
		// ͷ��
		// ȡǰ8���ֽ���Ϊnextָ��
		*(void**)obj = _freeList;
		_freeList = obj;

		_size++;
	}
	void* Pop()
	{
		assert(_freeList);
		// ͷɾ
		void* obj = _freeList;
		_freeList = NextObj(_freeList);
		_size--;
		return obj;
	}

	// һ�²�������
	void PushRange(void* start, void* end, size_t n)
	{
		NextObj(end) = _freeList;
		_freeList = start;
		_size += n;
	}

	// һ��ɾ������
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


// ����������ҳ����ڴ�Ŀ�Ƚṹ
struct Span
{
	PAGE_ID _pageId = 0;	// ����ڴ����ʼҳ��
	size_t _n = 0;			// ҳ������

	Span* _prev = nullptr;
	Span* _next = nullptr;

	size_t _useCount = 0;	//�кõ�С���ڴ棬�������ThreadCache�ļ���
	void* _freelist = nullptr;	// �кõ�С���ڴ����������
	bool _isUse = false;	// �Ƿ��ڱ�ʹ��

	size_t _objSize = 0;	//�кõ�С����Ĵ�С
};

// ˫���ͷѭ������
class SpanList
{
private:
	Span* _head;
public:
	std::mutex _mtx;	// CentralCache�·���ͬһ��SpanList��Ҫ����,���ڲ�������
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

	// ��pos��ǰ�����һ���µĽڵ�
	void Insert(Span* pos, Span* new_span)
	{
		assert(pos);
		assert(new_span);
		// �ҵ�pos��ǰһ���ڵ�
		Span* prev = pos->_prev;
		new_span->_next = pos;
		new_span->_prev = prev;
		prev->_next = new_span;
		pos->_prev = new_span;
	}
	// ͷ��
	void PushFront(Span* span)
	{
		Insert(Begin(), span);
	}

	//ͷɾ
	Span* PopFront()
	{
		Span* front = _head->_next;
		Erase(front);
		return front;
	}

	// ɾ��posλ�õ�Span
	void Erase(Span* pos)
	{
		assert(pos);
		// ������ɾ���ڱ�λ��ͷ�ڵ�
		assert(_head);

		Span* prev = pos->_prev;
		Span* next = pos->_next;
		prev->_next = next;
		next->_prev = prev;
		// ֻ��Ҫ������ӹ�ϵ������Ҫ�ͷ�
	}

	// �п�
	bool Empty()
	{
		return _head->_next == _head;
	}
};


class SizeClass
{
private:

public:
	// ���������10%���ҵ��ڲ���Ƭ�˷�
	// [1,128]					8byte����			freelist[0,16)
	// [128+1,1024]				16byte����			freelist[16,72)
	// [1024+1,8*1024]			128byte����			freelist[72,128)
	// [8*1024+1,64*1024]		1024byte����			freelist[128,184)
	// [64*1024+1,256*1024]		8*1024byte����		freelist[184,208)


	// ����д��
	//size_t _RoundUp(size_t size, size_t align_num)
	//{
	//	size_t align_size = 0;
	//	if (size % align_num != 0)
	//	{
	//		// ����Ϊ�������ı���
	//		align_size = (size / align_num + 1) * align_num;
	//	}
	//	else
	//	{
	//		//����0������Ѿ����˶������ı���
	//		align_size = size;
	//	}
	//	return align_size;
	//}

	// ����д��
	static inline size_t _RoundUp(size_t size, size_t align_num)
	{
		return ((size + align_num - 1) & ~(align_num - 1));
	}

	static inline size_t RoundUp(size_t size)
	{
		if (size <= 128)
		{
			// ����8�ֽڶ���
			return _RoundUp(size, 8);
		}
		else if (size <= 1024)
		{
			// ����16�ֽڶ���
			return _RoundUp(size, 16);
		}
		else if (size <= 8 * 1024)
		{
			// ����128�ֽڶ���
			return _RoundUp(size, 128);
		}
		else if (size <= 64 * 1024)
		{
			// ����1024�ֽڶ���
			return _RoundUp(size, 1024);
		}
		else if (size <= 256 * 1024)
		{
			// ����8 * 1024�ֽڶ���
			return _RoundUp(size, 8 * 1024);
		}
		else
		{
			// ����256KB
			return _RoundUp(size, 1 << PAGE_SHIFT);
		}
	}

	// ����д��
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

	// ����д��
	// ��ȡ���ĸ�Ͱȡ����
	static inline size_t _Index(size_t size, size_t align_shift)
	{
		return ((size + (1 << align_shift) - 1) >> align_shift) - 1;
	}

	// ����ӳ�����һ����������Ͱ
	static inline size_t Index(size_t bytes)
	{
		assert(bytes <= MAX_BYTES);
		// ÿ�������ж��ٸ���
		static int group_array[4] = { 16, 56, 56, 56 };
		if (bytes <= 128) {
			// ����8�ֽڶ���
			return _Index(bytes, 3);
		}
		else if (bytes <= 1024) {
			// ����16�ֽڶ���
			return _Index(bytes - 128, 4) + group_array[0];
		}
		else if (bytes <= 8 * 1024) {
			// ����128�ֽڶ���
			return _Index(bytes - 1024, 7) + group_array[1] + group_array[0];
		}
		else if (bytes <= 64 * 1024) {
			// ����1024����
			return _Index(bytes - 8 * 1024, 10) + group_array[2] + group_array[1] + group_array[0];
		}
		else if (bytes <= 256 * 1024) {
			// ����8 * 1024����
			return _Index(bytes - 64 * 1024, 13) + group_array[3] + group_array[2] + group_array[1] + group_array[0];
		}
		else {
			assert(false);
		}
		return -1;
	}


	// thread cache��central cache������ĸ���
	static size_t NumMoveSize(size_t size)
	{
		assert(size > 0);
		// ÿ����ȡ[2, 512]��С�������㣬��������һ��
		size_t num = MAX_BYTES / size;
		if (num < 2)
			num = 2;
		if (num > 512)
			num = 512;
		return num;
	}


	// ����һ����ϵͳ��ȡ��ҳ
	static size_t NumMovePage(size_t size)
	{
		size_t num = NumMoveSize(size);
		size_t npage = num * size;
		npage >>= PAGE_SHIFT;
		// ���ٸ�һҳ
		if (npage == 0)
			npage = 1;
		return npage;
	}
};