#pragma once

#include<iostream>
#include<cassert>
#include<thread>
#include<mutex>

using std::endl;
using std::cout;

static const size_t MAX_BYTES = 256 * 1024; // ����thread cache���������ֽ���
static const size_t NFREE_LISTS = 208; // �������������

// ʹ������������ȷ��ҳ�ŵ�����
#ifdef _WIN64
typedef size_t PAGE_ID;
#elif _WIN32
typedef unsigned long long PAGE_ID;
#else
	// Linuxƽ̨��
#endif


// ���ص�ǰ�ڵ����һ���ڵ㣬���÷���
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
	// ͷ��
	void Push(void* obj)
	{
		assert(obj); // ����Ķ�����Ϊ��
		NextObj(obj) = _freeList;
		_freeList = obj;
	}

	// ����һ����Χ��ֱ��β��
	void PushRange(void* start, void* end)
	{
		NextObj(end) = _freeList;
		_freeList = start;
	}
	// ͷɾ
	void* Pop()
	{
		assert(_freeList);
		void* obj = _freeList;
		_freeList = NextObj(_freeList);
		return obj;
	}
	// �п�
	bool Empty()
	{
		return _freeList == nullptr;
	}

	// �����÷���maxSize
	size_t& MaxSize()
	{
		return _maxSize;
	}
};

class SizeClass
{
	// ������������10%���ҵ�����Ƭ�˷�
	// [1,128]					8byte����			freelist[0,16)
	// [128+1,1024]				16byte����			freelist[16,72)
	// [1024+1,8*1024]			128byte����			freelist[72,128)
	// [8*1024+1,64*1024]		1024byte����			freelist[128,184)
	// [64*1024+1,256*1024]		8*1024byte����		freelist[184,208)
public:
	// ����д��
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
	// ��Чд��
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

	// ����Ͱ������������д��
	/*static inline size_t _Index(size_t bytes, size_t align_num)
	{
		if (bytes % align_num == 0)
			return bytes / align_num - 1;
		else
			return bytes / align_num;
	}*/
	// ����Ͱ�������� ����д��
	static inline size_t _Index(size_t bytes, size_t align_shift)
	{
		return ((bytes + (1 << align_shift) - 1) >> align_shift) - 1;
	}

	// ����ӳ�����һ����������Ͱ
	static inline size_t Index(size_t bytes)
	{
		assert(bytes <= MAX_BYTES);
		// ÿ�������ж��ٸ���
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

	// һ�δ����Ļ����ȡ���ٸ�
	static size_t NumMoveSize(size_t size)
	{
		assert(size > 0);
		// [2, 512]��һ�������ƶ����ٸ������(������)����ֵ
		// С����һ���������޸�
		// С����һ���������޵�
		int num = MAX_BYTES / size;
		if (num < 2)
			num = 2;
		if (num > 512)
			num = 512;
		return num;
	}
};

// Spang����һ����ȵĴ���ڴ�
struct Span
{
	PAGE_ID _pageId;	// ����ڴ����ʼҳ��ҳ��
	size_t _n;			// ҳ������
	Span* _next;		// ˫������Ľṹ
	Span* _prev;
	size_t _useCount;	// �кõ�С���ڴ�ĸ���
	void* _freeList;	// �кõ�С���ڴ�Ŀ�������
};

// ��ͷ˫��ѭ������
class SpanList
{
private:
	Span* _head;		// ͷ�ڵ�
public:
	std::mutex _mtx;	// ��������Ͱ��
public:
	// ���캯��
	SpanList()
	{
		_head = new Span;
		_head->_next = nullptr;
		_head->_prev = nullptr;
	}

	// ��posǰ����
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

	// ɾ��pos
	void Erase(Span* pos)
	{
		assert(pos);
		assert(pos != _head);
		Span* next = pos->_next;
		Span* prev = pos->_prev;
		prev->_next = next;
		next->_prev = prev;
		// ����Ҫdelete��Span��ֻ��Ҫ�������
	}
};