/*--------------------------------------------------------------------------------------+
|
|     $Source: Definition.inl $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "Definition.h"

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <boost/preprocessor/tuple/elem.hpp>
	#include <boost/preprocessor/tuple/size.hpp>
	#include <boost/preprocessor/if.hpp>
	#include <boost/preprocessor/comparison/greater_equal.hpp>
#include <Compil/AfterNonUnrealIncludes.h>

#include "Interpolators.h"

#define _ITWIN_TIMELINE_DETAIL_DEFINE_PROPERTY_VALUES_IMPL_1(unusedSeq, unusedData, elem)	\
	BOOST_PP_TUPLE_ELEM(0, elem), BOOST_PP_TUPLE_ELEM(1, elem)

#define _ITWIN_TIMELINE_DETAIL_DEFINE_PROPERTY_VALUES_IMPL_2(unusedSeq, unusedData, elem)	\
	BOOST_PP_IF(BOOST_PP_GREATER_EQUAL(BOOST_PP_TUPLE_SIZE(elem), 3),					\
		BOOST_PP_TUPLE_ELEM(2, elem),													\
		::ITwin::Timeline::Interpolators::Default),								\
	BOOST_PP_TUPLE_ELEM(1, elem)

#define _ITWIN_TIMELINE_DETAIL_DEFINE_OBJECT_PROPERTIES_IMPL_1(unusedSeq, unusedData, elem)	\
	boost::optional<BOOST_PP_TUPLE_ELEM(0, elem)>, BOOST_PP_TUPLE_ELEM(1, elem)

#define _ITWIN_TIMELINE_DETAIL_DEFINE_OBJECT_PROPERTIES_IMPL_2(unusedSeq, unusedData, elem)	\
	::ITwin::Timeline::PropertyTimeline<BOOST_PP_TUPLE_ELEM(0, elem)>, BOOST_PP_TUPLE_ELEM(1, elem)
