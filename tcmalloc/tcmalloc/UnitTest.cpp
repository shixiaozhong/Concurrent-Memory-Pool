#include"ConcurrentAlloc.h"


void MutilThreadAlloc1()
{
	std::vector<void*> v;
	for (int i = 0; i < 5; i++)
	{
		void* ptr = ConcurrentAlloc(6);
		v.push_back(ptr);
	}
	for (auto e : v)
	{
		ConcurrentFree(e, 6);
	}
}
void MutilThreadAlloc2()
{
	std::vector<void*> v;
	for (int i = 0; i < 5; i++)
	{
		void* ptr = ConcurrentAlloc(6);
		v.push_back(ptr);
	}
	for (auto e : v)
	{
		ConcurrentFree(e, 6);
	}
}

void TestMutilThread()
{
	std::thread t1(MutilThreadAlloc1);

	std::thread t2(MutilThreadAlloc2);
	t1.join();

	t2.join();
}

void TestConcurrentAlloc()
{
	void* p1 = ConcurrentAlloc(6);
	void* p2 = ConcurrentAlloc(8);
	void* p3 = ConcurrentAlloc(1);
	void* p4 = ConcurrentAlloc(7);
	void* p5 = ConcurrentAlloc(8);
	void* p6 = ConcurrentAlloc(8);
	void* p7 = ConcurrentAlloc(8);



	std::cout << p1 << std::endl;
	std::cout << p2 << std::endl;
	std::cout << p3 << std::endl;
	std::cout << p4 << std::endl;
	std::cout << p5 << std::endl;

	ConcurrentFree(p1, 6);
	ConcurrentFree(p2, 8);
	ConcurrentFree(p3, 1);
	ConcurrentFree(p4, 7);
	ConcurrentFree(p5, 8);
	ConcurrentFree(p6, 8);
	ConcurrentFree(p7, 8);
}

void TestConcurrentAlloc2()
{
	for (size_t i = 0; i < 5; i++)
	{
		void* p1 = ConcurrentAlloc(6);
		std::cout << p1 << std::endl;
	}
	void* p2 = ConcurrentAlloc(8);
	std::cout << p2 << std::endl;
}


int main()
{
	//TestTLS();
	// TestConcurrentAlloc();
	TestConcurrentAlloc();
	//TestMutilThread();
	return 0;
}