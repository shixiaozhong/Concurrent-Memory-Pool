#pragma once
#include"Common.h"

// Single-level array
template <int BITS>
class TCMalloc_PageMap1 {
private:
	static const int LENGTH = 1 << BITS;// 设置映射关系的数目，例如32位下，每页8kb，最多有2^19个页
	void** array_;

public:
	typedef uintptr_t Number; // uintptr_t是unsiged int typedef的

	// 构造函数
	explicit TCMalloc_PageMap1() {
		size_t size = sizeof(void*) << BITS;		// 需要开辟数组的大小
		size_t alignSize = SizeClass::_RoundUp(size, 1 << PAGE_SHIFT);	// 按页对齐，一页是8kb
		array_ = (void**)SystemAlloc(alignSize >> PAGE_SHIFT);	// 向堆申请内存
		memset(array_, 0, sizeof(void*) << BITS);	// 将数组初始化为0
	}

	// Return the current value for KEY.  Returns NULL if not yet set,
	// or if k is out of range.
	// 在映射的表中查找
	void* get(Number k) const 
	{
		if ((k >> BITS) > 0) // 不是合法页号，例如32平台下，页号只需要19位来存储，右移19位如果大于0，表示不是合法页号
		{
			return NULL;
		}
		return array_[k];
	}

	// REQUIRES "k" is in range "[0,2^BITS-1]".
	// REQUIRES "k" has been ensured before.
	//
	// Sets the value 'v' for key 'k'.
	// 传入页号和span，构建映射关系
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

	static const int LEAF_BITS = BITS - ROOT_BITS;	// 14位
	static const int LEAF_LENGTH = 1 << LEAF_BITS;	// 2^14

	// root层中每个单位存储的元素类型
	struct Leaf {
		void* values[LEAF_LENGTH];
	};

	Leaf* root_[ROOT_LENGTH];             // root层的数组

public:
	typedef uintptr_t Number;

	// 构造函数
	explicit TCMalloc_PageMap2() {
		memset(root_, 0, sizeof(root_));	// 将root数组置为NULL
		PreallocateMoreMemory();
	}

	void* get(Number k) const 
	{
		const Number i1 = k >> LEAF_BITS;			// 获取对应的root层对应的下标
		const Number i2 = k & (LEAF_LENGTH - 1);	// 获取对应的第二层的下标
		//页号不在范围内或者没有建立映射关系
		if ((k >> BITS) > 0 || root_[i1] == NULL) 
		{
			return NULL;
		}
		return root_[i1]->values[i2];
	}

	void set(Number k, void* v)
	{
		const Number i1 = k >> LEAF_BITS;			// 获取对应的root层对应的下标
		const Number i2 = k & (LEAF_LENGTH - 1);	// 获取对应的第二层的下标
		assert(i1 < ROOT_LENGTH);	// 断言是否在root的大小之内
		root_[i1]->values[i2] = v;	// 建立映射关系
	}

	bool Ensure(Number start, size_t n) {
		for (Number key = start; key <= start + n - 1;) 
		{
			const Number i1 = key >> LEAF_BITS;	// 获取root层对应的下标

			// 超过了root层的长度
			if (i1 >= ROOT_LENGTH)
				return false;

			// Make 2nd level node if necessary
			// 第一层i1指向的空间未开辟
			if (root_[i1] == NULL) {
				static ObjectPool<Leaf>	leafPool;
				Leaf* leaf = (Leaf*)leafPool.New();	// 使用定长内存池开辟空间

				memset(leaf, 0, sizeof(*leaf));		// 将leaf置为NULL
				root_[i1] = leaf;					// 将leaf保存到root中
			}

			// Advance key past whatever is covered by this leaf node
			key = ((key >> LEAF_BITS) + 1) << LEAF_BITS;	// 继续向后检查，对应的位++
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