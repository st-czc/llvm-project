##===--------------------------------------------------------------------------
##                   ROCm Device Libraries
##
## This file is distributed under the University of Illinois Open Source
## License. See LICENSE.TXT for details.
##===--------------------------------------------------------------------------

# The subdirectories are symlinks to the amdgcn ones. They are added through
# this directory so that their binary directories are kept apart.
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")
include(OCL)

add_subdirectory(oclc)
add_subdirectory(ocml)
