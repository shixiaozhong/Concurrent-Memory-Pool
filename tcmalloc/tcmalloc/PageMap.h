#pragma once
#include"Common.h"

// Single-level array
template <int BITS>
class TCMalloc_PageMap1 {
private:
	static const int LENGTH = 1 << BITS;// ����ӳ���ϵ����Ŀ������32λ�£�ÿҳ8kb�������2^19��ҳ
	void** array_;

public:
	typedef uintptr_t Number; // uintptr_t��unsiged int typedef��

	// ���캯��
	explicit TCMalloc_PageMap1() {
		size_t size = sizeof(void*) << BITS;		// ��Ҫ��������Ĵ�С
		size_t alignSize = SizeClass::_RoundUp(size, 1 << PAGE_SHIFT);	// ��ҳ���룬һҳ��8kb
		array_ = (void**)SystemAlloc(alignSize >> PAGE_SHIFT);	// ��������ڴ�
		memset(array_, 0, sizeof(void*) << BITS);	// �������ʼ��Ϊ0
	}

	// Return the current value for KEY.  Returns NULL if not yet set,
	// or if k is out of range.
	// ��ӳ��ı��в���
	void* get(Number k) const 
	{
		if ((k >> BITS) > 0) // ���ǺϷ�ҳ�ţ�����32ƽ̨�£�ҳ��ֻ��Ҫ19λ���洢������19λ�������0����ʾ���ǺϷ�ҳ��
		{
			return NULL;
		}
		return array_[k];
	}

	// REQUIRES "k" is in range "[0,2^BITS-1]".
	// REQUIRES "k" has been ensured before.
	//
	// Sets the value 'v' for key 'k'.
	// ����ҳ�ź�span������ӳ���ϵ
	void set(Number k, void* v)
	{
		array_[k] = v;
	}
};

// Two-level radix tree
template <int BITS>
class TCMalloc_PageMap2 {
private:
	// Put 32 entries in the root and (2^BITS)/32 entries in each leaf.
	static const int ROOT_BITS = 5;
	static const int ROOT_LENGTH = 1 << ROOT_BITS; // 32 

	static const int LEAF_BITS = BITS - ROOT_BITS;	// 14λ
	static const int LEAF_LENGTH = 1 << LEAF_BITS;	// 2^14

	// root����ÿ����λ�洢��Ԫ������
	struct Leaf {
		void* values[LEAF_LENGTH];
	};

	Leaf* root_[ROOT_LENGTH];             // root�������

public:
	typedef uintptr_t Number;

	// ���캯��
	explicit TCMalloc_PageMap2() {
		memset(root_, 0, sizeof(root_));	// ��root������ΪNULL
		PreallocateMoreMemory();
	}

	void* get(Number k) const 
	{
		const Number i1 = k >> LEAF_BITS;			// ��ȡ��Ӧ��root���Ӧ���±�
		const Number i2 = k & (LEAF_LENGTH - 1);	// ��ȡ��Ӧ�ĵڶ�����±�
		//ҳ�Ų��ڷ�Χ�ڻ���û�н���ӳ���ϵ
		if ((k >> BITS) > 0 || root_[i1] == NULL) 
		{
			return NULL;
		}
		return root_[i1]->values[i2];
	}

	void set(Number k, void* v)
	{
		const Number i1 = k >> LEAF_BITS;			// ��ȡ��Ӧ��root���Ӧ���±�
		const Number i2 = k & (LEAF_LENGTH - 1);	// ��ȡ��Ӧ�ĵڶ�����±�
		assert(i1 < ROOT_LENGTH);	// �����Ƿ���root�Ĵ�С֮��
		root_[i1]->values[i2] = v;	// ����ӳ���ϵ
	}

