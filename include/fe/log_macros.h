#pragma once

#include "fe/log.h"

/// @name Logging Macros
/// Shorthands for the fe::Log::Level%s that expect a `log()` in scope.
/// They are in a header of their own, so including fe/log.h does not drag these macros in.
///@{
// clang-format off
#define ELOG(...) log().log(fe::Log::Level::Error,   __FILE__, __LINE__, __VA_ARGS__)
#define WLOG(...) log().log(fe::Log::Level::Warn,    __FILE__, __LINE__, __VA_ARGS__)
#define ILOG(...) log().log(fe::Log::Level::Info,    __FILE__, __LINE__, __VA_ARGS__)
#define VLOG(...) log().log(fe::Log::Level::Verbose, __FILE__, __LINE__, __VA_ARGS__)
/// Vaporizes to nothingness in `Release` build.
#ifndef NDEBUG
#define DLOG(...) log().log(fe::Log::Level::Debug,   __FILE__, __LINE__, __VA_ARGS__)
#define TLOG(...) log().log(fe::Log::Level::Trace,   __FILE__, __LINE__, __VA_ARGS__)
#else
#define DLOG(...) log()
#define TLOG(...) log()
#endif
// clang-format on
///@}
