if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/mmx_neon_micro.c)
    return()
endif()

if(NEW_DYNAREC)
    add_compile_definitions(USE_NEW_DYNAREC)
    add_compile_definitions(NEW_DYNAREC_BACKEND)
endif()

if(DYNAREC)
    add_compile_definitions(USE_DYNAREC)
endif()

if(APPLE AND ARCH STREQUAL "arm64")
    add_executable(mmx_neon_micro mmx_neon_micro.c)

    target_compile_options(mmx_neon_micro PRIVATE -O3 -mcpu=apple-m1 -march=armv8-a)

    install(TARGETS mmx_neon_micro
            RUNTIME DESTINATION bin
            BUNDLE DESTINATION bin)

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/dynarec_micro.c)
        add_executable(dynarec_micro dynarec_micro.c)
        target_compile_options(dynarec_micro PRIVATE -O3 -mcpu=apple-m1 -march=armv8-a)
        install(TARGETS dynarec_micro
                RUNTIME DESTINATION bin
                BUNDLE DESTINATION bin)
    endif()

    include_directories(${CMAKE_CURRENT_BINARY_DIR}/../src/include)
    include_directories(../src/include)
    include_directories(../src/cpu)
    include_directories(../src/codegen_new)

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/dynarec_sanity.c)
        add_executable(dynarec_sanity dynarec_sanity.c)
        target_compile_options(dynarec_sanity PRIVATE -O3 -mcpu=apple-m1 -march=armv8-a)

        target_link_libraries(dynarec_sanity)

        install(TARGETS dynarec_sanity
                RUNTIME DESTINATION bin
                BUNDLE DESTINATION bin)
    endif()
endif()
