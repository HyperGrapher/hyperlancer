find_package(Qt6 REQUIRED COMPONENTS Test)

# Tests link the core library (database, stores, parsers, clients), which holds everything
# but the QML layer.
function(hyperlancer_add_test name)
    qt_add_executable(${name} ${name}.cpp ${ARGN})
    target_link_libraries(${name} PRIVATE HyperlancerCore Qt6::Test Qt6::Qml)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

hyperlancer_add_test(tst_appsettings ${PROJECT_SOURCE_DIR}/src/AppSettings.cpp)
hyperlancer_add_test(tst_urlnormalizer)
hyperlancer_add_test(tst_jsonlines)
hyperlancer_add_test(tst_migrations)
hyperlancer_add_test(tst_profileingest)
hyperlancer_add_test(tst_langsearch)
hyperlancer_add_test(tst_gmailquery)
