/*--------------------------------------------------------------------------------------+
|
|     $Source: JsonTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "Json.h"

#include <variant>

#include <rfl/json.hpp> //note: we should not include this but compiler error VS 2022 17.9.2: static function 'void yyjson_mut_doc_set_root(yyjson_mut_doc *,yyjson_mut_val *)' declared but not defined
#include <rfl.hpp>

#include <catch2/catch_all.hpp>
struct Person {
	std::string first_name;
	std::string last_name;
	int age;
};

const auto homer =
Person{ .first_name = "Homer",
	   .last_name = "Simpson",
	   .age = 45 };

TEST_CASE("JsonTest:json_tostring") {
	const std::string r = AdvViz::SDK::Json::ToString(homer);
	REQUIRE(r == "{\"first_name\":\"Homer\",\"last_name\":\"Simpson\",\"age\":45}");
}

TEST_CASE("JsonTest:json_fromstring") {
	const std::string r = AdvViz::SDK::Json::ToString(homer);
	const std::string json_string("{\"first_name\":\"Homer\",\"last_name\":\"Simpson\",\"age\":45}");
	Person homer_fromstring;
	AdvViz::SDK::Json::FromString(homer_fromstring, json_string);
	REQUIRE(homer_fromstring.first_name == "Homer");
	REQUIRE(homer_fromstring.last_name == "Simpson");
	REQUIRE(homer_fromstring.age == 45);
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

TEST_CASE(JsonTest, json_fromstring3) {
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
	auto generic = AdvViz::SDK::Json::FromString<MyClass2>(json_string);
	//const std::string r = AdvViz::SDK::Json::ToString(homer);
	//const std::string json_string("{\"first_name\":\"Homer\",\"last_name\":\"Simpson\",\"age\":45}");
	//auto homer = AdvViz::SDK::Json::FromString<Person>(json_string);
	//REQUIRE(homer.first_name, "Homer");
	//REQUIRE(homer.last_name, "Simpson");
	//REQUIRE(homer.age, 45);
}

//typedef std::map<std::string, std::variant<Value::v, std::vector<Value::v>, std::map<std::string, Value::v> >> GenericJSon;

class Generic {
public:
	using ReflectionType = std::variant<bool, int, double, std::string,
		std::vector<Generic>,
		std::unordered_map<std::string, Generic>>;

	Generic(const ReflectionType& _value) : value_(_value) {}
	Generic() {}
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
	GenericT(){}
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
	Generic2T(){}
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

TEST_CASE(JsonTest, json_fromstring2) {
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

	//auto generic = AdvViz::SDK::Json::FromString<GenericJSon>(json_string);
	Generic generic;
	AdvViz::SDK::Json::FromString(generic, json_string);
	std::string s = AdvViz::SDK::Json::ToString(generic);

	Generic2 generic2;
	AdvViz::SDK::Json::FromString(generic2, json_string);
	//const std::string r = AdvViz::SDK::Json::ToString(homer);
	//const std::string json_string("{\"first_name\":\"Homer\",\"last_name\":\"Simpson\",\"age\":45}");
	//auto homer = AdvViz::SDK::Json::FromString<Person>(json_string);
	//REQUIRE(homer.first_name, "Homer");
	//REQUIRE(homer.last_name, "Simpson");
	//REQUIRE(homer.age, 45);
}
#endif
