/*--------------------------------------------------------------------------------------+
|
|     $Source: OptionsClass.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

/// See example in AmodalBrowserProxy.h or Menu.h.
#define OPTIONS_CLASS_START(OptionsClass, LinkType) \
	class LinkType OptionsClass { \
		using MyClass = OptionsClass;

#define OPTIONS_CLASS_ADD_MEMBER_PRIVATE(GetterConstness, Type, MyOption, EqualsDefVal) \
	private: \
		Type m_ ## MyOption EqualsDefVal; \
	public: \
		Type GetterConstness& MyOption() GetterConstness { return m_ ## MyOption; } \
		MyClass& MyOption(Type const& myOptVal) { m_ ## MyOption = myOptVal; return *this; }

/// Use OPTIONS_CLASS_ADD_MEMBER_NO_DEFAULT for a member with no default value,
/// OPTIONS_CLASS_ADD_MUTABLE_MEMBER for a member for which you want a mutable getter
#define OPTIONS_CLASS_ADD_MEMBER(Type, MyOption, DefVal) \
	OPTIONS_CLASS_ADD_MEMBER_PRIVATE(const, Type, MyOption, = DefVal)
#define OPTIONS_CLASS_ADD_MUTABLE_MEMBER(Type, MyOption, DefVal) \
	OPTIONS_CLASS_ADD_MEMBER_PRIVATE(/*nil*/, Type, MyOption, = DefVal)

/// When you use this macro at least once, you MUST define your own constructor (implicitly deleting
/// the default one) to initialize "m_MyOption"!
#define OPTIONS_CLASS_ADD_MEMBER_NO_DEFAULT(Type, MyOption) \
	OPTIONS_CLASS_ADD_MEMBER_PRIVATE(const, Type, MyOption, )
#define OPTIONS_CLASS_ADD_MUTABLE_MEMBER_NO_DEFAULT(Type, MyOption) \
	OPTIONS_CLASS_ADD_MEMBER_PRIVATE(/*nil*/, Type, MyOption, )

#define OPTIONS_CLASS_END \
	};
