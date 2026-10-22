// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once
#include <cstdint>
typedef void* TaskHandle_t; typedef void* SemaphoreHandle_t; typedef int BaseType_t; typedef uint32_t TickType_t;
#define pdPASS 1
#define pdTRUE 1
#define portMAX_DELAY 0xFFFFFFFFu
#define pdMS_TO_TICKS(x) ((TickType_t)(x))
