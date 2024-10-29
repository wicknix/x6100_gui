cmake_minimum_required(VERSION 3.23)

project(x6100_gui)

include_directories(.)
include_directories(third-party/rapidxml)
include_directories(third-party/utf8)

add_subdirectory(lvgl)

if(ENABLE_TESTING)
        enable_testing()
        add_subdirectory(src/ft8)
        add_subdirectory(src/qth)
        add_subdirectory(tests)
else()
        add_subdirectory(src)
        add_subdirectory(lv_drivers)
        add_subdirectory(sql)
        install(TARGETS ${PROJECT_NAME} DESTINATION sbin)
        install(DIRECTORY rootfs/
                DESTINATION /
                USE_SOURCE_PERMISSIONS
        )
        install(DIRECTORY images DESTINATION share/x6100)
endif()


