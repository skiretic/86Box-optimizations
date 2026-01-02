if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/mmx_neon_micro.c)
    return()
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

    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/dynarec_test.c AND NOT QT)
        add_executable(dynarec_test dynarec_test.c)
        target_compile_options(dynarec_test PRIVATE -O3 -mcpu=apple-m1 -march=armv8-a)

        # Link with required 86Box libraries
        target_link_libraries(dynarec_test 86Box)

        install(TARGETS dynarec_test
                RUNTIME DESTINATION bin
                BUNDLE DESTINATION bin)
    endif()
endif()
