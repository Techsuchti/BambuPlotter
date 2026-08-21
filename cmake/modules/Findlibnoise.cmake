# Findlibnoise.cmake - Find libnoise for structured fuzzy skin
#
# Supports both the historical BambuLab libnoise layout and the vcpkg
# libnoise port. The latter installs headers under include/noise and the
# static library as noise-static.lib on Windows.
#
# Provides: noise::noise (imported static library)

if(libnoise_FOUND)
  return()
endif()

# Allow callers to provide the dependency root through CMAKE_PREFIX_PATH.
# vcpkg's x64-windows prefix is one such root.
find_path(LIBNOISE_INCLUDE_DIR
  NAMES noise/noise.h noise.h
  PATHS ${CMAKE_PREFIX_PATH}
  PATH_SUFFIXES include include/libnoise
)

find_library(LIBNOISE_LIBRARY
  NAMES noise-static libnoise_static noise_static
  PATHS ${CMAKE_PREFIX_PATH}
  PATH_SUFFIXES lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(libnoise
  REQUIRED_VARS LIBNOISE_INCLUDE_DIR LIBNOISE_LIBRARY
)

if(libnoise_FOUND AND NOT TARGET noise::noise)
  add_library(noise::noise STATIC IMPORTED GLOBAL)
  set_target_properties(noise::noise PROPERTIES
    IMPORTED_LOCATION "${LIBNOISE_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${LIBNOISE_INCLUDE_DIR}"
  )
endif()

mark_as_advanced(LIBNOISE_INCLUDE_DIR LIBNOISE_LIBRARY)
