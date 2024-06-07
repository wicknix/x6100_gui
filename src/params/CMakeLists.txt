file(GLOB_RECURSE sources *.c)

target_sources(${PROJECT_NAME} PUBLIC
    ${sources}
)
