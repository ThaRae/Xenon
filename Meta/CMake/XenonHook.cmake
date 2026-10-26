# Injected into Ladybird's top-level CMakeLists.txt via CMAKE_PROJECT_ladybird_INCLUDE (see Meta/xenon.py).
#
# This runs right after project(ladybird), before any engine targets exist. We defer adding the Xenon frontend until the
# end of Ladybird's top-level CMakeLists.txt, when LibWebView, the helper processes, and the resource helpers are all
# defined. This lets us build on an unmodified Ladybird checkout.
#
# NB: CMake doesn't allow add_subdirectory() during deferred execution, so the frontend is include()d instead.

get_filename_component(XENON_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL include "${XENON_SOURCE_DIR}/UI/Xenon.cmake")
