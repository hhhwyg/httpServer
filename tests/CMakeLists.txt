add_executable(ObjectPoolTest base/ObjectPoolTest.cpp)
target_include_directories(ObjectPoolTest PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(ObjectPoolTest PRIVATE httpserver_options)

add_test(NAME base.object_pool COMMAND ObjectPoolTest)
set_tests_properties(base.object_pool PROPERTIES LABELS "unit;base")

add_executable(LogFileTest base/LogFileTest.cpp)
target_include_directories(LogFileTest PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${HTTPSERVER_LEGACY_INCLUDE_DIRS}
)
target_link_libraries(LogFileTest PRIVATE httpserver::core)
add_test(NAME base.log_file COMMAND LogFileTest)
set_tests_properties(base.log_file PROPERTIES LABELS "unit;base;observability")

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

add_executable(AuthenticationRateLimiterTest application/AuthenticationRateLimiterTest.cpp)
target_include_directories(AuthenticationRateLimiterTest PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(AuthenticationRateLimiterTest PRIVATE httpserver::core)
add_test(NAME application.authentication_rate_limiter COMMAND AuthenticationRateLimiterTest)
set_tests_properties(application.authentication_rate_limiter PROPERTIES
    LABELS "unit;application;security"
)

add_executable(MetricsTest application/MetricsTest.cpp)
target_include_directories(MetricsTest PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(MetricsTest PRIVATE httpserver::core)
add_test(NAME application.metrics COMMAND MetricsTest)
set_tests_properties(application.metrics PROPERTIES LABELS "unit;application;observability")

add_executable(DatabaseExecutorTest application/DatabaseExecutorTest.cpp)
target_include_directories(DatabaseExecutorTest PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(DatabaseExecutorTest PRIVATE httpserver::core)
add_test(NAME application.database_executor COMMAND DatabaseExecutorTest)
set_tests_properties(application.database_executor PROPERTIES
    LABELS "unit;application;concurrency"
)

add_test(
    NAME architecture.application_dependencies
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/architecture/ApplicationDependencyTest.sh
            ${PROJECT_SOURCE_DIR}/src/application/ApplicationRouter.cpp
)
set_tests_properties(architecture.application_dependencies PROPERTIES
    LABELS "architecture;application"
)

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

add_test(
    NAME integration.http_protocol
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/integration/HttpProtocolTest.py
            $<TARGET_FILE:WebServer> 18099
)
set_tests_properties(integration.http_protocol PROPERTIES
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    LABELS "integration;http;protocol"
    TIMEOUT 25
)

if(HTTPSERVER_BUILD_MANUAL_TESTS)
    add_executable(LoggingStressTest base/LoggingTest.cpp)
    target_link_libraries(LoggingStressTest PRIVATE httpserver::core)
endif()
add_test(
    NAME integration.websocket_smoke
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/integration/WebSocketSmokeTest.py
            $<TARGET_FILE:WebServer> 18097
)
set_tests_properties(integration.websocket_smoke PROPERTIES
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    LABELS "integration;websocket"
    TIMEOUT 20
)

add_test(
    NAME integration.connection_lifecycle
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/integration/ConnectionLifecycleStressTest.py
            $<TARGET_FILE:WebServer> 18098
)
set_tests_properties(integration.connection_lifecycle PROPERTIES
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    LABELS "integration;lifecycle"
    TIMEOUT 30
)

add_test(
    NAME integration.graceful_shutdown
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/integration/GracefulShutdownTest.py
            $<TARGET_FILE:WebServer> 18101
)
set_tests_properties(integration.graceful_shutdown PROPERTIES
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    LABELS "integration;lifecycle;operations"
    TIMEOUT 12
)

if(HTTPSERVER_BUILD_DATABASE_INTEGRATION_TESTS)
    add_test(
        NAME integration.database_authentication
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/integration/DatabaseAuthenticationTest.py
                $<TARGET_FILE:WebServer> 18100
    )
    set_tests_properties(integration.database_authentication PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        LABELS "integration;database;security"
        TIMEOUT 30
    )
endif()
