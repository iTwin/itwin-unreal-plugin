/*--------------------------------------------------------------------------------------+
|
|     $Source: StrongTypeId.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once
#include <array>

namespace AdvViz::SDK
{

	template<typename T>
	class StrongTypeId
	{
	public:
		StrongTypeId() {}
		StrongTypeId(const StrongTypeId<T>&) = default;
		StrongTypeId(StrongTypeId<T>&&) = default;
		StrongTypeId& operator=(const StrongTypeId<T>&) = default;

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

		void Reset() { value_.clear(); }

		bool operator==(const std::string& v) const
		{
			return value_ == v;
		}

		bool operator==(const StrongTypeId<T>& v) const
		{
			return value_ == v.value_;
		}

		bool operator<(const StrongTypeId<T>& v) const
		{
			return value_ < v.value_;
		}

	private:
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

template<typename T>
struct std::hash<AdvViz::SDK::StrongTypeId<T>>
{
	std::size_t operator()(const AdvViz::SDK::StrongTypeId<T>& s) const noexcept
	{
		std::hash<std::string> h;
		return h((std::string&)s);
	}
};
