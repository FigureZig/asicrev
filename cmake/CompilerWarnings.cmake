# Shared warning / sanitizer configuration for asicrev targets.

function(asicrev_set_warnings target)
  if(NOT ASICREV_ENABLE_WARNINGS)
    return()
  endif()

  set(gcc_like_warnings
      -Wall
      -Wextra
      -Wpedantic
      -Wshadow
      -Wnon-virtual-dtor
      -Wold-style-cast
      -Wcast-align
      -Wunused
      -Woverloaded-virtual
      -Wconversion
      -Wsign-conversion
      -Wdouble-promotion
      -Wformat=2
      -Wimplicit-fallthrough)

  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    list(APPEND gcc_like_warnings -Wmisleading-indentation -Wduplicated-cond
         -Wduplicated-branches -Wlogical-op -Wuseless-cast)
  endif()

  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(${target} PRIVATE ${gcc_like_warnings})
  elseif(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
  endif()
endfunction()

function(asicrev_set_sanitizers target)
  if(NOT ASICREV_ENABLE_SANITIZERS)
    return()
  endif()
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined
                                             -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()
