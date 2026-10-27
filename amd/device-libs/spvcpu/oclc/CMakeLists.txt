##===--------------------------------------------------------------------------
##                   ROCm Device Libraries
##
## This file is distributed under the University of Illinois Open Source
## License. See LICENSE.TXT for details.
##===--------------------------------------------------------------------------

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/inc)

# The ISA and ABI version libraries are not built: they describe the amdgcn
# target, and nothing in the spvcpu device code reads them.
file(GLOB sources ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cl)
foreach(file ${sources})
  get_filename_component(name ${file} NAME_WE)
  get_filename_component(ext ${file} EXT)
  opencl_bc_lib(NAME oclc_${name} SOURCES ${file})
endforeach()
