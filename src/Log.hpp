#pragma once

//#define SPDLOG_ENABLE_SYSLOG
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/async.h"

struct Log {
    Log()
    {
//        spdlog::set_async_mode(4096);
        spdlog::set_level(spdlog::level::info);
        logger_ = spdlog::basic_logger_st<spdlog::async_factory>("logger", "log.txt");
//    logger_ = spdlog::syslog_logger("logger", "chat", LOG_PID);
//    logger_ = spdlog::stdout_color_mt("logger");
    }
    void log( const char *format, ... )
    {
        va_list args;
        char msg[1024];
        va_start( args, format );
        vsnprintf( msg, sizeof msg, format, args );
        va_end( args );

        logger_->info( msg );
        logger_->flush_on(spdlog::level::info);
    }
    static Log& instance() {
        static Log log;
        return log;
    }
    std::shared_ptr<spdlog::logger> logger_;
};
#define LL Log::instance().log
