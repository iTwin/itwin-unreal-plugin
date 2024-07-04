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

class MyClass : public Tools::ExtensionSupport
{
};

class MyExtension : public Tools::Extension
{
public:
	enum ETypeid : std::uint64_t {
		value = Tools::GenHash("MyExtension")
	};
};

class BadExt : public Tools::Extension
{
public:
	enum ETypeid : std::uint64_t {
		value = Tools::GenHash("BadExt")
	};
};

TEST_CASE("Tools:Extension") 
{
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
