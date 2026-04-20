set(DOXYFILE          "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile")
set(DOXY_INDEX_HTML   "${CMAKE_CURRENT_BINARY_DIR}/html/index.html")
set(DOXY_EXTRA_FILES  "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(FE_LOGO_SVG       "${CMAKE_CURRENT_BINARY_DIR}/logo.svg")
set(FE_LOGO_PNG       "${CMAKE_CURRENT_BINARY_DIR}/logo.png")

configure_file(Doxyfile.in ${DOXYFILE} @ONLY)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../assets/logo.png" "${FE_LOGO_PNG}" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../assets/logo.svg" "${FE_LOGO_SVG}" COPYONLY)

add_custom_command(
    OUTPUT ${DOXY_INDEX_HTML}
    COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYFILE}
    MAIN_DEPENDENCY ${DOXYFILE}
    DEPENDS fe ${DOXY_EXTRA_FILES} ${FE_LOGO_PNG} ${FE_LOGO_SVG}
    COMMENT "Generating Doxygen HTML documentation"
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)

add_custom_target(docs ALL DEPENDS fe ${DOXY_INDEX_HTML})
