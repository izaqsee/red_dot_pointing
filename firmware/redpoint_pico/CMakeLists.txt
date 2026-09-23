cmake_minimum_required(VERSION 3.20)

# The tested current TinyUSB checkout is a read-only dependency, not SDK-bundled TinyUSB.
set(REDPOINT_TINYUSB_PATH "" CACHE PATH "Path to the tested current TinyUSB checkout")
if(NOT EXISTS "${REDPOINT_TINYUSB_PATH}/src/class/net/ncm_device.c")
  message(FATAL_ERROR "Set REDPOINT_TINYUSB_PATH to the tested current TinyUSB checkout")
endif()
get_filename_component(REDPOINT_TINYUSB_PATH "${REDPOINT_TINYUSB_PATH}" ABSOLUTE)
set(PICO_TINYUSB_PATH "${REDPOINT_TINYUSB_PATH}" CACHE PATH "TinyUSB source" FORCE)
set(BOARD raspberry_pi_pico CACHE STRING "Reference RP2040 BSP")
set(FAMILY rp2040 CACHE STRING "Target family")
include("${REDPOINT_TINYUSB_PATH}/hw/bsp/family_support.cmake")
project(redpoint_reva C CXX ASM)
family_initialize_project(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR})
if(NOT PICO_PLATFORM STREQUAL "rp2040")
  message(FATAL_ERROR "RevA Milestone A requires RP2040")
endif()
set(LWIP "${REDPOINT_TINYUSB_PATH}/lib/lwip")
if(NOT EXISTS "${LWIP}/src/core/init.c")
  message(FATAL_ERROR "The external TinyUSB checkout must already contain lib/lwip")
endif()
add_executable(redpoint_reva src/main.c src/usb_descriptors.c src/hid.c
  src/config_platform.cpp src/config_cdc.cpp
  src/input_runtime.cpp src/hid_state.cpp src/platform_io.cpp src/status_led.cpp
  src/config_storage.cpp src/flash_backend.cpp
  ../redpoint/button_action_codec.cpp ../redpoint/button_action_state.cpp ../redpoint/config_record.cpp)
# RevA physical capacity was confirmed with picotool (16384 KiB). Reserve the
# last sector outside all loadable firmware/ROM assets, without putting it in UF2.
set(REDPOINT_FLASH_BYTES 16777216)
math(EXPR REDPOINT_CONFIG_OFFSET "${REDPOINT_FLASH_BYTES} - 4096")
set(layout "${CMAKE_CURRENT_BINARY_DIR}/redpoint-layout")
file(MAKE_DIRECTORY "${layout}")
configure_file(src/flash_layout.h.in "${layout}/flash_layout.h" @ONLY)
file(WRITE "${layout}/pico_flash_region.ld"
  "FLASH(rx) : ORIGIN = 0x10000000, LENGTH = ${REDPOINT_CONFIG_OFFSET}\n")
file(WRITE "${layout}/section_extra_post_end.incl"
  "ASSERT(__flash_binary_end <= 0x10fff000, \"Firmware overlaps RedPoint config sector\")\n")
pico_add_linker_script_override_path(redpoint_reva "${layout}"
  FILES pico_flash_region.ld section_extra_post_end.incl)
target_include_directories(redpoint_reva PRIVATE "${layout}")
target_compile_definitions(redpoint_reva PRIVATE PICO_FLASH_SIZE_BYTES=16777216
  PICO_FLASH_ASSUME_CORE1_SAFE=1 PICO_STACK_SIZE=0x1000)
pico_generate_pio_header(redpoint_reva "${PICO_SDK_PATH}/src/rp2_common/pico_status_led/ws2812.pio")
target_link_libraries(redpoint_reva PRIVATE hardware_pio hardware_flash pico_flash)
target_include_directories(redpoint_reva PRIVATE src "${LWIP}/src/include")
set(LWIP_CORE def dns inet_chksum init ip mem memp netif pbuf raw stats sys tcp tcp_in tcp_out timeouts udp)
foreach(unit IN LISTS LWIP_CORE)
  target_sources(redpoint_reva PRIVATE "${LWIP}/src/core/${unit}.c")
endforeach()
foreach(unit etharp icmp igmp ip4 ip4_addr ip4_frag)
  target_sources(redpoint_reva PRIVATE "${LWIP}/src/core/ipv4/${unit}.c")
endforeach()
target_sources(redpoint_reva PRIVATE "${LWIP}/src/netif/ethernet.c"
  "${LWIP}/src/apps/http/httpd.c" "${LWIP}/src/apps/http/fs.c")
include("${CMAKE_CURRENT_LIST_DIR}/../http_lwip/redpoint_http.cmake")
redpoint_attach_http(redpoint_reva)
family_configure_device_example(redpoint_reva noos)
# TinyUSB example warnings include C-only flags; keep the applicable C++ checks.
get_target_property(redpoint_sources redpoint_reva SOURCES)
foreach(source IN LISTS redpoint_sources)
  if(source MATCHES "\\.cpp$")
    get_source_file_property(source_warnings "${source}" COMPILE_OPTIONS)
    if(source_warnings)
      list(REMOVE_ITEM source_warnings -Wstrict-prototypes -Werror-implicit-function-declaration -Wmissing-prototypes)
      set_source_files_properties("${source}" PROPERTIES COMPILE_OPTIONS "${source_warnings}")
    endif()
  endif()
endforeach()
# Baseline equality intentionally compares the same stored float values exactly.
set_property(SOURCE "${CMAKE_CURRENT_LIST_DIR}/../redpoint/config_command.cpp"
  APPEND PROPERTY COMPILE_OPTIONS -Wno-float-equal)
# CDC is owned by the application; SDK stdio must not claim the same interface.
pico_enable_stdio_usb(redpoint_reva 0)
pico_enable_stdio_uart(redpoint_reva 0)
pico_set_program_name(redpoint_reva "RedPoint RevA Milestone C")
pico_set_program_version(redpoint_reva "0.3.0")
target_link_options(redpoint_reva PRIVATE "LINKER:--print-memory-usage")
