/*--------------------------------------------------------------------------------------+
|
|     $Source: SchedulesJsonMacros.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

// conflicts with ITwinWebServices.cpp
#undef JSON_GETOBJ_OR
#undef JSON_GETOBJ_OR_CUSTOM

/// Get a non-empty string from the Json object passed, or log an error and do something (typically continue
/// or return)
#define JSON_GETSTR_OR(JsonObj, Field, Dest, WhatToDo) \
	{ if (!(JsonObj)->TryGetStringField(TEXT(Field), Dest) || Dest.IsEmpty()) { \
		BE_LOGE("ITwin4DImp", "Parsing error or empty string field in Json response: " << Field); \
		WhatToDo; \
	}}
/// Get a number from the Json object passed, or log an error and do something (typically continue or return)
#define JSON_GETNUMBER_OR(JsonObj, Field, Dest, WhatToDo) \
	{ if (!(JsonObj)->TryGetNumberField(TEXT(Field), Dest)) { \
		BE_LOGE("ITwin4DImp", "Parsing error for number field in Json response: " << Field); \
		WhatToDo; \
	}}
/// Get a boolean from the Json object passed, or log an error and do something (typically continue or return)
#define JSON_GETBOOL_OR(JsonObj, Field, Dest, WhatToDo) \
	{ if (!(JsonObj)->TryGetBoolField(TEXT(Field), Dest)) { \
		BE_LOGE("ITwin4DImp", "Parsing error for boolean field in Json response: " << Field); \
		WhatToDo; \
	}}
/// Get an Object from the Json object passed, or log an error and do something (typically continue or return)
#define JSON_GETOBJ_OR(JsonObj, Field, Dest, WhatToDo) \
	{ Dest = nullptr; \
		if (!(JsonObj)->TryGetObjectField(TEXT(Field), Dest) || !Dest) { \
		BE_LOGE("ITwin4DImp", "Parsing error for object field in Json response: " << Field); \
		WhatToDo; \
	}}
/// Get the optional deleted flag (not seen for animation bindings, at least in Legacy schedules) from the Json
/// object passed, or set it to false
#define JSON_GETDELETEDORFALSE(JsonObj, Dest) \
	{ bool ParsedDeletedFlag = false; \
		if (!(JsonObj)->TryGetBoolField(TEXT("deleted"), ParsedDeletedFlag)) { \
			Dest.bDeleted = EDeletedProp(false); \
		} \
		Dest.bDeleted = EDeletedProp(ParsedDeletedFlag); }
