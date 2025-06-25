/*--------------------------------------------------------------------------------------+
|
|     $Source: AdvVizLinkType.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once
#ifndef INC_ADVVIZ_LINK_H_
#define INC_ADVVIZ_LINK_H_

#ifdef ADVVIZDLL
#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(ADVVIZ_LINK_EXPORTS) // add by CMake 
#    ifdef __GNUC__
#      define  ADVVIZ_LINK __attribute__(dllexport)
#    else
#      define  ADVVIZ_LINK __declspec(dllexport)
#    endif
#  else
#    ifdef __GNUC__
#      define  ADVVIZ_LINK __attribute__(dllimport)
#    else
#      define  ADVVIZ_LINK __declspec(dllimport)
#    endif
#  endif // ADVVIZ_LINK_EXPORTS

#elif defined __GNUC__
#  if __GNUC__ >= 4
#    define ADVVIZ_LINK __attribute__ ((visibility ("default")))
#  else
#    define ADVVIZ_LINK
#  endif

#elif defined __clang__
#  define ADVVIZ_LINK __attribute__ ((visibility ("default")))

#else
#   error "Do not know how to export classes for this platform"
#endif

#endif
#endif//SINGLETONDLL

#ifndef ADVVIZ_LINK
#define  ADVVIZ_LINK 
#endif