cmake_minimum_required(VERSION 2.6)

set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wcomment -Wcast-align -Wswitch -Wunused -Wformat -Wmultichar -Wmissing-braces -Wparentheses -Wpointer-arith -Wsign-compare -Wreturn-type -Wwrite-strings" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wcomment -Wcast-align -Wswitch -Wunused -Wformat -Wmultichar -Wmissing-braces -Wparentheses -Wpointer-arith -Wsign-compare -Wreturn-type -Wwrite-strings -Woverloaded-virtual -Wno-ctor-dtor-privacy -Wnon-virtual-dtor -Wpmf-conversions -Wsign-promo" )

if("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "x86_64" OR "${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "i686")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse -msse2 -msse3")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse -msse2 -msse3")
endif("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "x86_64" OR "${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "i686")

PROJECT( aces )

SET( ACES_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/libs/aces )

ADD_SUBDIRECTORY( libs )
ADD_SUBDIRECTORY( apps )
