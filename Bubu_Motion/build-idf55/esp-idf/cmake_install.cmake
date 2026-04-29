# Install script for directory: /Users/judes/Documents/Arduino/Bubu_Motion_V1/esp/esp-idf

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "TRUE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/Users/judes/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-objdump")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/xtensa/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_driver_gpio/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_timer/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_pm/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/mbedtls/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/bootloader/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esptool_py/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/partition_table/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_app_format/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_bootloader_format/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/app_update/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_partition/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/efuse/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/bootloader_support/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_mm/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/spi_flash/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_system/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_common/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_rom/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/hal/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/log/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/heap/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/soc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_security/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_hw_support/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/freertos/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/newlib/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/pthread/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/cxx/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_psram/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_event/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_ringbuf/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_driver_uart/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_driver_usb_serial_jtag/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_vfs_console/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/vfs/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/lwip/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_netif_stack/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_netif/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_driver_spi/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_driver_i2c/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_driver_i2s/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_driver_jpeg/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_driver_ppa/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/console/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/nvs_flash/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_phy/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/wpa_supplicant/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_coex/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_wifi/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_gdbstub/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/bt/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/wear_levelling/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/sdmmc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_driver_sdmmc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_driver_sdspi/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/fatfs/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/http_parser/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp-tls/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/tcp_transport/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/mqtt/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/78__uart-uhci/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/78__esp-ml307/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_http_server/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/json/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/78__esp-wifi-connect/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_driver_pcnt/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_driver_gptimer/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_driver_mcpwm/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_driver_ana_cmpr/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_driver_sdio/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_driver_dac/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_driver_bitscrambler/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_driver_rmt/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_driver_tsens/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_driver_sdm/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_driver_ledc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_driver_parlio/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_driver_twai/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/driver/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_lcd/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__cmake_utilities/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/78__esp_lcd_nv3023/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__iot_eth/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/78__uart-eth-modem/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/lvgl__lvgl/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/78__xiaozhi-fonts/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_adc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__adc_battery_estimation/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp_codec_dev/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__adc_mic/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__i2c_bus/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__bmi270_sensor/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__button/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/spiffs/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__dl_fft/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp-dsp/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp-sr/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp_jpeg/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp32-camera/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp_audio_codec/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp_audio_effects/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp_image_effects/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp_io_expander/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp_io_expander_tca9554/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp_io_expander_tca95xx_16bit/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp_lcd_touch/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp_lcd_axs15231b/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp_lcd_co5300/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp_lcd_gc9a01/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp_lcd_ili9341/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp_lcd_panel_io_additions/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp_lcd_spd2010/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp_lcd_st7701/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp_lcd_st77916/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp_lcd_st7796/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp_lcd_touch_cst816s/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp_lcd_touch_ft5x06/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp_lcd_touch_gt1151/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp_lcd_touch_gt911/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp_lvgl_port/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp_mmap_assets/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp_new_jpeg/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_driver_isp/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/esp_driver_cam/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp_sccb_intf/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp_cam_sensor/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/usb/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__usb_host_uvc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__esp_video/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__iot_usbh_cdc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__iot_usbh_rndis/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__knob/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__led_strip/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif__freetype/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/laride__heatshrink/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif2022__esp_emote_gfx/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif2022__esp_emote_assets/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif2022__esp_emote_expression/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/espressif2022__image_player/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/tny-robotics__sh1106-esp-idf/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/txp666__otto-emoji-gif-component/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/waveshare__custom_io_expander_ch32v003/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/waveshare__esp_lcd_sh8601/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/waveshare__esp_lcd_touch_cst9217/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/wvirgil123__sscma_client/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/main/cmake_install.cmake")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/build-idf55/esp-idf/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
