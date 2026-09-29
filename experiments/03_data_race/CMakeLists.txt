add_executable(exp_03_data_race main.cpp)
target_link_libraries(exp_03_data_race PRIVATE Threads::Threads)
