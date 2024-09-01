cmake_minimum_required(VERSION 3.23)

project(x6100_gui)

include_directories(.)
include_directories(third-party/rapidxml)
include_directories(third-party/utf8)

add_subdirectory(lvgl)
add_subdirectory(lv_drivers)
add_subdirectory(src)
add_subdirectory(sql)

install(TARGETS ${PROJECT_NAME} DESTINATION sbin)
install(DIRECTORY rootfs/
        DESTINATION /
        USE_SOURCE_PERMISSIONS
)
install(DIRECTORY images DESTINATION share/x6100)
