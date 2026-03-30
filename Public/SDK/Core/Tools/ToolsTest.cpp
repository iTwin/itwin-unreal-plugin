/*--------------------------------------------------------------------------------------+
|
|     $Source: ToolsTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <catch2/catch_all.hpp>

#include "Tools.h"
#include "SharedRecursiveMutex.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>

#include <mutex>

using namespace AdvViz::SDK;
using namespace AdvViz::SDK::Tools;

// Helper macro to conditionally check state
#ifndef RELEASE_CONFIG
	#define CHECK_STATE(mutex, expectedState) REQUIRE((mutex).GetState() == (expectedState))
#else
	#define CHECK_STATE(mutex, expectedState) (void)0
#endif

namespace ExtensionTest {
	class MyClass : public Tools::ExtensionSupport
	{
	};

	class MyExtension : public Tools::Extension, public Tools::TypeId<MyExtension>
	{
		int member = 356;
	};

	class BadExt : public Tools::Extension, public Tools::TypeId<BadExt>
	{
	};

	namespace DummyNS
	{
		class Flower :public Tools::TypeId<Flower>
		{};
	}

	class Flower :public Tools::TypeId<Flower>
	{};

	// note: for the compiler, GetTypeId is ambiguous between Tools::TypeId<Flower2>::GetTypeId and Flower::GetTypeId, we need to precise which one to use explicitly.
	class Flower2 :public Tools::TypeId<Flower2>, public Flower
	{
	public:
		using Tools::TypeId<Flower2>::GetTypeId;
	};

	static_assert(Flower::GetTypeId() != DummyNS::Flower::GetTypeId());
	static_assert(Flower::GetTypeId() != Flower2::GetTypeId());
}

TEST_CASE("Tools:Extension") 
{
	using namespace ExtensionTest;
	MyClass myclass;
	std::shared_ptr<MyExtension> ext(new MyExtension);
	myclass.AddExtension(ext);

	REQUIRE(myclass.HasExtension<MyExtension>() == true);
	REQUIRE(myclass.GetExtension<MyExtension>().get() == ext.get());

	REQUIRE(myclass.GetExtension<BadExt>().get() == nullptr);
	REQUIRE(myclass.HasExtension<BadExt>() == false);

	myclass.RemoveExtension<MyExtension>();
	REQUIRE(myclass.HasExtension<MyExtension>() == false);
	REQUIRE(myclass.GetExtension<MyExtension>().get() == nullptr);
}

namespace InterfaceTest {

	class IMyClass : public Tools::Factory<IMyClass>
	{
	public:
		virtual int Fct1() = 0;
	};

	class MyClass : public Tools::TypeId<MyClass>, public IMyClass
	{
	public:
		using Tools::TypeId<MyClass>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override	{ return (i == GetTypeId()); }
		int Fct1() override { return 245; };
	};

	class MyExtendedClass2 : public MyClass, public Tools::TypeId<MyExtendedClass2>
	{
	public:
		typedef MyClass BaseClass;
		using Tools::TypeId<MyExtendedClass2>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override
		{
			if (i == GetTypeId()) return true;
			return BaseClass::IsTypeOf(i);
		}

		int value_ = 741;
		int Fct3() { return value_; }
	};

	class MyExtendedClass : public MyExtendedClass2, public Tools::TypeId<MyExtendedClass>
	{
	public:
		typedef MyExtendedClass2 BaseClass;
		using Tools::TypeId<MyExtendedClass>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override
		{
			if (i == GetTypeId()) return true;
			return BaseClass::IsTypeOf(i);
		}
		
		int Fct1() override {
			REQUIRE(BaseClass::Fct1() == 245);
			return 654;
		}
		
		int value_ = 987;
		int Fct2() { return value_; }
	};

	class DummyClass : public Tools::IDynType, public Tools::TypeId<DummyClass> {
	public:
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override
		{
			if (i == GetTypeId()) return true;
			return false;
		}
	};

	static_assert(MyClass::GetTypeId() != MyExtendedClass::GetTypeId());
}

template<>
Tools::Factory<InterfaceTest::IMyClass>::Globals::Globals()
{
	newFct_ = [](){
			return static_cast<InterfaceTest::IMyClass*>(new InterfaceTest::MyClass());
		};
}

template<>
Tools::Factory<InterfaceTest::IMyClass>::Globals& Tools::Factory<InterfaceTest::IMyClass>::GetGlobals()
{
	static Tools::Factory<InterfaceTest::IMyClass>::Globals globals;
	return globals;
}

TEST_CASE("Tools:Interface")
{
	using namespace InterfaceTest;
	// check with base class
	{
		std::shared_ptr<IMyClass> pObj(IMyClass::New());
		REQUIRE(pObj->Fct1() == 245);
		REQUIRE(pObj->GetDynTypeId() == MyClass::GetTypeId());
	}
	{
		// We want MyExtendedClass to be instantiate everywhere we need a IMyClass, so we define the "New" function.
		IMyClass::SetNewFct([]() {
			IMyClass* p(static_cast<IMyClass*>(new MyExtendedClass));
			return p;
			});

		std::shared_ptr<IMyClass> pObj(IMyClass::New());
		REQUIRE(pObj->Fct1() == 654);
		REQUIRE(pObj->GetDynTypeId() == MyExtendedClass::GetTypeId());
		REQUIRE(pObj->IsTypeOf(MyClass::GetTypeId()) == true);
		REQUIRE(pObj->IsTypeOf(MyExtendedClass2::GetTypeId()) == true);
		REQUIRE(pObj->IsTypeOf(DummyClass::GetTypeId()) == false);
		REQUIRE(pObj->IsTypeOf(MyExtendedClass::GetTypeId()) == true);
		if (pObj->GetDynTypeId() == MyExtendedClass::GetTypeId())
		{
			std::shared_ptr<MyExtendedClass> ext = std::static_pointer_cast<MyExtendedClass>(pObj);
			REQUIRE(ext->Fct2() == 987);
		}

		auto obj2 = Tools::DynamicCast<MyExtendedClass2>(pObj);
		REQUIRE((bool)obj2 == true);
		if (obj2)
		{
			REQUIRE(obj2->Fct3() == 741);
		}
	}
}

static std::vector<std::string> g_LogList;
class MyLog : public AdvViz::SDK::Tools::Log
{
public:
	MyLog(std::string s, AdvViz::SDK::Tools::Level level):AdvViz::SDK::Tools::Log(s, level)
	{}

	void DoLog(const std::string& msg, AdvViz::SDK::Tools::Level sev, const char* srcPath, const char* func, int line)
	{
		g_LogList.push_back(msg);
		AdvViz::SDK::Tools::Log::DoLog(msg, sev, srcPath, func, line);
	}
};

TEST_CASE("Tools:Log")
{
	using namespace Tools;

	ILog::SetNewFct([](std::string s, Level level) {
		ILog* p(static_cast<MyLog*>(new MyLog(s, level)));
		return p;
		});

	InitLog("log_Test.txt");

	CreateLogChannel("test", Level::info);
	BE_LOGD("test", "print:" << 99); // should not be log
	BE_LOGI("test", "print:" << 10); //should be log
	BE_LOGI("test", "早上好" << 52); //should be log
	BE_LOGI("test1", "print2:" << 33); // should not be log

	REQUIRE(g_LogList.size() == 2);
	REQUIRE(g_LogList[0] == "print:10");
	REQUIRE(g_LogList[1] == "早上好52");

	BE_GETLOG("test")->SetLevel(Level::debug);
	BE_LOGD("test", "print:" << 99);

	REQUIRE(g_LogList.size() == 3);
	REQUIRE(g_LogList[2] == "print:99"); //should be log now

}

AdvViz::expected<int, std::string> to_int(char const* const text)
{
	char* pos = nullptr;
	auto value = strtol(text, &pos, 0);

	if (pos != text) 
		return value;
	else
		return AdvViz::make_unexpected(std::string("'") + text + "' isn't a number");
}

AdvViz::expected<void, std::string> TestExpected(int i)
{
	if (i > 0)
		return {}; // same as: AdvViz::expected<void, std::string>() but shorter
	else
		return AdvViz::make_unexpected("i is neg number");
}

TEST_CASE("Tools:expected")
{
	{
		auto ei = to_int("toto");
		REQUIRE((bool)ei == false);
		REQUIRE(ei.has_value() == false);
		REQUIRE(ei.error() == "'toto' isn't a number");
	}

	{
		auto ei = to_int("45");
		REQUIRE(ei.has_value() == true);
		REQUIRE(*ei == 45);
	}

	{
		auto ei = TestExpected(1);
		REQUIRE((bool)ei == true);
	}

	{
		auto ei = TestExpected(-11);
		REQUIRE((bool)ei == false);
		REQUIRE(ei.error() == "i is neg number");
	}
}

std::string g_AssertCheckStr;
class MyAssertHandler :public AdvViz::SDK::Tools::AssertHandler
{
public:
	void Handler(const libassert::assertion_info& info) override
	{
		REQUIRE(info.type == libassert::assert_type::assertion);
		g_AssertCheckStr = "success &ddefe";
		std::string message = info.to_string(
			libassert::terminal_width(libassert::stderr_fileno),
			libassert::isatty(libassert::stderr_fileno)
			? libassert::get_color_scheme()
			: libassert::color_scheme::blank
		);
		std::cout << "Assert handler message recieved:" << message << std::endl;
	}
};

TEST_CASE("Tools:AssertHandler")
{
	using namespace AdvViz::SDK::Tools;
	auto prevHandler = IAssertHandler::GetNewFct();
	IAssertHandler::SetNewFct([]() {
		IAssertHandler* p(static_cast<MyAssertHandler*>(new MyAssertHandler));
		return p;
		});

	InitAssertHandler("Test");
	auto myVar = "test param";
	BE_ASSERT(false == true, "test assert", myVar); // ignore this assert when debugging
	BE_ISSUE("test Issue texte"); // ignore this assert when debugging
	REQUIRE(g_AssertCheckStr == "success &ddefe");
	//restore previous handler
	IAssertHandler::SetNewFct(prevHandler);
	InitAssertHandler("Test");
}

TEST_CASE("Tools:StrongType")
{
	using namespace AdvViz::SDK::Tools;

	class Tag1 {};
	using TId1 = StrongTypeId<Tag1>;

	class Tag2 {};
	using TId2 = StrongTypeId<Tag2>;

	TId1 id1("plop");
	TId2 id2("bob");

	//id1 = "titi"; // compile should fail
	id1 = TId1("titi"); // ok
	//id1 = id2; // compile should fail
	//std::string s = id1; //compile should fail
	std::string s = static_cast<std::string>(id1);
}


std::vector<std::string> g_logs;
std::mutex g_logsMutex;
void addLog(const std::string& s)
{
	std::lock_guard<std::mutex> lock(g_logsMutex);
	g_logs.emplace_back(s);
}

#define TSLOG(a)\
{ std::stringstream s; s<<a; addLog(s.str());}

std::vector<std::string> g_failures;
std::mutex g_FailureMutex;
void addFailure(const std::string& s)
{
	std::lock_guard<std::mutex> lock(g_FailureMutex);
	g_failures.emplace_back(s);
}

#define TSFAILURE(c, a)\
{if (!(c)) { std::stringstream tmpStream; tmpStream<<a; addFailure(tmpStream.str());}}

void PrintLogsAndFailures()
{
	std::lock_guard<std::mutex> lock1(g_FailureMutex);
	std::lock_guard<std::mutex> lock2(g_logsMutex);
	if (!g_failures.empty())
	{
		std::cout << "---- Failures ----" << std::endl;
		for (const auto& fail : g_failures)
		{
			std::cout << fail << std::endl;
		}
		REQUIRE(g_failures.empty());
		g_failures.clear();
	}
	for (const auto& log : g_logs)
	{
		std::cout << log << std::endl;
	}
	g_logs.clear();
}

TEST_CASE("Tools:SharedRecursiveMutex - Basic read lock")
{

#ifndef RELEASE_CONFIG
	SharedRecursiveMutex::EnableStateTracking(true);
#endif
	SharedRecursiveMutex mutex;
	int sharedData = 0;
	
	CHECK_STATE(mutex, 0); // Initially unlocked
	
	// Single read lock
	{
		mutex.lock_shared();
		CHECK_STATE(mutex, 2); // Read locked
		int value = sharedData;
		REQUIRE(value == 0);
		mutex.unlock_shared();
		CHECK_STATE(mutex, 0); // Unlocked
	}
	
	// Recursive read locks
	{
		mutex.lock_shared();
		CHECK_STATE(mutex, 2);
		mutex.lock_shared();
		CHECK_STATE(mutex, 2);
		mutex.lock_shared();
		CHECK_STATE(mutex, 2);
		int value = sharedData;
		REQUIRE(value == 0);
		mutex.unlock_shared();
		CHECK_STATE(mutex, 2); // Still read locked
		mutex.unlock_shared();
		CHECK_STATE(mutex, 2); // Still read locked
		mutex.unlock_shared();
		CHECK_STATE(mutex, 0); // Finally unlocked
	}
}

TEST_CASE("Tools:SharedRecursiveMutex - Basic write lock")
{
#ifndef RELEASE_CONFIG
	SharedRecursiveMutex::EnableStateTracking(true);
#endif
	SharedRecursiveMutex mutex;
	int sharedData = 0;
	
	CHECK_STATE(mutex, 0);
	
	// Single write lock
	{
		mutex.lock();
		CHECK_STATE(mutex, 1); // Write locked
		sharedData = 42;
		REQUIRE(sharedData == 42);
		mutex.unlock();
		CHECK_STATE(mutex, 0);
	}
	
	// Recursive write locks
	{
		mutex.lock();
		CHECK_STATE(mutex, 1);
		sharedData = 10;
		mutex.lock();
		CHECK_STATE(mutex, 1);
		sharedData += 5;
		mutex.lock();
		CHECK_STATE(mutex, 1);
		sharedData += 3;
		REQUIRE(sharedData == 18);
		mutex.unlock();
		CHECK_STATE(mutex, 1); // Still write locked
		mutex.unlock();
		CHECK_STATE(mutex, 1); // Still write locked
		mutex.unlock();
		CHECK_STATE(mutex, 0);
	}
}

TEST_CASE("Tools:SharedRecursiveMutex - Read then write (promotion)")
{
#ifndef RELEASE_CONFIG
	SharedRecursiveMutex::EnableStateTracking(true);
#endif
	SharedRecursiveMutex mutex;
	int sharedData = 0;
	
	CHECK_STATE(mutex, 0);
	
	// Acquire read lock, then upgrade to write
	{
		mutex.lock_shared();
		CHECK_STATE(mutex, 2);
		int value = sharedData;
		REQUIRE(value == 0);
		
		// Promote to write lock
		mutex.lock();
		CHECK_STATE(mutex, 1); // Now write locked
		sharedData = 100;
		REQUIRE(sharedData == 100);
		mutex.unlock();
		CHECK_STATE(mutex, 2); // Read lock is RESTORED after write unlock
		
		// Need to unlock the restored read lock
		mutex.unlock_shared();
		CHECK_STATE(mutex, 0);
	}
}

TEST_CASE("Tools:SharedRecursiveMutex - State consistency during promotion")
{
#ifndef RELEASE_CONFIG
	SharedRecursiveMutex::EnableStateTracking(true);
#endif
	SharedRecursiveMutex mutex;
	
	CHECK_STATE(mutex, 0);
	
	// Test promotion with multiple read locks
	mutex.lock_shared();
	CHECK_STATE(mutex, 2);
	mutex.lock_shared();
	CHECK_STATE(mutex, 2);
	
	// Promote to write
	mutex.lock();
	CHECK_STATE(mutex, 1); // Should be write locked now
	
	mutex.unlock();
	CHECK_STATE(mutex, 2); // Read locks are RESTORED (readCount was 2)
	
	// Need to unlock the restored read locks
	mutex.unlock_shared();
	CHECK_STATE(mutex, 2); // Still have one read lock
	mutex.unlock_shared();
	CHECK_STATE(mutex, 0); // Now completely unlocked
}

TEST_CASE("Tools:SharedRecursiveMutex - State after try_lock promotion")
{
#ifndef RELEASE_CONFIG
	SharedRecursiveMutex::EnableStateTracking(true);
#endif
	SharedRecursiveMutex mutex;
	
	CHECK_STATE(mutex, 0);
	
	// Acquire read lock
	mutex.lock_shared();
	CHECK_STATE(mutex, 2);
	
	// Try to promote
	bool promoted = mutex.try_lock();
	if (promoted)
	{
		CHECK_STATE(mutex, 1); // Should be write locked
		mutex.unlock();
		CHECK_STATE(mutex, 2); // Read lock is RESTORED after write unlock
		
		// Need to unlock the restored read lock
		mutex.unlock_shared();
	}
	else
	{
		// Failed promotion should restore read lock
		CHECK_STATE(mutex, 2);
		mutex.unlock_shared();
	}
	
	CHECK_STATE(mutex, 0);
}

TEST_CASE("Tools:SharedRecursiveMutex - State transitions")
{
#ifndef RELEASE_CONFIG
	SharedRecursiveMutex::EnableStateTracking(true);
#endif
	SharedRecursiveMutex mutex;
	
	// Test various state transitions
	CHECK_STATE(mutex, 0); // Start unlocked
	
	// 0 -> 2 (read lock)
	mutex.lock_shared();
	CHECK_STATE(mutex, 2);
	
	// 2 -> 1 (promote to write)
	mutex.lock();
	CHECK_STATE(mutex, 1);
	
	// 1 -> 1 (acquire read while holding write)
	mutex.lock_shared();
	CHECK_STATE(mutex, 1);
	
	// 1 -> 1 (release read while holding write)
	mutex.unlock_shared();
	CHECK_STATE(mutex, 1);
	
	// 1 -> 2 (release write, read lock is restored from promotion)
	mutex.unlock();
	CHECK_STATE(mutex, 2);
	
	// 2 -> 0 (release the restored read lock)
	mutex.unlock_shared();
	CHECK_STATE(mutex, 0);
}

TEST_CASE("Tools:SharedRecursiveMutex - Multiple readers")
{
#ifndef RELEASE_CONFIG
	SharedRecursiveMutex::EnableStateTracking(false);
#endif
	SharedRecursiveMutex mutex;
	std::atomic<int> readersCount{ 0 };
	std::atomic<int> maxConcurrentReaders{ 0 };
	int sharedData = 42;
	std::atomic<int> threadsStarted{ 0 };
	std::atomic<int> threadsCompleted{ 0 };
	std::atomic<bool> testFailed{ false };

	std::vector<std::thread> threads;
	const int numReaders = 5;

	g_logs.clear();
	g_failures.clear();
	TSLOG( "[Multiple readers] Starting test with " << numReaders << " readers" );

	for (int i = 0; i < numReaders; ++i)
	{
		threads.emplace_back([&mutex, &readersCount, &maxConcurrentReaders, &sharedData, &threadsStarted, &threadsCompleted, &testFailed, numReaders, threadId = i]() {
			TSLOG( "[Thread " << threadId << "] Started" );

			// Signal that this thread has started
			int started = ++threadsStarted;
			TSLOG( "[Thread " << threadId << "] Incremented threadsStarted to " << started );

			// Wait for all threads to be ready with timeout
			auto startTime = std::chrono::steady_clock::now();
			constexpr auto timeout = std::chrono::seconds(5);
			while (threadsStarted.load() < numReaders)
			{
				if (std::chrono::steady_clock::now() - startTime > timeout)
				{
					TSLOG("[Thread " << threadId << "] TIMEOUT waiting for threads. threadsStarted="
						<< threadsStarted.load() << "/" << numReaders );
					testFailed = true;
					FAIL("Timeout waiting for threads to start");
					return;
				}
				std::this_thread::yield();
			}

			TSLOG( "[Thread " << threadId << "] All threads ready, acquiring lock_shared" );

			mutex.lock_shared();
			TSLOG( "[Thread " << threadId << "] Acquired lock_shared" );

			int concurrent = ++readersCount;

			// Track maximum concurrent readers
			int maxSoFar = maxConcurrentReaders.load();
			while (concurrent > maxSoFar)
			{
				if (maxConcurrentReaders.compare_exchange_weak(maxSoFar, concurrent))
				{
					TSLOG( "[Thread " << threadId << "] New max concurrent readers: " << concurrent );
				}
			}

			// Simulate some read work
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			int value = sharedData;
			TSFAILURE(value == 42, "value == 42, bad value");

			readersCount--;
			TSLOG( "[Thread " << threadId << "] Releasing lock_shared" );
			mutex.unlock_shared();

			++threadsCompleted;
			TSLOG( "[Thread " << threadId << "] Completed" );
			});
	}

	TSLOG( "[Multiple readers] Waiting for threads to join..." );

	// Wait for all threads to complete with timeout
	auto joinStart = std::chrono::steady_clock::now();
	for (size_t i = 0; i < threads.size(); ++i)
	{
		auto& thread = threads[i];
		if (thread.joinable())
		{
			thread.join();
			TSLOG( "[Multiple readers] Thread " << i << " joined" );
		}
	}

	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - joinStart).count();
	TSLOG( "[Multiple readers] All threads joined in " << elapsed << "ms" );
	TSLOG( "[Multiple readers] threadsStarted=" << threadsStarted.load()
		<< ", threadsCompleted=" << threadsCompleted.load()
		<< ", maxConcurrentReaders=" << maxConcurrentReaders.load() );

	PrintLogsAndFailures();

	REQUIRE_FALSE(testFailed.load());
	// Verify multiple readers were active simultaneously
	REQUIRE(maxConcurrentReaders.load() > 1);
	REQUIRE(readersCount.load() == 0);
}

TEST_CASE("Tools:SharedRecursiveMutex - Writer exclusion")
{
	SharedRecursiveMutex mutex;
#ifndef RELEASE_CONFIG
	SharedRecursiveMutex::EnableStateTracking(false);
#endif
	std::atomic<bool> writerActive{false};
	std::atomic<int> maxConcurrentWriters{0};
	std::atomic<int> currentWriters{0};
	int sharedData = 0;
	
	std::vector<std::thread> threads;
	const int numWriters = 3;
	g_logs.clear();
	g_failures.clear();
	for (int i = 0; i < numWriters; ++i)
	{
		threads.emplace_back([&, threadId = i]() {
			mutex.lock();
			
			// Track concurrent writers
			int concurrent = ++currentWriters;
			int maxSoFar = maxConcurrentWriters.load();
			while (concurrent > maxSoFar)
			{
				maxConcurrentWriters.compare_exchange_weak(maxSoFar, concurrent);
			}
			
			// Only one writer should be active at a time
			TSFAILURE(writerActive.exchange(true) == false, "writerActive.exchange(true) == false failed");
			
			// Simulate some write work
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			sharedData = threadId;
			
			TSFAILURE(writerActive.exchange(false) == true, "writerActive.exchange(false) == true failed");
			--currentWriters;
			
			mutex.unlock();
		});
	}
	
	for (auto& thread : threads)
	{
		thread.join();
	}
	
	PrintLogsAndFailures();
	REQUIRE(g_failures.size() == 0);
	// At most one writer should have been active at any time
	REQUIRE(maxConcurrentWriters.load() == 1);
}


TEST_CASE("Tools:SharedRecursiveMutex - Reader/Writer interaction")
{
#ifndef RELEASE_CONFIG
	SharedRecursiveMutex::EnableStateTracking(false);
#endif
	SharedRecursiveMutex mutex;
	std::atomic<int> value{ 0 };
	std::atomic<bool> writerDone{ false };
	std::atomic<bool> testFailed{ false };
	std::atomic<int> readersRunning{ 0 };
	std::atomic<int> readersCompleted{ 0 };
	constexpr int numReaders = 3;
	constexpr auto timeout = std::chrono::seconds(10);

	g_logs.clear();
	g_failures.clear();
	TSLOG("[Reader/Writer] Starting test with " << numReaders << " readers");

	// Reader threads
	std::vector<std::thread> readers;
	for (int i = 0; i < numReaders; ++i)
	{
		readers.emplace_back([&, readerId = i]() {
			TSLOG("[Reader " << readerId << "] Started");
			readersRunning++;

			auto startTime = std::chrono::steady_clock::now();
			int iterations = 0;

			while (!writerDone.load(std::memory_order_acquire))
			{
				// Timeout check to prevent infinite loop
				if (std::chrono::steady_clock::now() - startTime > timeout)
				{
					TSLOG("[Reader " << readerId << "] TIMEOUT after " << iterations
						<< " iterations. writerDone=" << writerDone.load());
					testFailed = true;
					break;
				}

				TSLOG( "[Reader " << readerId << "] All threads ready, acquiring lock_shared");
				mutex.lock_shared();
				TSLOG( "[Reader " << readerId << "] Acquired lock_shared");

				int currentValue = value.load();
				// Value should be stable while we hold read lock
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
				int afterValue = value.load();
				// Either we're reading while writer is blocked (values equal)
				// or we're reading after writer finished (both are final value)
				if (!(afterValue == currentValue || writerDone.load()))
				{
					TSLOG("[Reader " << readerId << "] INVARIANT VIOLATED: currentValue="
						<< currentValue << ", afterValue=" << afterValue
						<< ", writerDone=" << writerDone.load());
				}
				TSFAILURE((afterValue == currentValue || writerDone.load()), "[Reader " << readerId << "] (afterValue == currentValue || writerDone.load()) failed");
				TSLOG( "[Reader " << readerId << "] Releasing lock_shared");
				mutex.unlock_shared();
				TSLOG( "[Reader " << readerId << "] lock_shared Released ");

				iterations++;
				// Small yield to allow writer to acquire lock
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}

			readersRunning--;
			readersCompleted++;
			TSLOG("[Reader " << readerId << "] Completed after " << iterations << " iterations");
			});
	}

	// Wait for all readers to start with timeout
	TSLOG("[Reader/Writer] Waiting for readers to start...");
	auto waitStart = std::chrono::steady_clock::now();
	while (readersRunning.load() < numReaders)
	{
		if (std::chrono::steady_clock::now() - waitStart > timeout)
		{
			TSLOG("[Reader/Writer] TIMEOUT waiting for readers to start. readersRunning="
				<< readersRunning.load() << "/" << numReaders);
			testFailed = true;
			writerDone = true; // Signal readers to exit
			break;
		}
		std::this_thread::yield();
	}
	TSLOG("[Reader/Writer] All " << readersRunning.load() << " readers running");

	// Writer thread
	std::thread writer([&]() {
		TSLOG( "[Writer] Started, sleeping before write...");
		std::this_thread::sleep_for(std::chrono::milliseconds(20));

		TSLOG( "[Writer] Acquiring write lock...");
		mutex.lock();
		TSLOG( "[Writer] Write lock acquired, incrementing value");

		for (int i = 0; i < 10; ++i)
		{
			value++;
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}

		TSLOG( "[Writer] Releasing write lock, final value=" << value.load());
		mutex.unlock();

		writerDone.store(true, std::memory_order_release);
		TSLOG( "[Writer] Completed, writerDone=true");
		});

	TSLOG( "[Reader/Writer] Waiting for writer to join...");
	writer.join();
	TSLOG( "[Reader/Writer] Writer joined");

	// Wait for readers to finish with timeout
	TSLOG( "[Reader/Writer] Waiting for readers to join...");
	for (size_t i = 0; i < readers.size(); ++i)
	{
		if (readers[i].joinable())
		{
			readers[i].join();
			TSLOG( "[Reader/Writer] Reader " << i << " joined");
		}
	}

	TSLOG( "[Reader/Writer] Test complete. value=" << value.load()
		<< ", readersCompleted=" << readersCompleted.load()
		<< ", testFailed=" << testFailed.load());

	PrintLogsAndFailures();
	REQUIRE_FALSE(testFailed.load());
	REQUIRE(value.load() == 10);
}

TEST_CASE("Tools:SharedRecursiveMutex - Complex lock promotion scenario")
{
#ifndef RELEASE_CONFIG
	SharedRecursiveMutex::EnableStateTracking(false);
#endif
	SharedRecursiveMutex mutex;
	std::atomic<int> sharedData{ 0 };
	std::atomic<bool> promotionHappened{ false };
	std::atomic<bool> promoterReady{ false };
	std::atomic<bool> readerReady{ false };
	g_failures.clear();
	g_logs.clear();
	std::thread promoter([&]() {
		// Start with read lock
		mutex.lock_shared();
		int initialValue = sharedData.load();
		promoterReady = true;

		// Wait for reader to be ready to create the race condition
		while (!readerReady.load())
		{
			std::this_thread::yield();
		}

		// Small delay to ensure reader has time to acquire its read lock
		std::this_thread::sleep_for(std::chrono::milliseconds(5));

		// Promote to write lock (this will block until reader releases if reader got lock first)
		mutex.lock();
		promotionHappened.store(true, std::memory_order_release);
		sharedData = initialValue + 100;
		mutex.unlock();

		mutex.unlock_shared();
		});

	std::thread reader([&]() {
		// Wait for promoter to acquire its initial read lock
		while (!promoterReady.load())
		{
			std::this_thread::yield();
		}

		// Signal we're ready
		readerReady = true;

		// Try to acquire read lock (may succeed immediately or wait for promotion)
		mutex.lock_shared();
		int value = sharedData.load();
		bool promotion = promotionHappened.load(std::memory_order_acquire);

		// Should see either original value or updated value, but not inconsistent state
		if (promotion)
		{
			// If promotion happened, we must see the updated value
			TSFAILURE(value == 100, "value == 100, bad value");
		}
		else
		{
			// If promotion hasn't happened yet, we see original value
			TSFAILURE(value == 0, "value == 0, bad value");
		}
		mutex.unlock_shared();
		});

	promoter.join();
	reader.join();
	PrintLogsAndFailures();
	REQUIRE(sharedData.load() == 100);
	REQUIRE(promotionHappened.load() == true);
}

TEST_CASE("Tools:SharedRecursiveMutex - Stress test")
{
#ifndef RELEASE_CONFIG
	SharedRecursiveMutex::EnableStateTracking(false);
#endif

	SharedRecursiveMutex mutex;
	std::atomic<int> sharedCounter{ 0 };
	const int numThreads = 10;
	const int operationsPerThread = 100;
	std::atomic<int> totalWrites{ 0 };

	std::vector<std::thread> threads;

	for (int t = 0; t < numThreads; ++t)
	{
		threads.emplace_back([&, threadId = t]() {
			for (int i = 0; i < operationsPerThread; ++i)
			{
				if (threadId % 2 == 0)
				{
					// Even threads: direct write
					mutex.lock();
					sharedCounter++;
					totalWrites++;
					mutex.unlock();
				}
				else
				{
					// Odd threads: read, then occasionally promote to write
					mutex.lock_shared();
					int value = sharedCounter.load();

					// Occasionally promote to write
					if (value % 3 == 0)
					{
						mutex.lock(); // Promote from read to write
						sharedCounter++;
						totalWrites++;
						mutex.unlock(); // Release write, read lock is restored
						mutex.unlock_shared(); // Release the restored read lock
					}
					else
					{
						// Just read
						mutex.unlock_shared();
					}
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
			});
	}

	for (auto& thread : threads)
	{
		thread.join();
	}

	// Verify we got the expected number of writes
	REQUIRE(sharedCounter.load() == totalWrites.load());
	// At least the even threads should have incremented (numThreads/2 * operationsPerThread)
	// Plus some promotions from odd threads
	REQUIRE(sharedCounter.load() >= (numThreads / 2) * operationsPerThread);
}

//TEST_CASE("Failure")
//{
//	INFO("This test is expected to fail an assertion.");
//	REQUIRE(1 + 1 == 3);
//}

int main(int argc, char* argv[]) {
	// setup ...
	AdvViz::SDK::Tools::InitAssertHandler("Test"); // to prevent assert to abort (default behaviour of libassert)
	
#ifndef RELEASE_CONFIG
	SharedRecursiveMutex::EnableStateTracking(true);
#endif

	int result = Catch::Session().run(argc, argv);

	// clean-up...

	return result;
}
