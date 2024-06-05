/*--------------------------------------------------------------------------------------+
|
|     $Source: JsonTest.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

import SDK.Core.Json;

#include <variant>

#include <rfl/json.hpp> //note: we should not include this but compiler error VS 2022 17.9.2: static function 'void yyjson_mut_doc_set_root(yyjson_mut_doc *,yyjson_mut_val *)' declared but not defined
#include <rfl.hpp>

#include <gtest/gtest.h>
struct Person {
	std::string first_name;
	std::string last_name;
	int age;
};

const auto homer =
Person{ .first_name = "Homer",
	   .last_name = "Simpson",
	   .age = 45 };

TEST(JsonTest, json_tostring) {
	const std::string r = SDK::Core::Json::ToString(homer);
	EXPECT_EQ(r, "{\"first_name\":\"Homer\",\"last_name\":\"Simpson\",\"age\":45}");
}

TEST(JsonTest, json_fromstring) {
	const std::string r = SDK::Core::Json::ToString(homer);
	const std::string json_string("{\"first_name\":\"Homer\",\"last_name\":\"Simpson\",\"age\":45}");
	auto homer = SDK::Core::Json::FromString<Person>(json_string);
	EXPECT_EQ(homer.first_name, "Homer");
	EXPECT_EQ(homer.last_name, "Simpson");
	EXPECT_EQ(homer.age, 45);
}

#if 0
typedef std::vector<std::pair<std::string, int>>  MyClass2;

class MyClass : public std::pair<std::string, int>
{
public:
	//std::pair<std::string, std::variant<bool, double, std::string>> v;
	rfl::Flatten<std::unique_ptr<MyClass>> v1;
	//int v;
	//std::map<std::string, int> v;
};

TEST(JsonTest, json_fromstring3) {
//	std::string json_string(
//		"\
//	{\
//		\"v\":0 \
//		,\"v1\":{ \
//			\"v\":1 \
//			,\"v1\":{ \"v\":2 } \
//			}\
//	}\
//"
//);
	std::string json_string(
		"\
	{\
		\"v0\":0 \
		,\"v1\":1 \
		,\"v2\":2 \
	}\
"
);
	auto generic = SDK::Core::Json::FromString<MyClass2>(json_string);
	//const std::string r = SDK::Core::Json::ToString(homer);
	//const std::string json_string("{\"first_name\":\"Homer\",\"last_name\":\"Simpson\",\"age\":45}");
	//auto homer = SDK::Core::Json::FromString<Person>(json_string);
	//EXPECT_EQ(homer.first_name, "Homer");
	//EXPECT_EQ(homer.last_name, "Simpson");
	//EXPECT_EQ(homer.age, 45);
}

//typedef std::map<std::string, std::variant<Value::v, std::vector<Value::v>, std::map<std::string, Value::v> >> GenericJSon;
#endif

class Generic {
public:
	using ReflectionType = std::variant<bool, int, double, std::string,
		std::vector<Generic>,
		std::unordered_map<std::string, Generic>>;

	Generic(const ReflectionType& _value) : value_(_value) {}

	~Generic() = default;

	ReflectionType reflection() const { return value_; };

private:
	ReflectionType value_;
};

template<typename StringType, typename... Types>
class GenericT {
public:
	//typedef GenericT<Types...> ThisType;
	using ReflectionType = std::variant<Types...,
							std::vector<GenericT<StringType, Types...>>,
							std::unordered_map<StringType, GenericT<StringType, Types...>>>;

	GenericT(const ReflectionType& _value) : value_(_value) {}

	~GenericT() = default;

	ReflectionType reflection() const { return value_; };

private:
	ReflectionType value_;
};

using Generic2 = GenericT<std::string,
						  bool, std::uint64_t, double, std::string>;


template<template<typename T> typename BaseType>
class Generic2T {
public:
	typedef GenericT<BaseType> ThisType;
	using ReflectionType = BaseType<ThisType>;

	Generic2T(const ReflectionType& _value) : value_(_value) {}

	~Generic2T() = default;

	ReflectionType reflection() const { return value_; };

private:
	ReflectionType value_;
};

template<typename RecursiveType>
using VarT = std::variant<bool, int, double, std::string,
	std::vector<RecursiveType>,
	std::unordered_map<std::string, RecursiveType>>;

using Generic3 = Generic2T<VarT>;




struct Value: public std::variant<bool, double, std::string> {};
struct GenericJSon : public std::map<std::string, std::variant<Value, std::vector<Value>, GenericJSon>> {};

//typedef std::variant<bool, double, std::string> Value;
//typedef std::variant<bool, double, std::string, std::vector<Value>, std::map<std::string, Value> > Value2;
//typedef std::map<std::string, std::variant<std::map<std::string, Value>, std::vector<std::map<std::string, Value>>, std::vector<Value2>, Value2>> GenericJSon;

TEST(JsonTest, json_fromstring2) {
	std::string json_string(
"\
	{\
		\"toto\":\"titi\" \
		, \"float\":2.0 \
		, \"int\":2 \
		, \"intV\":[1,2,3]\
		, \"floatV\": [1.1, 2.2, 3.3] \
		, \"sturct\": {\"a\":1.1, \"b\":2.2} \
		, \"sturctV\": [{\"a\":1.1, \"b\":2.2}, {\"c\":true, \"d\":\"tutu\"}] \
		, \"sturctNestedV\": [{\"a\":1.1, \"b\":[{\"a\":1.1}, {\"b\":1.1}]}, {\"c\":true, \"d\":\"tutu\"}] \
	}\
"
		);

	//auto generic = SDK::Core::Json::FromString<GenericJSon>(json_string);
	auto generic = SDK::Core::Json::FromString<Generic>(json_string);
	std::string s = SDK::Core::Json::ToString(generic);

	auto generic2 = SDK::Core::Json::FromString<Generic2>(json_string);
	//const std::string r = SDK::Core::Json::ToString(homer);
	//const std::string json_string("{\"first_name\":\"Homer\",\"last_name\":\"Simpson\",\"age\":45}");
	//auto homer = SDK::Core::Json::FromString<Person>(json_string);
	//EXPECT_EQ(homer.first_name, "Homer");
	//EXPECT_EQ(homer.last_name, "Simpson");
	//EXPECT_EQ(homer.age, 45);
}
