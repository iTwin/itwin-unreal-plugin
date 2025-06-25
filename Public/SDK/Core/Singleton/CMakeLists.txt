cmake_minimum_required(VERSION 3.0)

project(singleton-cpp-lib C CXX)
enable_language(C CXX)

set(libtype STATIC)
if (SINGLETONDLL)
    set(libtype SHARED)
endif()

add_library(Singleton ${libtype}     
    singleton.h
    singleton_api.h
    singleton.cpp
)
target_compile_features(Singleton PRIVATE ${DefaultCXXSTD})
set_target_properties(Singleton PROPERTIES FOLDER "SDK/Core") 