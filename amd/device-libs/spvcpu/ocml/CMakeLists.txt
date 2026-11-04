##===--------------------------------------------------------------------------
##                   ROCm Device Libraries
##
## This file is distributed under the University of Illinois Open Source
## License. See LICENSE.TXT for details.
##===--------------------------------------------------------------------------

file(GLOB sources
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cl
  )

list(REMOVE_ITEM sources
  ${CMAKE_CURRENT_SOURCE_DIR}/src/powF.cl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/powD.cl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/powH.cl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/sinF.cl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/cosF.cl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tanF.cl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/sincosF.cl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/sinD.cl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/cosD.cl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tanD.cl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/sincosD.cl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/sinH.cl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/cosH.cl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tanH.cl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/sincosH.cl)

file(GLOB src2_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/src2/*.cl
  )
list(APPEND sources ${src2_sources})

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../irif/inc)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../oclc/inc)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/inc)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

set(native_func_flags -fapprox-func)

set_source_files_properties(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/native_logF.cl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/native_expF.cl
  PROPERTIES COMPILE_FLAGS "${native_func_flags}")


# This implementation of sqrt will not be used through opencl, openmp,
# or hip. Compile to be correctly rounded just in case
set_source_files_properties(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/sqrtF.cl
  PROPERTIES COMPILE_FLAGS -cl-fp32-correctly-rounded-divide-sqrt)

opencl_bc_lib(NAME ocml SOURCES ${sources})
