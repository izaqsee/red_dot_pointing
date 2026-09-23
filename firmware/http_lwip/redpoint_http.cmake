# Attach to a target compiling standard lwIP httpd.c + fs.c (NO_SYS=1).
# The target must also link the RedPoint platform implementations of config
# storage, status LED, action validation/formatting, and call load/baseline at boot.
set(REDPOINT_HTTP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
function(redpoint_attach_http target)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  get_filename_component(root "${REDPOINT_HTTP_ROOT}" ABSOLUTE)
  set(generated "${CMAKE_CURRENT_BINARY_DIR}/redpoint-http")
  set(fsdata "${generated}/fsdata_redpoint.c")
  add_custom_command(OUTPUT "${fsdata}"
    COMMAND "${Python3_EXECUTABLE}" "${root}/tools/generate_http_fsdata.py" --output "${fsdata}"
    DEPENDS "${root}/tools/generate_http_fsdata.py"
      "${root}/configurator/index.html" "${root}/configurator/app.js"
      "${root}/configurator/shortcuts.js" "${root}/configurator/style.css"
    VERBATIM)
  add_custom_target(${target}_redpoint_assets DEPENDS "${fsdata}")
  add_dependencies(${target} ${target}_redpoint_assets)
  target_sources(${target} PRIVATE
    "${root}/firmware/http_lwip/redpoint_httpd.cpp"
    "${root}/firmware/redpoint/config_command.cpp"
    "${root}/firmware/redpoint/config_http.cpp")
  target_include_directories(${target} PRIVATE "${root}/firmware/redpoint" "${generated}")
  target_compile_definitions(${target} PRIVATE
    LWIP_HTTPD_SUPPORT_POST=1 LWIP_HTTPD_CUSTOM_FILES=1
    LWIP_HTTPD_DYNAMIC_HEADERS=0 LWIP_HTTPD_DYNAMIC_FILE_READ=0
    HTTPD_PRECALCULATED_CHECKSUM=0 HTTPD_FSDATA_FILE="fsdata_redpoint.c")
endfunction()
