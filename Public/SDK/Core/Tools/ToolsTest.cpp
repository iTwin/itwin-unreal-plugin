/*--------------------------------------------------------------------------------------+
|
|     $Source: ToolsTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <catch2/catch_all.hpp>

#include "Tools.h"
#include <iostream>

using namespace AdvViz::SDK;

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

int main(int argc, char* argv[]) {
	// setup ...
	AdvViz::SDK::Tools::InitAssertHandler("Test"); // to prevent assert to abort (default behaviour of libassert)

	int result = Catch::Session().run(argc, argv);

	// clean-up...

	return result;
}