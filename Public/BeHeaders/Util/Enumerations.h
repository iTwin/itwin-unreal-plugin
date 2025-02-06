/*--------------------------------------------------------------------------------------+
|
|     $Source: Enumerations.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

// from vue.git/Vue/Source/ToolsLib/enumerations.h
// Note: PYTHON_WRAPPER stuff removed

#pragma once

#include <BeHeaders/Compil/Attributes.h>
#include <BeHeaders/Compil/AutoOperators.h>
#include <BeHeaders/Compil/EnumSwitchCoverage.h>

#include <string_view>
namespace tools
{
	using string_view = std::string_view;
	using wstring_view = std::wstring_view;
}
#include <boost/container_hash/hash.hpp>
#include <boost/preprocessor/seq/enum.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/seq/size.hpp>
#include <boost/preprocessor/tuple/to_seq.hpp>

/**
The following macros allow to construct class-like enumerations.<br>

The base macro functions ENUM_CONSTRUCT_CASE, ENUM_CONSTRUCT_HEAD,
ENUM_CONSTRUCT_TRANSITION, and ENUM_CONSTRUCT_TAIL are needed to create generic
enumerations of any number of components.<br>

Usage example:<br>
CONSTRUCT_ENUMERATION(Direction,(Up,Down,Left,Right))<br>
CONSTRUCT_ENUMERATION is limited by Boost preprocessor's max tuple size (64 in 1.62).
If your enumeration is larger, you can use CONSTRUCT_ENUMERATION_SEQ (limited
to 256 in boost 1.62):
CONSTRUCT_ENUMERATION_SEQ(Direction,(Up)(Down)(Left)(Right))

will construct an enumeration class name 'Boolean' at the place of invocation.<br>
This example enum class will provide Boolean::True and Boolean::False,
and Boolean::NumberOfPossibilities of 2.<br>
The first value in the list of choices is used as default value, which is
 Boolean::True in this case.<br>
Although the provided constants are constructed as a part of enum (*)::EConst,
 any instance of the respective enum class (*) can and should carry the value itself.<br>
In the above example write<br>
<code>Boolean b = Boolean::True</code><br>
as opposed to<br>
<code>Boolean::EConst b = Boolean::True</code><br>

Enumeration class instances are fully comparable via <=, >=, <, >, !=, and ==.<br>
Additionally, enum classes provide the following methods:<br>
	ToWString() can be used to convert an enumeration value to a const char* string:<br>
	<code>
		Boolean b;
		b.ToWString();			//returns "Boolean::True"
		b = Boolean::False;
		Boolean::ToWString(b);	//identical to b.ToWString(): now returns "Boolean::False"
		b.ToWString(false);		//returns "False", without any preceeding class name
	</code><br>
	Conversely, enumeration values can be decoded from strings using the case-sensitive
	 Decode() method.
	Decode() checks both with and without preceeding class name. Consequentially,
	both "Boolean::True" and "True" are valid for enumeration class 'Boolean',
	 "true" however is not.
	The method returns true on success.<br>
	<br>
	<code>
		Boolean b;
		if (b.Decode("False"))
		{
			//string decoding succeeded
		}
		else
		{
			//string decoding failed - in our case this should not happen
		}
	</code><br>
	IsValid() determines the validity of a loaded value:<br>
	<code>
		Boolean b;
		b.IsValid();			//returns true
		b = (Boolean::EConst)-1;	//you shouldn't be doing this anyway, use b.Load(...) instead
		Boolean::IsValid(b);	//identical to b.IsValid(): now returns false
	</code><br>

	Load(...) can be used to load values from native types (e.g. an int).
	The Load() method will return true if the specified type can be cast into a
	valid constant value. In that case it will return true.
	Otherwise the current local value will remain unchanged, and false returned:<br>
	<code>
		Boolean b;
		b.Load(0);	//Will load Boolean::True and return true
		b.Load(1);	//Will load Boolean::False and return true
		b.Load(2);	//Will not load anything and return false
	</code><br>
	The static Reinterpret(...) method can be used to load values directly into
	new instances of your enumeration:<br>
	<code>
		Boolean b = Boolean::Reinterpret(1);
		if (Boolean::Reinterpret(0) == Boolean::True)
		{
			...
		}
	</code>

*/

