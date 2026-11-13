/**
 * @file    gpu/log.h
 * @brief   Log category for the native renderer.
 *
 * Added to the logger with the others in RenutApp::OnConfigure (kRenutGpuLog).
 */
#pragma once

#include <rex/logging/log_category.h>
#include <rex/logging/logger.h>

REXLOG_CATEGORY(renut_gpu, "reNut renderer")
REXLOG_TABLE(kRenutGpuLog, renut_gpu)

#define RGPU_TRACE(...) REXLOG_CAT_TRACE(::rex::log::renut_gpu, __VA_ARGS__)
#define RGPU_DEBUG(...) REXLOG_CAT_DEBUG(::rex::log::renut_gpu, __VA_ARGS__)
#define RGPU_INFO(...) REXLOG_CAT_INFO(::rex::log::renut_gpu, __VA_ARGS__)
#define RGPU_WARN(...) REXLOG_CAT_WARN(::rex::log::renut_gpu, __VA_ARGS__)
#define RGPU_ERROR(...) REXLOG_CAT_ERROR(::rex::log::renut_gpu, __VA_ARGS__)
