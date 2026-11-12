# The hanan router as ALIGN builds it: the program `hanan2`, installed into the align package
# (align/bin/hanan2), where align/pnr/hanan2.py runs it for `--router hanan2`. The Makefile
# beside this file builds the same sources standalone (`hanan_router`).
file(GLOB HANAN2_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

find_package(Threads REQUIRED)

add_executable(hanan2 ${HANAN2_SOURCES})
# its own nlohmann/json (include/) and boost.polygon (src/polygon) first; Boost's other headers
# from ALIGN's Boost::boost
target_include_directories(hanan2 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hanan2 PRIVATE Boost::boost Threads::Threads)
target_compile_definitions(hanan2 PRIVATE DEBUG=0)
# the Makefile's optimization (its -march=native left out: the program runs where ALIGN is installed)
target_compile_options(hanan2 PRIVATE -O3 -ffast-math -funroll-loops)
target_code_coverage(hanan2)

install(TARGETS hanan2 DESTINATION "${CMAKE_INSTALL_PREFIX}/align/bin")
