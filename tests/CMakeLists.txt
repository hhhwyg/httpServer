add_executable(ObjectPoolTest base/ObjectPoolTest.cpp)
target_include_directories(ObjectPoolTest PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(ObjectPoolTest PRIVATE httpserver_options)

add_test(NAME base.object_pool COMMAND ObjectPoolTest)
set_tests_properties(base.object_pool PROPERTIES LABELS "unit;base")

add_executable(UringOperationRegistryTest base/UringOperationRegistryTest.cpp)
target_include_directories(UringOperationRegistryTest PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(UringOperationRegistryTest PRIVATE httpserver_options)
add_test(NAME base.uring_operation_registry COMMAND UringOperationRegistryTest)
set_tests_properties(base.uring_operation_registry PROPERTIES LABELS "unit;uring")

add_executable(ChannelOwnershipTest base/ChannelOwnershipTest.cpp)
target_include_directories(ChannelOwnershipTest PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(ChannelOwnershipTest PRIVATE httpserver::core)
add_test(NAME base.channel_ownership COMMAND ChannelOwnershipTest)
set_tests_properties(base.channel_ownership PROPERTIES LABELS "unit;transport")

add_executable(PollerBackendTest base/PollerBackendTest.cpp)
target_include_directories(PollerBackendTest PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${HTTPSERVER_LEGACY_INCLUDE_DIRS}
)
target_link_libraries(PollerBackendTest PRIVATE httpserver::core)
add_test(NAME base.poller_backend COMMAND PollerBackendTest)
set_tests_properties(base.poller_backend PROPERTIES LABELS "unit;poller")

add_executable(CryptoUtilTest util/CryptoUtilTest.cpp)
target_include_directories(CryptoUtilTest PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${HTTPSERVER_LEGACY_INCLUDE_DIRS}
)
target_link_libraries(CryptoUtilTest PRIVATE httpserver::core)
add_test(NAME util.crypto COMMAND CryptoUtilTest)
set_tests_properties(util.crypto PROPERTIES LABELS "unit;security")

add_executable(ServerConfigTest config/ServerConfigTest.cpp)
target_include_directories(ServerConfigTest PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(ServerConfigTest PRIVATE httpserver::core)
add_test(NAME config.server COMMAND ServerConfigTest)
set_tests_properties(config.server PROPERTIES LABELS "unit;config;security")

add_executable(HttpDataLifecycleTest httpdata/HttpDataLifecycleTest.cpp)
target_include_directories(HttpDataLifecycleTest PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${HTTPSERVER_LEGACY_INCLUDE_DIRS}
)
target_link_libraries(HttpDataLifecycleTest PRIVATE httpserver::core)
add_test(NAME httpdata.lifecycle COMMAND HttpDataLifecycleTest)
set_tests_properties(httpdata.lifecycle PROPERTIES LABELS "unit;lifecycle")

add_executable(MimeTypeTest httpdata/MimeTypeTest.cpp)
target_include_directories(MimeTypeTest PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(MimeTypeTest PRIVATE httpserver::core)
add_test(NAME httpdata.mime_type COMMAND MimeTypeTest)
set_tests_properties(httpdata.mime_type PROPERTIES LABELS "unit;protocol")

add_executable(HttpRequestParserTest httpdata/HttpRequestParserTest.cpp)
target_include_directories(HttpRequestParserTest PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(HttpRequestParserTest PRIVATE httpserver::core)
add_test(NAME httpdata.request_parser COMMAND HttpRequestParserTest)
set_tests_properties(httpdata.request_parser PROPERTIES LABELS "unit;protocol")

add_executable(HttpResponseWriterTest httpdata/HttpResponseWriterTest.cpp)
target_include_directories(HttpResponseWriterTest PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(HttpResponseWriterTest PRIVATE httpserver::core)
add_test(NAME httpdata.response_writer COMMAND HttpResponseWriterTest)
set_tests_properties(httpdata.response_writer PROPERTIES LABELS "unit;protocol")

add_executable(StaticFileServiceTest httpdata/StaticFileServiceTest.cpp)
target_include_directories(StaticFileServiceTest PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(StaticFileServiceTest PRIVATE httpserver::core)
add_test(NAME httpdata.static_file COMMAND StaticFileServiceTest)
set_tests_properties(httpdata.static_file PROPERTIES
    LABELS "unit;protocol"
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)

add_executable(WebSocketCodecTest httpdata/WebSocketCodecTest.cpp)
target_include_directories(WebSocketCodecTest PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(WebSocketCodecTest PRIVATE httpserver::core)
add_test(NAME httpdata.websocket_codec COMMAND WebSocketCodecTest)
set_tests_properties(httpdata.websocket_codec PROPERTIES LABELS "unit;protocol")

add_executable(ApplicationBoundaryTest application/ApplicationBoundaryTest.cpp)
target_include_directories(ApplicationBoundaryTest PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(ApplicationBoundaryTest PRIVATE httpserver::core)
add_test(NAME application.boundary COMMAND ApplicationBoundaryTest)
set_tests_properties(application.boundary PROPERTIES LABELS "unit;application")

add_executable(ChatRoomTest chat/ChatRoomTest.cpp)
target_include_directories(ChatRoomTest PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${HTTPSERVER_LEGACY_INCLUDE_DIRS}
)
target_link_libraries(ChatRoomTest PRIVATE httpserver::core)
add_test(NAME chat.room_membership COMMAND ChatRoomTest)
set_tests_properties(chat.room_membership PROPERTIES LABELS "unit;chat")

add_test(
    NAME integration.http_smoke
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/integration/HttpSmokeTest.sh
            $<TARGET_FILE:WebServer> 18096
)
set_tests_properties(integration.http_smoke PROPERTIES
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    LABELS "integration;http"
    TIMEOUT 20
)

if(HTTPSERVER_BUILD_MANUAL_TESTS)
    add_executable(LoggingStressTest base/LoggingTest.cpp)
    target_link_libraries(LoggingStressTest PRIVATE httpserver::core)
endif()
