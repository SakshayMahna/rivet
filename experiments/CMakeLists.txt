find_package(Threads REQUIRED)

option(RIVET_ENABLE_TSAN "Build with ThreadSanitizer" OFF)
if(RIVET_ENABLE_TSAN)
    add_compile_options(-fsanitize=thread -g)
    add_link_options(-fsanitize=thread)
endif()

add_subdirectory(01_threads)
add_subdirectory(02_jthreads)
add_subdirectory(03_data_race)
