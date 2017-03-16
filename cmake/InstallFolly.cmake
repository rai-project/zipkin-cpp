set (FOLLY_VERSION      2017.03.13.00)
set (FOLLY_URL_MD5      3ba9d455edcf6e930b6f43e93e9f99f7)

ExternalProject_Add(folly
    DOWNLOAD_NAME       folly-${FOLLY_VERSION}.tar.gz
    URL                 https://github.com/facebook/folly/archive/v${FOLLY_VERSION}.tar.gz
    URL_MD5             ${FOLLY_URL_MD5}
    CONFIGURE_COMMAND   cd <SOURCE_DIR>/folly && autoreconf -vi && <SOURCE_DIR>/folly/configure --prefix=<INSTALL_DIR> --with-openssl=${OPENSSL_ROOT_DIR} --with-boost=${BOOST_ROOT}
    BUILD_COMMAND       cd <SOURCE_DIR>/folly && make
    INSTALL_COMMAND     cd <SOURCE_DIR>/folly && make install
)

ExternalProject_Get_Property(folly INSTALL_DIR)

set (FOLLY_ROOT_DIR         ${INSTALL_DIR})
set (FOLLY_STATIC_LIBRARY   "${FOLLY_ROOT_DIR}/lib/libfolly.a")
set (FOLLY_LIBRARIES        ${FOLLY_STATIC_LIBRARY})

find_path(FOLLY_INCLUDE_DIR "folly/String.h"
    PATHS       ${FOLLY_ROOT_DIR}/include)

find_package_handle_standard_args(folly
    REQUIRED_ARGS FOLLY_LIBRARIES FOLLY_INCLUDE_DIR
)

add_library(FOLLY_STATIC_LIBRARY STATIC IMPORTED)
add_dependencies(FOLLY_STATIC_LIBRARY folly)
mark_as_advanced(FOLLY_LIBRARIES FOLLY_STATIC_LIBRARY FOLLY_INCLUDE_DIR)