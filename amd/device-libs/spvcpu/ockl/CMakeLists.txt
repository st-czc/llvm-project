##===--------------------------------------------------------------------------
##                   ROCm Device Libraries
##
## This file is distributed under the University of Illinois Open Source
## License. See LICENSE.TXT for details.
##===--------------------------------------------------------------------------

set(sources
  ${CMAKE_CURRENT_SOURCE_DIR}/src/mul24.cl
)

file(GLOB src2_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/src2/*.cl
)
list(APPEND sources ${src2_sources})

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/inc)

opencl_bc_lib(NAME ockl SOURCES ${sources})
