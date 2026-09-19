# SPDX-License-Identifier: BSD-3-Clause
cmake_minimum_required(VERSION 3.20.0)
get_filename_component(NS51_REPOSITORY "${CMAKE_CURRENT_SOURCE_DIR}/../.." REALPATH)
get_filename_component(NS51_BUILD "${CMAKE_BINARY_DIR}" REALPATH)
cmake_path(IS_PREFIX NS51_REPOSITORY "${NS51_BUILD}" NORMALIZE NS51_IN_REPOSITORY)
if(NS51_IN_REPOSITORY)
  message(FATAL_ERROR "NS51 target builds and SDK artifacts must stay outside the repository")
endif()
set(NRF_802154_PROJECT_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/radio_config.h")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ns51_phyend_observer C)
target_sources(app PRIVATE control.c transport.c main.c)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
if(NOT CONFIG_NRF_802154_SOURCE_NRFXLIB OR NOT CONFIG_NRF_802154_SL_OPENSOURCE
   OR CONFIG_MPSL OR CONFIG_BT OR CONFIG_NETWORKING OR CONFIG_ZERO_LATENCY_IRQS
   OR CONFIG_FLASH OR CONFIG_NRFX_NVMC OR CONFIG_BOOTLOADER_MCUBOOT
   OR CONFIG_NFCT_PINS_AS_GPIOS OR CONFIG_NRF_APPROTECT_LOCK
   OR CONFIG_NRF_CC3XX_PLATFORM OR CONFIG_ENTROPY_CC3XX
   OR CONFIG_RESET_ON_FATAL_ERROR)
  message(FATAL_ERROR "NS51 requires the reviewed source-only, no-writer profile")
endif()
