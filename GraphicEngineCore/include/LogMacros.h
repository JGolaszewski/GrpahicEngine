#pragma once

#ifndef ENGINE_ENABLE_LOGGING
#define ENGINE_ENABLE_LOGGING 1
#endif
#ifndef ENGINE_LOG_LEVEL_MIN
#define ENGINE_LOG_LEVEL_MIN 0
#endif

#if ENGINE_ENABLE_LOGGING
#include "LogEngine.h"

#define ENGINE_DETAIL_LOG(level, category, format, ...) \
    ::Engine::Log::Submit(level, category, format, std::source_location::current() __VA_OPT__(, ) __VA_ARGS__)

#if ENGINE_LOG_LEVEL_MIN <= 0
#define ENGINE_LOG_TRACE(category, format, ...) \
    ENGINE_DETAIL_LOG(::Engine::Log::Level::Trace, category, format __VA_OPT__(, ) __VA_ARGS__)
#else
#define ENGINE_LOG_TRACE(...) ((void)0)
#endif
#if ENGINE_LOG_LEVEL_MIN <= 1
#define ENGINE_LOG_DEBUG(category, format, ...) \
    ENGINE_DETAIL_LOG(::Engine::Log::Level::Debug, category, format __VA_OPT__(, ) __VA_ARGS__)
#else
#define ENGINE_LOG_DEBUG(...) ((void)0)
#endif
#if ENGINE_LOG_LEVEL_MIN <= 2
#define ENGINE_LOG_INFO(category, format, ...) \
    ENGINE_DETAIL_LOG(::Engine::Log::Level::Info, category, format __VA_OPT__(, ) __VA_ARGS__)
#else
#define ENGINE_LOG_INFO(...) ((void)0)
#endif
#if ENGINE_LOG_LEVEL_MIN <= 3
#define ENGINE_LOG_WARN(category, format, ...) \
    ENGINE_DETAIL_LOG(::Engine::Log::Level::Warning, category, format __VA_OPT__(, ) __VA_ARGS__)
#else
#define ENGINE_LOG_WARN(...) ((void)0)
#endif
#if ENGINE_LOG_LEVEL_MIN <= 4
#define ENGINE_LOG_ERROR(category, format, ...) \
    ENGINE_DETAIL_LOG(::Engine::Log::Level::Error, category, format __VA_OPT__(, ) __VA_ARGS__)
#else
#define ENGINE_LOG_ERROR(...) ((void)0)
#endif
#if ENGINE_LOG_LEVEL_MIN <= 5
#define ENGINE_LOG_FATAL(category, format, ...) \
    ENGINE_DETAIL_LOG(::Engine::Log::Level::Fatal, category, format __VA_OPT__(, ) __VA_ARGS__)
#else
#define ENGINE_LOG_FATAL(...) ((void)0)
#endif
#else
#define ENGINE_LOG_TRACE(...) ((void)0)
#define ENGINE_LOG_DEBUG(...) ((void)0)
#define ENGINE_LOG_INFO(...) ((void)0)
#define ENGINE_LOG_WARN(...) ((void)0)
#define ENGINE_LOG_ERROR(...) ((void)0)
#define ENGINE_LOG_FATAL(...) ((void)0)
#endif
