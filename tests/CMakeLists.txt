add_executable(ObjectPoolTest base/ObjectPoolTest.cpp)
target_include_directories(ObjectPoolTest PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(ObjectPoolTest PRIVATE httpserver_options)

add_test(NAME base.object_pool COMMAND ObjectPoolTest)
set_tests_properties(base.object_pool PROPERTIES LABELS "unit;base")

add_executable(UringOperationRegistryTest base/UringOperationRegistryTest.cpp)
target_include_directories(UringOperationRegistryTest PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(UringOperationRegistryTest PRIVATE httpserver_options)
add_test(NAME base.uring_operation_registry COMMAND UringOperationRegistryTest)
set_tests_properties(base.uring_operation_registry PROPERTIES LABELS "unit;uring")

add_executable(PollerBackendTest base/PollerBackendTest.cpp)
target_include_directories(PollerBackendTest PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${HTTPSERVER_LEGACY_INCLUDE_DIRS}
)
target_link_libraries(PollerBackendTest PRIVATE httpserver::core)
add_test(NAME base.poller_backend COMMAND PollerBackendTest)
set_tests_properties(base.poller_backend PROPERTIES LABELS "unit;poller")

add_executable(HttpDataLifecycleTest httpdata/HttpDataLifecycleTest.cpp)
target_include_directories(HttpDataLifecycleTest PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${HTTPSERVER_LEGACY_INCLUDE_DIRS}
)
target_link_libraries(HttpDataLifecycleTest PRIVATE httpserver::core)
add_test(NAME httpdata.lifecycle COMMAND HttpDataLifecycleTest)
set_tests_properties(httpdata.lifecycle PROPERTIES LABELS "unit;lifecycle")

if(HTTPSERVER_BUILD_MANUAL_TESTS)
    add_executable(LoggingStressTest base/LoggingTest.cpp)
    target_link_libraries(LoggingStressTest PRIVATE httpserver::core)
endif()
