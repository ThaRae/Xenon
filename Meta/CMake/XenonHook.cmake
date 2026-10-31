get_filename_component(XENON_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL include "${XENON_SOURCE_DIR}/UI/Xenon.cmake")
