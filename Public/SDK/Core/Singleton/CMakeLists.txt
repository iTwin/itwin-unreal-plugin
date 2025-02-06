cmake_minimum_required(VERSION 3.0)

project(singleton-cpp-lib C CXX)
enable_language(C CXX)

add_library(Singleton SHARED     
    singleton.h
    singleton_api.h
    singleton.cpp
)
target_compile_features(Singleton PRIVATE ${DefaultCXXSTD})
set_target_properties(Singleton PROPERTIES FOLDER "SDK/Core") 