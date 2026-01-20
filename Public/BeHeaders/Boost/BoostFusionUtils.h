/*--------------------------------------------------------------------------------------+
|
|     $Source: BoostFusionUtils.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

// from vue.git/Vue/Source/eonLib/...

#pragma once

#include <boost/operators.hpp>
#include <BeHeaders/Compil/EmptyBases.h>
#include <ios>

namespace BoostFusionUtils
{

//! Returns whether the given sequence are equal, by comparing each member.
template<class _Sequence>
bool AreSequencesEqual(const _Sequence& x, const _Sequence& y);
//! Returns a hash value for the given sequence, taking into account each member.
template<class _Sequence>
std::size_t GetSequenceHashValue(const _Sequence& x);
//! Writes each member of the given sequence to the stream.
template<class _Sequence>
std::ostream& WriteToStdStream(std::ostream& os, const _Sequence& x);

//! Can be used as subclass of a sequence, to provide additional features:
//! comparison (equality), write to stream etc.
//! Note: BE_EMPTY_BASES is not just an optimization, it actually fixes a crash in IModelUsdNodeAddon,
//! apparently due to the compiler generating classes with inconsistent size.
template<class _Base>
class BE_EMPTY_BASES SequenceEx
	:public _Base
	,boost::equality_comparable<SequenceEx<_Base>>
{
public:
	using This = SequenceEx<_Base>;
	using Super = _Base;
	using Super::Super;
};

template <class _Base>
bool operator ==(const SequenceEx<_Base>& x, const SequenceEx<_Base>& y);
//! Compares members in order ("lexicographic" compare).
template <class _Base>
bool operator <(const SequenceEx<_Base>& x, const SequenceEx<_Base>& y);
template <class _Base>
std::ostream& operator <<(std::ostream& os, const SequenceEx<_Base>& x);
template<class _Base>
std::size_t hash_value(const SequenceEx<_Base>& v) noexcept;

} // namespace BoostFusionUtils

#include "BoostFusionUtils.inl"
