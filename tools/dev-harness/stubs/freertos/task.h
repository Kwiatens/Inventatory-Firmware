// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once
#include "FreeRTOS.h"
inline BaseType_t xTaskCreatePinnedToCore(void (*)(void*), const char*, uint32_t, void*, int, TaskHandle_t*, int) { return 1; }
inline void vTaskDelay(TickType_t) {}
inline void vTaskDelete(TaskHandle_t) {}
