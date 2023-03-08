//#include"ConcurrentAlloc.h"
//
//
//void MutilThreadAlloc1()
//{
//	std::vector<void*> v;
//	for (int i = 0; i < 5; i++)
//	{
//		void* ptr = ConcurrentAlloc(6);
//		v.push_back(ptr);
//	}
//	for (auto e : v)
//	{
//		ConcurrentFree(e);
//	}
//}
//void MutilThreadAlloc2()
//{
//	std::vector<void*> v;
//	for (int i = 0; i < 5; i++)
//	{
//		void* ptr = ConcurrentAlloc(6);
//		v.push_back(ptr);
//	}
//	for (auto e : v)
//	{
//		ConcurrentFree(e);
//	}
//}
//
//void TestMutilThread()
//{
//	std::thread t1(MutilThreadAlloc1);
//
//	std::thread t2(MutilThreadAlloc2);
//	t1.join();
//
//	t2.join();
//}
//
//void TestConcurrentAlloc()
//{
//	void* p1 = ConcurrentAlloc(6);
//	void* p2 = ConcurrentAlloc(8);
//	void* p3 = ConcurrentAlloc(1);
//	void* p4 = ConcurrentAlloc(7);
//	void* p5 = ConcurrentAlloc(8);
//	void* p6 = ConcurrentAlloc(8);
//	void* p7 = ConcurrentAlloc(8);
//
//
//
//	std::cout << p1 << std::endl;
//	std::cout << p2 << std::endl;
//	std::cout << p3 << std::endl;
//	std::cout << p4 << std::endl;
//	std::cout << p5 << std::endl;
//
//	ConcurrentFree(p1);
//	ConcurrentFree(p2);
//	ConcurrentFree(p3);
//	ConcurrentFree(p4);
//	ConcurrentFree(p5);
//	ConcurrentFree(p6);
//	ConcurrentFree(p7);
//}
//
//void TestConcurrentAlloc2()
//{
//	for (size_t i = 0; i < 5; i++)
//	{
//		void* p1 = ConcurrentAlloc(6);
//		std::cout << p1 << std::endl;
//	}
//	void* p2 = ConcurrentAlloc(8);
//	std::cout << p2 << std::endl;
//}
//
//
//
//void BigAlloc()
//{
//	void* p1 = ConcurrentAlloc(257 * 1024);
//	ConcurrentFree(p1);
//	void* p2 = ConcurrentAlloc(129 * 8 * 1024);
//	ConcurrentFree(p2);
//}
//
//
//int main()
//{
//	//TestTLS();
//	TestConcurrentAlloc();
//	//TestMutilThread();
//	BigAlloc();
//	return 0;
//}