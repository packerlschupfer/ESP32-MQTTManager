/*
 * MQTTManagerLogging.h - part of the ESP32-MQTTManager library
 *
 * Copyright (C) 2025-2026 packerlschupfer
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef MQTTM_LOGGING_H
#define MQTTM_LOGGING_H

#define MQTTM_LOG_TAG "MQTTM"

// Define log levels based on debug flag
#ifdef MQTTMANAGER_DEBUG
    // Debug mode: Show all levels
    #define MQTTM_LOG_LEVEL_E ESP_LOG_ERROR
    #define MQTTM_LOG_LEVEL_W ESP_LOG_WARN
    #define MQTTM_LOG_LEVEL_I ESP_LOG_INFO
    #define MQTTM_LOG_LEVEL_D ESP_LOG_DEBUG
    #define MQTTM_LOG_LEVEL_V ESP_LOG_VERBOSE
#else
    // Release mode: Only Error, Warn, Info
    #define MQTTM_LOG_LEVEL_E ESP_LOG_ERROR
    #define MQTTM_LOG_LEVEL_W ESP_LOG_WARN
    #define MQTTM_LOG_LEVEL_I ESP_LOG_INFO
    #define MQTTM_LOG_LEVEL_D ESP_LOG_NONE  // Suppress
    #define MQTTM_LOG_LEVEL_V ESP_LOG_NONE  // Suppress
#endif

// Route to custom logger or ESP-IDF
#ifdef USE_CUSTOM_LOGGER
    #include <LogInterface.h>
    #define MQTTM_LOG_E(...) LOG_WRITE(MQTTM_LOG_LEVEL_E, MQTTM_LOG_TAG, __VA_ARGS__)
    #define MQTTM_LOG_W(...) LOG_WRITE(MQTTM_LOG_LEVEL_W, MQTTM_LOG_TAG, __VA_ARGS__)
    #define MQTTM_LOG_I(...) LOG_WRITE(MQTTM_LOG_LEVEL_I, MQTTM_LOG_TAG, __VA_ARGS__)
    #define MQTTM_LOG_D(...) LOG_WRITE(MQTTM_LOG_LEVEL_D, MQTTM_LOG_TAG, __VA_ARGS__)
    #define MQTTM_LOG_V(...) LOG_WRITE(MQTTM_LOG_LEVEL_V, MQTTM_LOG_TAG, __VA_ARGS__)
#else
    // ESP-IDF logging with compile-time suppression
    #include <esp_log.h>
    #define MQTTM_LOG_E(...) ESP_LOGE(MQTTM_LOG_TAG, __VA_ARGS__)
    #define MQTTM_LOG_W(...) ESP_LOGW(MQTTM_LOG_TAG, __VA_ARGS__)
    #define MQTTM_LOG_I(...) ESP_LOGI(MQTTM_LOG_TAG, __VA_ARGS__)
    #ifdef MQTTMANAGER_DEBUG
        #define MQTTM_LOG_D(...) ESP_LOGD(MQTTM_LOG_TAG, __VA_ARGS__)
        #define MQTTM_LOG_V(...) ESP_LOGV(MQTTM_LOG_TAG, __VA_ARGS__)
    #else
        #define MQTTM_LOG_D(...) ((void)0)
        #define MQTTM_LOG_V(...) ((void)0)
    #endif
#endif

// Feature-specific debug helpers
#ifdef MQTTMANAGER_DEBUG_CONN
    #define MQTTM_LOG_CONN(...) MQTTM_LOG_D("CONN: " __VA_ARGS__)
#else
    #define MQTTM_LOG_CONN(...) ((void)0)
#endif

// Optional: Message queue debugging
#ifdef MQTTMANAGER_DEBUG_QUEUE
    #define MQTTM_LOG_QUEUE(...) MQTTM_LOG_D("QUEUE: " __VA_ARGS__)
#else
    #define MQTTM_LOG_QUEUE(...) ((void)0)
#endif

#endif // MQTTM_LOGGING_H