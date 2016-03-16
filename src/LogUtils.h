// LogUtils.h
#include "Poco/LogStream.h"

/// Convenience macros for Poco Logging

/// Define a static function that holds the logger with the specified name. 
/// Add this to the cpp file where you want to use LOG_XXX macros 
#define LOG_DEFINE_MODULE_DEFAULT(name) static Poco::Logger& logger() { static Poco::Logger& logger = Poco::Logger::get(name); return logger; } \
static Poco::LogStream lstrm(logger());

/// you can set the max log level by defining LOG_MAX_LEVEL. 
/// Define ENABLE_DEBUG_LOG_IN_RELEASE to have DEBUG and TRACE log level in release build
#define LOG_MAX_LEVEL Poco::Message::PRIO_DEBUG
#ifndef LOG_MAX_LEVEL
    #if defined(_DEBUG) || defined(ENABLE_DEBUG_LOG_IN_RELEASE)
        #define LOG_MAX_LEVEL Poco::Message::PRIO_TRACE
    #else
        #define LOG_MAX_LEVEL Poco::Message::PRIO_INFORMATION
    #endif
#endif

/// Convenience macros to get a logger stream. 
/// Use like: LOG_INFO << "my log message var1=" << var1 << " var2=" << var2;
/// A function Poco::Logger& logger() must be available in the current context. 
/// Use LOG_DEFINE_MODULE_DEFAULT("Category.Subcategory");
#define LOG_FATAL \
   if(Poco::Message::PRIO_FATAL > LOG_MAX_LEVEL) ; \
   else if (!(logger()).fatal()) ; \
    else lstrm.fatal()

#define LOG_CRITICAL \
   if(Poco::Message::PRIO_CRITICAL > LOG_MAX_LEVEL) ; \
   else if (!(logger()).critical()) ; \
    else lstrm.critical()

#define LOG_ERROR \
   if(Poco::Message::PRIO_ERROR > LOG_MAX_LEVEL) ; \
   else if (!(logger()).error()) ; \
    else lstrm.error()

#define LOG_WARN \
   if(Poco::Message::PRIO_WARNING > LOG_MAX_LEVEL) ; \
   else if (!(logger()).warning()) ; \
    else lstrm.warning()

#define LOG_NOTICE \
   if(Poco::Message::PRIO_NOTICE > LOG_MAX_LEVEL) ; \
   else if (!(logger()).notice()) ; \
    else lstrm.notice()

#define LOG_INFO \
   if(Poco::Message::PRIO_INFORMATION > LOG_MAX_LEVEL) ; \
   else if (!(logger()).information()) ; \
    else lstrm.information()

#define LOG_DEBUG \
    if(Poco::Message::PRIO_DEBUG > LOG_MAX_LEVEL) ; \
   else if (!(logger()).debug()) ; \
    else lstrm.debug()

#define LOG_TRACE \
   if(Poco::Message::PRIO_TRACE > LOG_MAX_LEVEL) ; \
   else if (!(logger()).trace()) ; \
    else lstrm.trace()


