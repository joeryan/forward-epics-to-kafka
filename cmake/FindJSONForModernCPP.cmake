find_path(JSONFORMODERNCPP_INCLUDE_DIR NAMES nlohmann/json.hpp)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(JSONFORMODERNCPP DEFAULT_MSG
    JSONFORMODERNCPP_INCLUDE_DIR
)
