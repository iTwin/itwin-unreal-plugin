/*--------------------------------------------------------------------------------------+
|
|     $Source: TaggedVector.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <array>
#include "TaggedValue.h"
#include <vector>

namespace Be
{
	/** std::vector or std::array accessed by a StrongValue index,
	 * so that you don't mix index by error.
	 *
	 * Complete API as needed, with respect to the one of std::vector
	 *
	 * INDEX is supposed to be a StrongValue,
	 * but could be anything which has a value member
	 */
	template< class CONT, class INDEX >
	struct TaggedCont
	{
		using value_type = typename CONT::value_type;
		using DataType = value_type;
		using Index = INDEX;

		TaggedCont() {}
		explicit TaggedCont(Index const& nb) : cont_(nb.value()) {}
		TaggedCont(Index const& nb, DataType const& val) : cont_(nb.value(), val) {}

		void reserve(Index const& nb)
		{
			cont_.reserve(nb.value());
		}

		void resize(Index const& nb)
		{
			cont_.resize(nb.value());
		}

		void resize(Index const& nb , DataType value)
		{
			cont_.resize(nb.value() , value);
		}

		void swap(TaggedCont& other)
		{
			cont_.swap(other.cont_);
		}

		[[nodiscard]] DataType & operator[](Index const& index)
		{
			BE_ASSERT(index < size());
			return cont_[index.value()];
		}

		[[nodiscard]] DataType const& operator[](Index const& index) const
		{
			BE_ASSERT(index < size());
			return cont_[index.value()];
		}

		[[nodiscard]] DataType & at(Index const& index)
		{
			BE_ASSERT(index < size());
			return cont_.at(index.value());
		}

		[[nodiscard]] DataType const& at(Index const& index) const
		{
			BE_ASSERT(index < size());
			return cont_.at(index.value());
		}

		[[nodiscard]] Index size() const
		{
			return Index(static_cast<typename INDEX::ValueType>(cont_.size()));
		}

		[[nodiscard]] bool empty() const
		{
			return cont_.empty();
		}

		void clear()
		{
			cont_.clear();
		}

		void push_back(DataType const& data)
		{
			cont_.push_back(data);
		}

		[[nodiscard]] DataType const& front() const
		{
			return cont_.front();
		}

		[[nodiscard]] DataType const& back() const
		{
			return cont_.back();
		}

		// This was not ported from original repo (blame here for more info):
		//template<class V> void VisitDataVersion(V & v, VisitVersion version) const

		typedef typename CONT::iterator iterator ;
		typedef typename CONT::const_iterator const_iterator ;

		iterator erase(iterator at) { return cont_.erase(at); }

		iterator begin() { return cont_.begin(); }
		iterator end() { return cont_.end(); }

		[[nodiscard]] const_iterator begin() const { return cont_.begin(); }
		[[nodiscard]] const_iterator end() const { return cont_.end(); }

		[[nodiscard]] bool operator==(TaggedCont const& o) const { return cont_ == o.cont_; }
		[[nodiscard]] bool operator!=(TaggedCont const& o) const { return cont_ != o.cont_; }

		void insert(iterator at, DataType const& value)
		{
			cont_.insert(at, value);
		}

	private:
		CONT cont_;
	};

	template< class T, class INDEX >
	using TaggedVector = TaggedCont< std::vector<T>, INDEX >;

	template< class T, size_t Size, class INDEX >
	using TaggedArray = TaggedCont< std::array<T, Size>, INDEX >;

} // ns Be
