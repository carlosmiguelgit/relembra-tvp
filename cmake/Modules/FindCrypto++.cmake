find_path(CRYPTOPP_INCLUDE_DIR cryptlib.h
  PATHS /usr/include /usr/local/include
)

find_library(CRYPTOPP_LIBRARY NAMES cryptopp
  PATHS /usr/lib /usr/local/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Crypto++ DEFAULT_MSG CRYPTOPP_LIBRARY CRYPTOPP_INCLUDE_DIR)

if(Crypto++_FOUND)
  set(Crypto++_INCLUDE_DIR ${CRYPTOPP_INCLUDE_DIR})
  set(Crypto++_LIBRARIES ${CRYPTOPP_LIBRARY})
endif()