#undef ENUM_CONSTRUCT_CASE
#define ENUM_CONSTRUCT_CASE(ENUM_NAME,ENUM_VAL)\
	case ENUM_VAL:\
		return includeEnumName\
			? #ENUM_NAME "::" #ENUM_VAL\
			: #ENUM_VAL;

// wide character version
#undef ENUM_CONSTRUCT_CASE_W
#define ENUM_CONSTRUCT_CASE_W(ENUM_NAME,ENUM_VAL)\
	case ENUM_VAL:\
		return includeEnumName\
			? L ## #ENUM_NAME L"::" L ## #ENUM_VAL\
			: L ## #ENUM_VAL;

#undef ENUM_CONSTRUCT_HEAD
#define ENUM_CONSTRUCT_HEAD(NAME)\
	class NAME\
	{\
	public:\
		struct EonEnumTag{};\
		enum EConst\
		{

#undef ENUM_CONSTRUCT_TRANSITION
#define ENUM_CONSTRUCT_TRANSITION(NAME, CONST_COUNT)\
		};\
		static const size_t NumberOfPossibilities = CONST_COUNT;\
		static const char* GetClassName() {return #NAME;}\
		static const char* ToString(EConst type, bool includeEnumName=true)\
		{\
			switch (type)\
			{


#undef ENUM_CONSTRUCT_PART2
#define ENUM_CONSTRUCT_PART2()\
				BE_NO_UNCOVERED_ENUM_ASSERT_AND_RETURN("Invalid enumeration value"); \
			}\
		}\
		static const wchar_t* ToWString(EConst type, bool includeEnumName=true)\
		{\
			switch (type)\
			{

#ifdef USE_BE_ASSERT
#define ENUM_REINTERPRET(NAME)\
	static NAME Reinterpret(const T&val)\
	{\
		NAME rs;\
		BE_ASSERT(rs.Load(val));\
		return rs;\
	}
#else
#define ENUM_REINTERPRET(NAME)\
	static NAME Reinterpret(const T&val)\
	{\
		NAME rs;\
		rs.Load(val);\
		return rs;\
	}
#endif

#undef ENUM_CONSTRUCT_TAIL
#define ENUM_CONSTRUCT_TAIL(NAME, DEFAULT)\
				BE_NO_UNCOVERED_ENUM_ASSERT_AND_RETURN(L"Invalid enumeration value"); \
			}\
		}\
		static const EConst Default = DEFAULT;\
		NAME():value_(DEFAULT) {}\
		NAME(EConst val):value_(val) {}\
		NAME(const NAME&val) = default;\
		NAME(NAME&&val) = default;\
		template <typename T>\
		bool Load(const T&val)\
		{\
			if (IsValid((EConst)val))\
			{\
				value_ = (EConst)val;\
				return true;\
			}\
			return false;\
		}\
		\
		template <typename T>\
		ENUM_REINTERPRET(NAME)\
		\
		bool Decode(tools::string_view const& str)\
		{\
			for (size_t i = 0; i < NumberOfPossibilities; i++)\
				if (str == ToString((EConst)i) || str == ToString((EConst)i,false))\
				{\
					value_ = (EConst)i;\
					return true;\
				}\
			return false;\
		}\
		static const char* ToString(NAME const& val, bool includeEnumName = true)\
		{\
			return ToString(val.value_, includeEnumName);\
		}\
		const char* ToString(bool includeEnumName = true) const\
		{\
			return ToString(value_, includeEnumName);\
		}\
		\
		bool Decode(tools::wstring_view const& str)\
		{\
			for (size_t i = 0; i < NumberOfPossibilities; i++)\
				if (str == ToWString((EConst)i) || str == ToWString((EConst)i,false))\
				{\
					value_ = (EConst)i;\
					return true;\
				}\
			return false;\
		}\
		static const wchar_t* ToWString(const NAME& val, bool includeEnumName = true)\
		{\
			return ToWString(val.value_, includeEnumName);\
		}\
		const wchar_t* ToWString(bool includeEnumName = true) const\
		{\
			return ToWString(value_, includeEnumName);\
		}\
		static bool IsValid(const NAME& val)\
		{\
			return unsigned(val.value_)<unsigned(NumberOfPossibilities);\
		}\
		static bool IsValid(EConst val)\
		{\
			return unsigned(val)<unsigned(NumberOfPossibilities);\
		}\
		bool IsValid() const\
		{\
			return unsigned(value_)<unsigned(NumberOfPossibilities);\
		}\
		NAME& operator=(EConst val)\
		{\
			value_ = val;\
			return *this;\
		}\
		NAME& operator=(const NAME& val) = default;\
		BE_AUTOOPERATORS_THREEWAY(NAME, first.value_ - second.value_);\
		BE_AUTOOPERATORS_THREEWAY_WITHOTHERTYPE(NAME, EConst, first.value_ - second);\
		operator EConst() const\
		{\
			return value_;\
		}\
		BE_MAYBE_UNUSED [[nodiscard]] friend std::size_t hash_value(const NAME& x)\
		{\
			return boost::hash_value(x.value_);\
		}\
	private:\
		EConst value_ = Default;\
	};

#define BE_CONSTRUCT_ENUMERATION_BOOST_PP_MACRO(r, name, elem) ENUM_CONSTRUCT_CASE(name, elem)
#define BE_CONSTRUCT_ENUMERATION_BOOST_PP_MACRO_W(r, name, elem) ENUM_CONSTRUCT_CASE_W(name, elem)
#define CONSTRUCT_ENUMERATION_SEQ(name, list)										\
	ENUM_CONSTRUCT_HEAD(name)														\
	BOOST_PP_SEQ_ENUM(list)															\
	ENUM_CONSTRUCT_TRANSITION(name, BOOST_PP_SEQ_SIZE(list))						\
	BOOST_PP_SEQ_FOR_EACH(BE_CONSTRUCT_ENUMERATION_BOOST_PP_MACRO, name, list)		\
	ENUM_CONSTRUCT_PART2()															\
	BOOST_PP_SEQ_FOR_EACH(BE_CONSTRUCT_ENUMERATION_BOOST_PP_MACRO_W, name, list)	\
	ENUM_CONSTRUCT_TAIL(name, BOOST_PP_SEQ_ELEM(0, list))
#define CONSTRUCT_ENUMERATION(name, list) CONSTRUCT_ENUMERATION_SEQ(name, BOOST_PP_TUPLE_TO_SEQ(list))


//=============================================================================
//						CONSTRUCT_ENUM_CLASS
//
// variant based on enum class
// (simpler and compatible with PythonWrapper - see below)
// this will progressively replace the CONSTRUCT_ENUMERATION macro
//=============================================================================

#undef ENUM_CLASS_CONSTRUCT_CASE
#define ENUM_CLASS_CONSTRUCT_CASE(ENUM_NAME, ENUM_VAL)\
	case ENUM_NAME::ENUM_VAL:\
		return includeEnumName\
			? #ENUM_NAME "::" #ENUM_VAL\
			: #ENUM_VAL;

// wide character version
#undef ENUM_CLASS_CONSTRUCT_CASE_W
#define ENUM_CLASS_CONSTRUCT_CASE_W(ENUM_NAME, ENUM_VAL)\
	case ENUM_NAME::ENUM_VAL:\
		return includeEnumName\
			? L ## #ENUM_NAME L"::" L ## #ENUM_VAL\
			: L ## #ENUM_VAL;

#undef ENUM_CLASS_CONSTRUCT_HEAD
#define ENUM_CLASS_CONSTRUCT_HEAD(NAME, LIST)\
	enum class NAME\
	{\
		BOOST_PP_SEQ_ENUM(LIST)\
	};

#undef ENUM_CONSTRUCT_HELPER_HEAD
#define ENUM_CONSTRUCT_HELPER_HEAD(NAME, CONST_COUNT)\
	class NAME ## _EnumHelper\
	{\
	public:\
		static constexpr size_t	NumberOfPossibilities = CONST_COUNT;\
		static const char* GetClassName() { return #NAME; }\
		\
		static const char* ToString(NAME type, bool includeEnumName = true)\
		{\
			switch (type)\
			{

#undef ENUM_CONSTRUCT_HELPER_PART2
#define ENUM_CONSTRUCT_HELPER_PART2(NAME)\
				BE_NO_UNCOVERED_ENUM_ASSERT_AND_RETURN("Invalid enumeration value");\
			}\
		}\
		\
		static const wchar_t* ToWString(NAME type, bool includeEnumName = true)\
		{\
			switch (type)\
			{

#undef ENUM_CONSTRUCT_HELPER_TAIL
#define ENUM_CONSTRUCT_HELPER_TAIL(NAME)\
				BE_NO_UNCOVERED_ENUM_ASSERT_AND_RETURN(L"Invalid enumeration value"); \
			}\
		}\
		\
		template <typename T>\
		static std::optional<NAME> Reinterpret(T const&val)\
		{\
			if (unsigned(val) < unsigned(NumberOfPossibilities))\
			{\
				return static_cast<NAME>(val);\
			}\
			return std::nullopt;\
		}\
		\
		static std::optional<NAME> Decode(tools::string_view const& str)\
		{\
			for (size_t i = 0; i < NumberOfPossibilities; i++)\
				if (str == ToString((NAME)i) || str == ToString((NAME)i, false))\
				{\
					return static_cast<NAME>(i);\
				}\
			return std::nullopt;\
		}\
		\
		static std::optional<NAME> Decode(tools::wstring_view const& str)\
		{\
			for (size_t i = 0; i < NumberOfPossibilities; i++)\
				if (str == ToWString((NAME)i) || str == ToWString((NAME)i, false))\
				{\
					return static_cast<NAME>(i);\
				}\
			return std::nullopt;\
		}\
	};

#define BE_CONSTRUCT_ENUM_CLASS_BOOST_PP_MACRO(r, name, elem)\
	ENUM_CLASS_CONSTRUCT_CASE(name, elem)

#define BE_CONSTRUCT_ENUM_CLASS_BOOST_PP_MACRO_W(r, name, elem)\
	ENUM_CLASS_CONSTRUCT_CASE_W(name, elem)

#define CONSTRUCT_ENUM_CLASS_SEQ(name, list)\
	ENUM_CLASS_CONSTRUCT_HEAD(name, list)\
	ENUM_CONSTRUCT_HELPER_HEAD(name, BOOST_PP_SEQ_SIZE(list))\
	BOOST_PP_SEQ_FOR_EACH(BE_CONSTRUCT_ENUM_CLASS_BOOST_PP_MACRO, name, list)\
	ENUM_CONSTRUCT_HELPER_PART2(name)\
	BOOST_PP_SEQ_FOR_EACH(BE_CONSTRUCT_ENUM_CLASS_BOOST_PP_MACRO_W, name, list)\
	ENUM_CONSTRUCT_HELPER_TAIL(name)
#define CONSTRUCT_ENUM_CLASS(name, list) CONSTRUCT_ENUM_CLASS_SEQ(name, BOOST_PP_TUPLE_TO_SEQ(list))
