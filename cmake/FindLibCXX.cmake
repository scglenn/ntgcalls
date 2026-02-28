set(LIBCXX_INCLUDE ${DEPS_DIR}/libcxx)
set(LIBCXXABI_INCLUDE ${DEPS_DIR}/libcxxabi)
set(BUILDTOOLS_INCLUDE ${DEPS_DIR}/buildtools)

if((LINUX OR MACOS OR ANDROID) AND USE_LIBCXX)
    GitClone(
        URL https://chromium.googlesource.com/external/github.com/llvm/llvm-project/libcxx.git
        COMMIT ${LIBCXX_COMMIT}
        DIRECTORY ${LIBCXX_INCLUDE}
    )
    GitClone(
        URL https://chromium.googlesource.com/chromium/src/buildtools.git
        COMMIT ${BUILDTOOLS_COMMIT}
        DIRECTORY ${BUILDTOOLS_INCLUDE}
    )
    file(COPY ${BUILDTOOLS_INCLUDE}/third_party/libc++/__config_site DESTINATION ${LIBCXX_INCLUDE}/include)
    file(COPY ${BUILDTOOLS_INCLUDE}/third_party/libc++/__assertion_handler DESTINATION ${LIBCXX_INCLUDE}/include)
    GitClone(
        URL https://chromium.googlesource.com/external/github.com/llvm/llvm-project/libcxxabi.git
        COMMIT ${LIBCXX_ABI_COMMIT}
        DIRECTORY ${LIBCXXABI_INCLUDE}
    )
    add_compile_options(
        "$<$<COMPILE_LANGUAGE:CXX>:-nostdinc++>"
        "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<BOOL:LIBCXX_INCLUDE_DIR>>:-isystem${LIBCXX_INCLUDE}/include>"
        "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<BOOL:LIBCXXABI_INCLUDE_DIR>>:-isystem${LIBCXXABI_INCLUDE}/include>"
        "$<$<COMPILE_LANGUAGE:OBJCXX>:-nostdinc++>"
        "$<$<AND:$<COMPILE_LANGUAGE:OBJCXX>,$<BOOL:LIBCXX_INCLUDE_DIR>>:-isystem${LIBCXX_INCLUDE}/include>"
        "$<$<AND:$<COMPILE_LANGUAGE:OBJCXX>,$<BOOL:LIBCXXABI_INCLUDE_DIR>>:-isystem${LIBCXXABI_INCLUDE}/include>"
    )
endif ()
