/*--------------------------------------------------------------------------------------+
|
|     $Source: FactoryClassInternalHelper.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once
#include "Core/Singleton/singleton.h"

#define DEFINEFACTORYGLOBALS(CLASS)\
	template<>\
	Tools::Factory<I##CLASS>::Globals::Globals()\
	{\
		newFct_ = []() {return static_cast<I##CLASS*>(new CLASS());};\
	}\
	template<>\
	Tools::Factory<I##CLASS>::Globals& Tools::Factory<I##CLASS>::GetGlobals()\
	{\
		return singleton<Tools::Factory<I##CLASS>::Globals>();\
	}
