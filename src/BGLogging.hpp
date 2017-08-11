// Copyright 2011-2017 Carnegie Mellon University.  See LICENSE file for terms.

#ifndef __BGLOGGING_H__
#define __BGLOGGING_H__

#include <sstream>
#include <string>
#include <stdio.h>
#include <sys/time.h>
#include <boost/thread.hpp>

#ifndef LOGGER_DEFAULT_LEVEL
#define LOGGER_DEFAULT_LEVEL INFO
#endif

#ifndef LOGGER_DEFAULT_TIMESTAMP
#define LOGGER_DEFAULT_TIMESTAMP "[%Y-%m-%d %H:%M:%S"
#endif


enum BGLogLevel {TRACE=0, DEBUG, INFO, WARNING, ERROR};

template <typename T>
class BGLog
{
public:
    BGLog();
    virtual ~BGLog();
    std::ostringstream& Get(BGLogLevel level = INFO);
public:
    static BGLogLevel& LoggerLevel();
    static std::string& ProcessName();
    static std::string ToString(BGLogLevel level);
    static BGLogLevel FromString(const std::string& level);
    static std::string getTimeString();
protected:
    std::ostringstream os;
private:
    BGLog(const BGLog&);
    BGLog& operator =(const BGLog&);
};

template <typename T>
BGLog<T>::BGLog()
{
}

template <typename T>
std::ostringstream& BGLog<T>::Get(BGLogLevel level)
{
    os << getTimeString();
    os << " " << ProcessName();
    os << " " << ToString(level);
    os << " (" << boost::this_thread::get_id() << ") ";
    return os;
}

template <typename T>
BGLog<T>::~BGLog()
{
    os << std::endl;
    T::Output(os.str());
}
template <typename T>
BGLogLevel& BGLog<T>::LoggerLevel()
{
    static BGLogLevel reportingLevel = LOGGER_DEFAULT_LEVEL;
    return reportingLevel;
}

template <typename T>
std::string& BGLog<T>::ProcessName()
{
    static std::string processName = "biggrep";
    return processName;
}

template <typename T>
std::string BGLog<T>::ToString(BGLogLevel level)
{
    static const char* const buffer[] = {"<trace>", "<debug>", "<info>", "<warn>",
                                         "<error>"};
    return buffer[level];
}
template <typename T>
std::string BGLog<T>::getTimeString()
{
    char timestamp[256];
    timeval curTime;
    tm r = {0};
    gettimeofday(&curTime, NULL);
    int milli = curTime.tv_usec/1000;

    strftime(timestamp, sizeof(timestamp), LOGGER_DEFAULT_TIMESTAMP,
             localtime_r(&curTime.tv_sec, &r));
    char result[256];
    sprintf(result, "%s:%d]", timestamp, milli);

    return std::string(result);
}

template <typename T>
BGLogLevel BGLog<T>::FromString(const std::string& level)
{
    if (level == "TRACE")
        return TRACE;
    if (level == "DEBUG")
        return DEBUG;
    if (level == "INFO")
        return INFO;
    if (level == "WARNING")
        return WARNING;
    if (level == "ERROR")
        return ERROR;
    BGLog<T>().Get(WARNING) << "Unknown logging level '"
                            << level << "'. Using INFO level as default.";
    return INFO;
}

class BGLog2File
{
public:
    static FILE*& Stream();
    static void Output(const std::string& msg);

};

inline FILE*& BGLog2File::Stream()
{
    static FILE* pStream = stderr;
    return pStream;
}

inline void BGLog2File::Output(const std::string& msg)
{
    FILE* pStream = Stream();
    if (!pStream)
        return;
    fprintf(pStream, "%s", msg.c_str());
    fflush(pStream);
}


typedef BGLog<BGLog2File> BGLogger;

#define LNOOP

/*#define BGLOG(level) (level < BGLogger::bgLogger()->getLoggerLevel(__FILE__,__LINE__)) ? LNOOP : BGLogger::bgLogger()->log(level)
  #define LOGcnt(level) (level < BGLogger::bgLogger()->getLoggerLevel(__FILE__,__LINE__)) ? LNOOP : BGLogger::bgLogger()->log(level, false)*/

#define BGLOG(level) \
    if (level < BGLogger::LoggerLevel()); \
    else BGLogger().Get(level)

#define BGTRACE BGLOG(TRACE)<< "(" << __LINE__ << ":" << __FUNCTION__ << "): "
#define BGDEBUG BGLOG(DEBUG) << "(" << __LINE__ << ":" << __FUNCTION__ << "): "
#define BGINFO BGLOG(INFO) << "(" << __LINE__ << ":" << __FUNCTION__ << "): "
#define BGWARN BGLOG(WARNING) << ": "
#define BGERR BGLOG(ERROR) << ": "

// more handy "cheating" macros:
#define BGSETINFOLOGLEVEL BGLogger::LoggerLevel() = INFO
#define BGSETDEBUGLOGLEVEL BGLogger::LoggerLevel() = DEBUG
#define BGSETTRACELOGLEVEL BGLogger::LoggerLevel() = TRACE
#define BGSETWARNLOGLEVEL BGLogger::LoggerLevel() = WARNING
#define BGSETERRLOGLEVEL BGLogger::LoggerLevel() = ERROR

#define BGSETPROCESSNAME(x) BGLogger::ProcessName() = x

// this is shorter for the lazy:
#define LEND std::endl

// handy little class for enter/exit messages for functions, etc:

#endif
