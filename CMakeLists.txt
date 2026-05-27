# SPDX-FileCopyrightText: 2025-2026 Evgenij Cjura and project contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# zhac-mono-core — single-chip ZHAC firmware (ESP32-S3 only).
# See PLAN.md for architectural intent. Sibling repos (zhac-main-core,
# zhac-net-core, zhac-components, embedded-zhc, www-spa) are read-only
# inputs here; the bridge shim under components/ overrides hap_master
# and hap_slave so the SPI-bridge symbols become local function calls.

cmake_minimum_required(VERSION 3.22)

# Local components/ wins by ESP-IDF's "first match by name" rule, so
# our hap_master / hap_slave shims under zhac-mono-core/components/
# transparently replace the sibling implementations even when those
# sibling dirs are also on EXTRA_COMPONENT_DIRS.
#
# We add all three sibling component dirs to the search path. ESP-IDF
# only *builds* components transitively reachable from main's REQUIRES,
# so unused components (e.g. ezsp_*, tg_gw in Phase 1) stay out of the
# graph until later phases wire them in.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../zhac-components/components"
    "${CMAKE_CURRENT_LIST_DIR}/../zhac-net-core/components"
    "${CMAKE_CURRENT_LIST_DIR}/../zhac-main-core/components"
)

# embedded-zhc is the host-testable C++20 device library; firmwares
# resolve it via EMBEDDED_ZHC_PATH (zhc_adapter's CMakeLists checks
# this var). The platform justfile sets it; here we default to the
# sibling repo so a manual `idf.py build` from this dir works.
if(NOT DEFINED ENV{EMBEDDED_ZHC_PATH})
    set(ENV{EMBEDDED_ZHC_PATH} "${CMAKE_CURRENT_LIST_DIR}/../embedded-zhc")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(zhac-mono-core)

# SPA → "spa" SPIFFS partition image, built from the sibling www-spa output
# (run `npm run build` in www-spa first). FLASH_IN_PROJECT so `idf.py flash`
# writes it. Guarded so the firmware still builds (empty spa partition) when
# dist/ is absent — reflash with `idf.py flash` once the SPA is built.
set(ZHAC_SPA_DIST "${CMAKE_CURRENT_LIST_DIR}/../www-spa/dist")
if(EXISTS "${ZHAC_SPA_DIST}/index.html")
    spiffs_create_partition_image(spa "${ZHAC_SPA_DIST}" FLASH_IN_PROJECT)
    message(STATUS "ZHAC: SPA image <- ${ZHAC_SPA_DIST}")
else()
    message(WARNING "ZHAC: ${ZHAC_SPA_DIST}/index.html not found — 'spa' partition "
                    "will be empty. Build www-spa (npm run build), then reflash.")
endif()
