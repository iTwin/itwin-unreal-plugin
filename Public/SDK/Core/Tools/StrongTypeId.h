/*--------------------------------------------------------------------------------------+
|
|     $Source: StrongTypeId.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once
#include <array>

namespace SDK::Core
{

	template<typename T>
	class StrongTypeId
	{
	public:
		StrongTypeId() {}
		StrongTypeId(const StrongTypeId&) = default;
		StrongTypeId(StrongTypeId&&) = default;
		StrongTypeId& operator=(const StrongTypeId&) = default;

		explicit StrongTypeId(const std::string& s) :value_(s)
		{}

		explicit operator std::string& ()
		{
			return value_;
		}

		explicit operator std::string()
		{
			return value_;
		}

		explicit operator std::string const() const
		{
			return value_;
		}

		bool IsValid() const
		{
			return !value_.empty();
		}

		std::string value_;
	};

	template<typename T>
	class WithStrongTypeId
	{
	public:
		typedef StrongTypeId<T> Id;
		virtual const Id& GetId() const = 0;
	};

}