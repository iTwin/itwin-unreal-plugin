/*--------------------------------------------------------------------------------------+
|
|     $Source: Definition.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <boost/preprocessor/cat.hpp>
	#include <boost/preprocessor/seq/transform.hpp>
	#include <boost/preprocessor/seq/variadic_seq_to_seq.hpp>
	#include <boost/preprocessor/stringize.hpp>
	#include <boost/fusion/include/define_struct_inline.hpp>
	#include <BeHeaders/Boost/BoostFusionUtils.h>
#include <Compil/AfterNonUnrealIncludes.h>

#include "TimelineBase.h"

/// Hack: needs to be in the same namespace as its specialization(s) :/
namespace ITwin::Timeline { template<typename PropertyClass> FString _iTwinTimelineGetPropertyName(); }
//! Example: the following call:
//! 
//! ITWIN_TIMELINE_DEFINE_PROPERTY_VALUES(Color,
//!     (bool, hasColor_, ITwin::Timeline::Interpolators::BoolOr)
//!     (CLR, value_)
//! )
//! 
//! will generate code similar to this:
//! 
//! struct ColorBase // Actually a boost::fusion::sequence
//! {
//!     bool hasColor_;
//!     CLR value_;
//! };
//! using Color = BoostFusionUtils::SequenceEx<ColorBase>
//! struct ColorInterpolators // Actually a boost::fusion::sequence
//! {
//!     ITwin::Timeline::Interpolators::BoolOr hasColor_;
//!     ITwin::Timeline::Interpolators::Default value_;
//! };
//! inline FString _iTwinTimelineGetPropertyName<Color>() { return "Color"; }
//! ColorInterpolators _iTwinTimelineGetInterpolators(Color); // Not implemented, used with decltype().
//!
#define ITWIN_TIMELINE_DEFINE_PROPERTY_VALUES(propertyName, values)			\
	BOOST_FUSION_DEFINE_STRUCT_INLINE(										\
		BOOST_PP_CAT(propertyName, Base),									\
		BOOST_PP_SEQ_TRANSFORM(												\
			_ITWIN_TIMELINE_DETAIL_DEFINE_PROPERTY_VALUES_IMPL_1, ,			\
			BOOST_PP_VARIADIC_SEQ_TO_SEQ(values)))							\
	using propertyName =													\
		::BoostFusionUtils::SequenceEx<BOOST_PP_CAT(propertyName, Base)>;	\
	BOOST_FUSION_DEFINE_STRUCT_INLINE(										\
		BOOST_PP_CAT(propertyName, Interpolators),							\
		BOOST_PP_SEQ_TRANSFORM(												\
			_ITWIN_TIMELINE_DETAIL_DEFINE_PROPERTY_VALUES_IMPL_2, ,			\
			BOOST_PP_VARIADIC_SEQ_TO_SEQ(values)))							\
	template<> inline FString _iTwinTimelineGetPropertyName<propertyName>()	\
		{ return BOOST_PP_STRINGIZE(propertyName); }						\
	BOOST_PP_CAT(propertyName, Interpolators) _iTwinTimelineGetInterpolators(propertyName);

//! Example: the following call:
//! 
//! ITWIN_TIMELINE_DEFINE_OBJECT_PROPERTIES(Element,
//!     (Visibility, visibility_)
//!     (Color, color_)
//! )
//! 
//! will generate code similar to this:
//! 
//! struct Element // Actually a boost::fusion::sequence
//! {
//!     Visibility visibility_;
//!     Color color_;
//! };
//! struct ElementStateBase // Actually a boost::fusion::sequence
//! {
//!     boost::optional<Visibility> visibility_;
//!     boost::optional<Color> color_;
//! };
//! using ElementState = BoostFusionUtils::SequenceEx<ElementStateBase>;
//! struct ElementTimelineBase // Actually a boost::fusion::sequence
//! {
//!     ITwin::Timeline::PropertyTimeline<Visibility> visibility_;
//!     ITwin::Timeline::PropertyTimeline<Color> color_;
//! };
//! using ElementTimeline = ITwin::Timeline::ObjectTimeline<
//!     ITwin::Timeline::ObjectTimelineMetadata<
//!         BoostFusionUtils::SequenceEx<ElementTimelineBase>
//!         ElementState>>;
//! 
#define ITWIN_TIMELINE_DEFINE_OBJECT_PROPERTIES(objectName, properties)					\
	BOOST_FUSION_DEFINE_STRUCT_INLINE(objectName, properties)							\
	BOOST_FUSION_DEFINE_STRUCT_INLINE(													\
		BOOST_PP_CAT(objectName, StateBase),											\
		BOOST_PP_SEQ_TRANSFORM(															\
			_ITWIN_TIMELINE_DETAIL_DEFINE_OBJECT_PROPERTIES_IMPL_1, ,					\
			BOOST_PP_VARIADIC_SEQ_TO_SEQ(properties)))									\
	using BOOST_PP_CAT(objectName, State) =												\
		::BoostFusionUtils::SequenceEx<BOOST_PP_CAT(objectName, StateBase)>;			\
	BOOST_FUSION_DEFINE_STRUCT_INLINE(													\
		BOOST_PP_CAT(objectName, TimelineBase),											\
		BOOST_PP_SEQ_TRANSFORM(															\
			_ITWIN_TIMELINE_DETAIL_DEFINE_OBJECT_PROPERTIES_IMPL_2, ,					\
			BOOST_PP_VARIADIC_SEQ_TO_SEQ(properties)))									\
	using BOOST_PP_CAT(objectName, Timeline) =											\
		::ITwin::Timeline::ObjectTimeline<												\
			::ITwin::Timeline::ObjectTimelineMetadata<									\
				::BoostFusionUtils::SequenceEx<BOOST_PP_CAT(objectName, TimelineBase)>,	\
				BOOST_PP_CAT(objectName, State)>>;

#include "Definition.inl"
