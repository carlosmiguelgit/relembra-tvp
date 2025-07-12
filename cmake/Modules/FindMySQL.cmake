# FindMySQL.cmake

find_path(MYSQL_INCLUDE_DIR mysql.h
    PATHS
        /usr/include/mysql
        /usr/local/include/mysql
)

find_library(MYSQL_LIBRARIES
    NAMES mysqlclient
    PATHS
        /usr/lib
        /usr/lib/x86_64-linux-gnu
        /usr/local/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MySQL DEFAULT_MSG MYSQL_INCLUDE_DIR MYSQL_LIBRARIES)

mark_as_advanced(MYSQL_INCLUDE_DIR MYSQL_LIBRARIES)
