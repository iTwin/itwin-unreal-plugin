/*--------------------------------------------------------------------------------------+
|
|     $Source: BoostFusionUtils.inl $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

// from vue.git/Vue/Source/eonLib/...

#pragma once

#include "BoostFusionUtils.h"

#include <boost/container_hash/hash.hpp>
#include <boost/fusion/include/all.hpp>
#include <boost/fusion/include/for_each.hpp>
#include <boost/fusion/include/zip.hpp>

namespace BoostFusionUtils
{

template<class _Sequence>
bool AreSequencesEqual(const _Sequence& x, const _Sequence& y)
{
	return boost::fusion::all(boost::fusion::zip(x, y),
		[](const auto& memberPair){return boost::fusion::at_c<0>(memberPair) == boost::fusion::at_c<1>(memberPair);});
}

template<class _Sequence>
std::size_t GetSequenceHashValue(const _Sequence& x)
{
	std::size_t h = 0;
	boost::fusion::for_each(x,
		[&](const auto& member){boost::hash_combine(h, member);});
	return h;
}

template<class _Sequence>
std::ostream& WriteToStdStream(std::ostream& os, const _Sequence& x)
{
	os << "{";
	bool isFirst = true;
	boost::fusion::for_each(x, [&](const auto& member)
		{
			if (!isFirst)
				os << ", ";
			else
				isFirst = false;
			os << member;
		});
	os << "}";
	return os;
}

template <class _Base>
bool operator ==(const SequenceEx<_Base>& x, const SequenceEx<_Base>& y)
{
	return AreSequencesEqual(x, y);
}

template <class _Base>
bool operator <(const SequenceEx<_Base>& x, const SequenceEx<_Base>& y)
{
	int comparisonResult = 0; // -1 if x < y, +1 if x > y, 0 if equal so far
	boost::fusion::for_each(boost::fusion::zip(x, y), [&](const auto& memberPair)
		{
			if (comparisonResult == 0)
			{
				if (boost::fusion::at_c<0>(memberPair) < boost::fusion::at_c<1>(memberPair))
					comparisonResult = -1;
				if (boost::fusion::at_c<1>(memberPair) < boost::fusion::at_c<0>(memberPair))
					comparisonResult = 1;
			}
		});
	return comparisonResult < 0;
}

template <class _Base>
std::ostream& operator <<(std::ostream& os, const SequenceEx<_Base>& x)
{
	return WriteToStdStream(os, x);
}

template<class _Base>
std::size_t hash_value(const SequenceEx<_Base>& v) noexcept
{
	return GetSequenceHashValue(v);
}

} // namespace BoostFusionUtils
