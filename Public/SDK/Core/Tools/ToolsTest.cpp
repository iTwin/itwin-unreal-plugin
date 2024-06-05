/*--------------------------------------------------------------------------------------+
|
|     $Source: ToolsTest.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <gtest/gtest.h>

import SDK.Core.Tools;

using namespace SDK::Core;

class MyClass : public Tools::ExtensionSupport
{
};

class MyExtension : public Tools::Extension
{
};

TEST(Tools, Extension) {
	MyClass myclass;
	Tools::ExtensionId MyGoodId{ 10 };
	Tools::ExtensionId MyBadId{ 20 };

	std::shared_ptr<Tools::Extension> ext(new MyExtension);
	myclass.AddExtension(MyGoodId, ext);
	EXPECT_EQ(myclass.HasExtension(MyGoodId), true);
	EXPECT_EQ(myclass.HasExtension(MyBadId), false);
	EXPECT_EQ(myclass.GetExtension(MyGoodId).get(), ext.get());
	EXPECT_EQ(myclass.GetExtension(MyBadId).get(), nullptr);
}