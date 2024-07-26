/*--------------------------------------------------------------------------------------+
|
|     $Source: ToolsTest.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <catch2/catch_all.hpp>

#include "Tools.h"

using namespace SDK::Core;

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

	class IMyClass : public Tools::Factory<IMyClass>, public Tools::IDynType
	{
	public:
		virtual int Fct1() = 0;
	};

	class MyClass : public Tools::TypeId<MyClass>, public IMyClass
	{
	public:
		std::uint64_t GetDynTypeId() override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) override	{ return (i == GetTypeId()); }
		int Fct1() override { return 245; };
	};

	class MyExtendedClass2 : public MyClass, public Tools::TypeId<MyExtendedClass2>
	{
	public:
		typedef MyClass BaseClass;
		using Tools::TypeId<MyExtendedClass2>::GetTypeId;
		std::uint64_t GetDynTypeId() override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) override
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
		std::uint64_t GetDynTypeId() override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) override
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
		std::uint64_t GetDynTypeId() override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) override
		{
			if (i == GetTypeId()) return true;
			return false;
		}
	};

	static_assert(MyClass::GetTypeId() != MyExtendedClass::GetTypeId());
}

template<>
std::function<std::shared_ptr<InterfaceTest::IMyClass>()> Tools::Factory<InterfaceTest::IMyClass>::newFct_ = []()
{
	std::shared_ptr<InterfaceTest::IMyClass> p(static_cast<InterfaceTest::IMyClass*>(new InterfaceTest::MyClass()));
	return p;
};

TEST_CASE("Tools:Interface")
{
	using namespace InterfaceTest;
	// check with base class
	{
		std::shared_ptr<IMyClass> pObj = IMyClass::New();
		REQUIRE(pObj->Fct1() == 245);
		REQUIRE(pObj->GetDynTypeId() == MyClass::GetTypeId());
	}
	{
		// We want MyExtendedClass to be instantiate everywhere we need a IMyClass, so we define the "New" function.
		IMyClass::SetNewFct([]() {
			std::shared_ptr<IMyClass> p(static_cast<IMyClass*>(new MyExtendedClass));
			return p;
			});

		std::shared_ptr<IMyClass> pObj = IMyClass::New();
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