	bool Ensure(Number start, size_t n) {
		for (Number key = start; key <= start + n - 1;) 
		{
			const Number i1 = key >> LEAF_BITS;	// ��ȡroot���Ӧ���±�

			// ������root��ĳ���
			if (i1 >= ROOT_LENGTH)
				return false;

			// Make 2nd level node if necessary
			// ��һ��i1ָ��Ŀռ�δ����
			if (root_[i1] == NULL) {
				static ObjectPool<Leaf>	leafPool;
				Leaf* leaf = (Leaf*)leafPool.New();	// ʹ�ö����ڴ�ؿ��ٿռ�

				memset(leaf, 0, sizeof(*leaf));		// ��leaf��ΪNULL
				root_[i1] = leaf;					// ��leaf���浽root��
			}

			// Advance key past whatever is covered by this leaf node
			key = ((key >> LEAF_BITS) + 1) << LEAF_BITS;	// ��������飬��Ӧ��λ++
		}
		return true;
	}

	void PreallocateMoreMemory() {
		// Allocate enough to keep track of all possible pages
		Ensure(0, 1 << BITS);
	}
};

// Three-level radix tree
template <int BITS>
class TCMalloc_PageMap3 {
private:
	// How many bits should we consume at each interior level
	static const int INTERIOR_BITS = (BITS + 2) / 3; // Round-up
	static const int INTERIOR_LENGTH = 1 << INTERIOR_BITS;

	// How many bits should we consume at leaf level
	static const int LEAF_BITS = BITS - 2 * INTERIOR_BITS;
	static const int LEAF_LENGTH = 1 << LEAF_BITS;

	// Interior node
	struct Node {
		Node* ptrs[INTERIOR_LENGTH];
	};

	// Leaf node
	struct Leaf {
		void* values[LEAF_LENGTH];
	};

	Node* root_;                          // Root of radix tree
	void* (*allocator_)(size_t);          // Memory allocator

	Node* NewNode() {
		Node* result = reinterpret_cast<Node*>((*allocator_)(sizeof(Node)));
		if (result != NULL) {
			memset(result, 0, sizeof(*result));
		}
		return result;
	}

public:
	typedef uintptr_t Number;

	explicit TCMalloc_PageMap3(void* (*allocator)(size_t)) {
		allocator_ = allocator;
		root_ = NewNode();
	}

	void* get(Number k) const {
		const Number i1 = k >> (LEAF_BITS + INTERIOR_BITS);
		const Number i2 = (k >> LEAF_BITS) & (INTERIOR_LENGTH - 1);
		const Number i3 = k & (LEAF_LENGTH - 1);
		if ((k >> BITS) > 0 ||
			root_->ptrs[i1] == NULL || root_->ptrs[i1]->ptrs[i2] == NULL) {
			return NULL;
		}
		return reinterpret_cast<Leaf*>(root_->ptrs[i1]->ptrs[i2])->values[i3];
	}

	void set(Number k, void* v) {
		ASSERT(k >> BITS == 0);
		const Number i1 = k >> (LEAF_BITS + INTERIOR_BITS);
		const Number i2 = (k >> LEAF_BITS) & (INTERIOR_LENGTH - 1);
		const Number i3 = k & (LEAF_LENGTH - 1);
		reinterpret_cast<Leaf*>(root_->ptrs[i1]->ptrs[i2])->values[i3] = v;
	}

	bool Ensure(Number start, size_t n) {
		for (Number key = start; key <= start + n - 1;) {
			const Number i1 = key >> (LEAF_BITS + INTERIOR_BITS);
			const Number i2 = (key >> LEAF_BITS) & (INTERIOR_LENGTH - 1);

			// Check for overflow
			if (i1 >= INTERIOR_LENGTH || i2 >= INTERIOR_LENGTH)
				return false;

			// Make 2nd level node if necessary
			if (root_->ptrs[i1] == NULL) {
				Node* n = NewNode();
				if (n == NULL) return false;
				root_->ptrs[i1] = n;
			}

			// Make leaf node if necessary
			if (root_->ptrs[i1]->ptrs[i2] == NULL) {
				Leaf* leaf = reinterpret_cast<Leaf*>((*allocator_)(sizeof(Leaf)));
				if (leaf == NULL) return false;
				memset(leaf, 0, sizeof(*leaf));
				root_->ptrs[i1]->ptrs[i2] = reinterpret_cast<Node*>(leaf);
			}

			// Advance key past whatever is covered by this leaf node
			key = ((key >> LEAF_BITS) + 1) << LEAF_BITS;
		}
		return true;
	}

	void PreallocateMoreMemory() {
	}
};