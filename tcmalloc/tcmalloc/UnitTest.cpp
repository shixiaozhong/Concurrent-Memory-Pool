#define _CRT_SECURE_NO_WARNINGS 1

#include"ConcurrentAlloc.h"

void Alloc1()
{
	for (size_t i = 0; i < 2; ++i)
	{
		void* ptr = ConcurrentAlloc(6);
	}
}

void Alloc2()
{
	for (size_t i = 0; i < 2; ++i)
	{
		void* ptr = ConcurrentAlloc(7);
	}
}


void Test()
{
	std::thread t1(Alloc1);
	std::thread t2(Alloc2);
	t1.join();
	t2.join();
}

int main()
{
	Test();
	return 0;
}