add_executable(test_pricers test_pricers.cpp)

target_link_libraries(test_pricers PRIVATE quant_core GTest::gtest_main)
target_include_directories(test_pricers PRIVATE ${CMAKE_SOURCE_DIR}/include)

include(GoogleTest)
gtest_discover_tests(test_pricers)
