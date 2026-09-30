# SPDX-License-Identifier: BSD-3-Clause
idf_component_register(SRCS "lwext4_example_main.c"
                       PRIV_REQUIRES lwext4 esp_partition esp_timer sdmmc
                                     esp_driver_sdmmc esp_driver_sdspi
                                     esp_driver_spi)
