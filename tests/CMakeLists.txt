add_executable(fast_write_test fast_write_test.cpp)
target_link_libraries(fast_write_test LINK_PUBLIC umq userver pthread)

add_executable(wsserver_test wsserver_test.cpp)
target_link_libraries(wsserver_test LINK_PUBLIC umq userver pthread)

add_executable(umq_demo umq_demo.cpp)
target_link_libraries(umq_demo LINK_PUBLIC umq userver pthread)
