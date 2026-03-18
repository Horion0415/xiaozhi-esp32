/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_check.h"

#ifdef CONFIG_BSP_ERROR_CHECK
#define BSP_ERROR_CHECK_RETURN_ERR(x) ESP_RETURN_ON_ERROR(x, TAG, "")
#define BSP_ERROR_CHECK_RETURN_NULL(x) do { if ((x) != ESP_OK) return NULL; } while(0)
#define BSP_NULL_CHECK(x, ret) do { if ((x) == NULL) return (ret); } while(0)
#else
#define BSP_ERROR_CHECK_RETURN_ERR(x) do { esp_err_t _e = (x); if (_e != ESP_OK) return _e; } while(0)
#define BSP_ERROR_CHECK_RETURN_NULL(x) do { if ((x) != ESP_OK) return NULL; } while(0)
#define BSP_NULL_CHECK(x, ret) do { if ((x) == NULL) return (ret); } while(0)
#endif
