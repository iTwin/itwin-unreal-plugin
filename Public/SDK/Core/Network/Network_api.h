/*--------------------------------------------------------------------------------------+
|
|     $Source: Network_api.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once
#ifndef INC_NETWORK_API_H_
#define INC_NETWORK_API_H_

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(Network_EXPORTS) // add by CMake 
#    ifdef __GNUC__
#      define  NETWORK_API __attribute__(dllexport)
#    else
#      define  NETWORK_API __declspec(dllexport)
#    endif
#  else
#    ifdef __GNUC__
#      define  NETWORK_API __attribute__(dllimport)
#    else
#      define  NETWORK_API __declspec(dllimport)
#    endif
#  endif // Network_EXPORTS

#elif defined __GNUC__
#  if __GNUC__ >= 4
#    define NETWORK_API __attribute__ ((visibility ("default")))
#  else
#    define NETWORK_API
#  endif

#elif defined __clang__
#  define NETWORK_API __attribute__ ((visibility ("default")))

#else
#   error "Do not know how to export classes for this platform"
#endif

#endif
