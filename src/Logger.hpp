// Copyright 2011-2017 Carnegie Mellon University.  See LICENSE file for terms.
//
// File:
//   Logger.hpp
//
// Description:
//   A simple logging interface that uses some tricks described in this article:
//     http://www.ddj.com/cpp/201804215
//       Note, that actually moved here at some point:
//         http://www.drdobbs.com/cpp/logging-in-c/201804215
//       and there was a follow on article:
//         http://www.ddj.com/cpp/221900468
//         http://www.drdobbs.com/cpp/logging-in-c-part-2/221900468
//   to help eliminate overhead when logging levels aren't turned up...
//   Logs to stderr by default, can specify a new ostream to use if need be.
//
//   Use is usually pretty simple:
//     LDEBUG << "debug message" << LEND;
//     LWARNING << "eek" << std::endl;
//     LERROR << "Danger Will Robinson" << LEND;
//     LCRITICAL << "This message will self destruct..." << std::endl;
//
//   If you need to exclude some more extensive processing than what can be
//   placed in a single line stream operation like above, there are some
//   helper macros for that too:
//     if (LISDEBUG) { ... }
//     if (LISLOGLEVEL(Logger.INFO)) { ... }
//
//   Default behavior can be controlled via some env vars (useful for
//   integration into new code that doesn't do cmd line processing).
//   Specifically LOGGER_LEVEL can be set to set the default log level either
//   globally or on a per file basis.  For example:
//     env LOGGER_LEVEL=1 ./yourprogram
//   would enable LDEBUG and above globally, while this:
//     env LOGGER_LEVEL="foo.cpp=0;bar.cpp=1" ./yourprogram
//   would enable ALL logging (LTRACE on up) for anything in foo.cpp, and
//   LDEBUG and above for anything in bar.cpp.  It uses the Boost Tokenizer
//   code for parsing the environment variable.
//
//   There's a LOGGER_TIMESTAMP env var too, to make that go away by setting
//   it to an empty string (for automated unit test comparisons, for example)
//   or change the format from the default for some other reason.
//
//   Now relies on Boost Thread library for limited thread safety.  Still can
//   get garbled output though, since stream usage isn't really thread safe...
//   I could probably make it a little better if I'd have stuck closer to the
//   original article's implementation (as it uses streams internally only),
//   but think I'm okay for now...
//
// Todo:
//   Allow for multiple named loggers, each with its own prefix and current
//   level?  Or does that push this code into the realm of "just go use
//   log4cpp already"?  Perhaps the file specific debug levels are sufficient.
//
//   Might be nice to have an easy way to output a particular message only the
//   first time it gets encountered...
//
//   Revisit implementation of original inspirtation, which might actually be
//   thread safe (as opposed to this implementation which is only partly so).
//

#ifndef __LOGGER_HPP__
#define __LOGGER_HPP__

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <ctime>
#include <cstring>
#include <cstdlib>

#include <boost/thread.hpp>
#include <boost/tokenizer.hpp>
#include <boost/foreach.hpp>

#ifndef LOGGER_LEVEL_ENV_VAR
#define LOGGER_LEVEL_ENV_VAR "LOGGER_LEVEL"
#endif

// set this env var to "" to eliminate the timestamp output:
#ifndef LOGGER_TIMESTAMP_ENV_VAR
#define LOGGER_TIMESTAMP_ENV_VAR "LOGGER_TIMESTAMP"
#endif

#ifndef LOGGER_DEFAULT_LEVEL
#define LOGGER_DEFAULT_LEVEL MSG
#endif

#ifndef LOGGER_DEFAULT_TIMESTAMP
#define LOGGER_DEFAULT_TIMESTAMP "%Y%m%d%H%M%S"
#endif



class Logger {
public:
  typedef enum { TRACE=0, DEBUG, INFO, MSG, WARNING, ERROR, CRITICAL, NOLOG, FORCELOG } Levels;

  // TRACE is for VERY verbose debug levels, INFO is for verbose messages,
  // MSG is for normal messages (default level), NOLOG shouldn't be used in
  // code (it's a level to turn off "all" logging), use FORCELOG priority to
  // override NOLOG for things that you always want to get out regardless...

  std::ostream&
  log(Levels level, bool showprefix=true)
  {
    // should check for level value, or will enum handle that for us?
    static const char* const LevelStrings[] = { "(T)", "(D)", "(I)", "(M)", "(W)", "(E)", "(C)", "(?)", "(F)" };

    // should probably lock here for some minimal thread safety:
    //boost::lock_guard<boost::mutex> guard(mtx);
    // but the various getters below will lock the mutex, so this would
    // deadlock here...so need to be more granular...

    // would really like to only output this stuff if last output was a
    // newline or something like that, but for now the extra macros that alter
    // showprefix will have todo:
    if (showprefix)
    {
      std::string ts(getTimeString());
      std::string tid(getThisThreadId());
      // should probably lock here for some minimal thread safety while
      // writing to the stream:
      boost::lock_guard<boost::mutex> guard(mtx);
      (*theStream) << theLogPrefix
                   << ts
                   << tid
                   << LevelStrings[level] << ": ";
    }
    return (*theStream);
  }

  void
  setLoggerStream(std::ostream &s)
  {
    theStream = &s;
  }

  void
  setLoggerLevel(Levels level)
  {
    theLogLevel = level;
  }

  Levels
  getLoggerLevel(void)
  {
    return theLogLevel;
  }

  Levels
  getLoggerLevel(const char* fname, int lineno = -1) // lineno currently unused...
  {
    int llevel(theLogLevel);
    if (fname && fname[0] != 0)
    {
      // look in internal list to see if override for current file set, return
      // the lower of the two
      if (fileOverrideLevels.count(fname) > 0)
      {
        llevel = std::min(llevel,(int)fileOverrideLevels[fname]);
      }
    }
    // Just to shut the compiler up about the unusued parameter.
    if (lineno == -1)
      return (Levels)llevel;
    else
      return (Levels)llevel;
  }

  void
  setLoggerPrefix(std::string& pfx)
  {
    boost::lock_guard<boost::mutex> guard(mtx);
    theLogPrefix = pfx;
    //if (theLogPrefix.back() != ' ') // apparently the back() method isn't in g++ 4.4.x, might be a C++11 addition?
    if (theLogPrefix.size() > 0 && theLogPrefix[theLogPrefix.size()-1] != ' ')
    {
      theLogPrefix += " ";
    }
  }

  void
  setLoggerPrefix(const char* pfx)
  {
    std::string npfx(pfx);
    setLoggerPrefix(npfx);
  }

  void
  setThisThreadId(std::string ts)
  {
    boost::lock_guard<boost::mutex> guard(mtx);
    // associate the thread id of the current thread with the provided string...
    boost::thread::id tid(boost::this_thread::get_id());
    threadIdMap[tid] = ts;
  }

  void
  setThisThreadId(const char *ts)
  {
    setThisThreadId(std::string(ts));
  }

  std::string
  getThisThreadId()
  {
    boost::lock_guard<boost::mutex> guard(mtx);
    std::string retval;
    boost::thread::id tid(boost::this_thread::get_id());
    if (threadIdMap.count(tid) > 0 && threadIdMap[tid] != "")
    {
      retval = "[" + threadIdMap[tid] + "] ";
    }
    return retval;
  }

  void
  setLoggerTimestamp(std::string& ts)
  {
    boost::lock_guard<boost::mutex> guard(mtx);
    theLogTimestamp = ts;
    if (theLogTimestamp.size() > 0 && theLogTimestamp[theLogTimestamp.size()-1] != ' ')
    {
      theLogTimestamp += " ";
    }
  }

  void
  setLoggerTimestamp(const char* ts)
  {
    std::string nts;
    if (ts)
    {
      nts = ts;
    }
    setLoggerTimestamp(nts);
  }

  std::string
  getTimeString()
  {
    boost::lock_guard<boost::mutex> guard(mtx);
    char timestamp[256];
    timestamp[0] = 0;
    if (theLogTimestamp != "")
    {
      time_t t;
      struct tm *tmp;
      t = std::time(NULL);
      tmp = std::localtime(&t);
      if (tmp == NULL || strftime(timestamp, sizeof(timestamp), theLogTimestamp.c_str(), tmp) == 0)
      {
        // should never get here
        std::strcpy(timestamp,"TIMERROR?");
      }
    }
    return std::string(timestamp);
  }

  // logger is a pseudo-singleton accessor...
  static
  Logger*
  logger(void)
  {
    static Logger *theLogger = NULL;
    static boost::mutex initmtx;
    if (theLogger == NULL)
    {
      // for thread safety, should double pump: lock mutex, check again
      boost::lock_guard<boost::mutex> guard(initmtx);
      if (theLogger == NULL)
      {
        theLogger = new Logger();
      }
    }
    return theLogger;
  }

private:
  // since this is effectively a singleton, these member vars are all
  // effectively static:
  Levels theLogLevel;
  std::ostream* theStream;
  //static std::vector< std::string > LevelStrings;
  std::string theLogPrefix;
  std::string theLogTimestamp;

  std::map< boost::thread::id, std::string > threadIdMap;

  std::map< std::string, Levels > fileOverrideLevels;

  boost::mutex mtx;

  // private CTOR to help enforce singleton behavior
  Logger(): theStream(&std::cerr)
  {
    setLoggerLevel(LOGGER_DEFAULT_LEVEL);
    setLoggerTimestamp(LOGGER_DEFAULT_TIMESTAMP);

    char *lle(std::getenv(LOGGER_LEVEL_ENV_VAR));
    std::string loglvlenv;
    if (lle)
    {
      loglvlenv = lle;
    }
    if (loglvlenv != "")
    {
      // okay, it can be a single integer value for the global level, or
      // semicolon delimited file=level pairs (and DEFAULT as a file will also
      // set global level, as will just an integer)
      if (loglvlenv.size() == 1)
      {
        int lenv(std::atoi(loglvlenv.c_str()));
        if (lenv >= TRACE && lenv <= FORCELOG)
        {
          setLoggerLevel((Levels)lenv);
        }
      }
      else
      {
        // can look like: logtest.cpp=1;DEFAULT=2;
        boost::char_separator<char> sep(",;");
        boost::tokenizer<boost::char_separator<char> > tokens(loglvlenv,sep);
        //for (boost::tokenizer::iterator tok_iter tokens.begin();
        //     tok_iter != tokens.end();
        //     ++tok_iter)
        BOOST_FOREACH(std::string tok, tokens)
        {
          int lval;
          if (tok.size() == 1)
          {
            lval = std::atoi(tok.c_str());
            if (lval >= TRACE && lval <= FORCELOG)
            {
              setLoggerLevel((Levels)lval);
            }
          }
          else
          {
            boost::char_separator<char> sep2("=");
            boost::tokenizer<boost::char_separator<char> > tokens2(tok,sep2);
            // better be 2 tokens!  No size method though, and no distance_to
            // so iterator math didn't work either:
            boost::tokenizer<boost::char_separator<char> >::iterator tok_iter(tokens2.begin());
            std::string fname(*tok_iter);
            ++tok_iter;
            lval = std::atoi((*tok_iter).c_str());
            if (lval >= TRACE && lval <= FORCELOG)
            {
              if (fname == "DEFAULT")
              {
                setLoggerLevel((Levels)lval);
              }
              else
              {
                fileOverrideLevels[fname] = (Levels)lval;
              }
            }
          }
        }
      }
    }
    char *logtsenv(std::getenv(LOGGER_TIMESTAMP_ENV_VAR));
    if (logtsenv)
    {
      setLoggerTimestamp(logtsenv);
    }
  }

};

// macro version, usage like so: LOG(Logger::WARNING) << "Hm...something looks suspicious";
#if 0
#define LOG(level) \
  if (level < Logger::logger()->getLoggerLevel()) ; \
  else Logger::logger()->log(level)

#define LOGcnt(level) \
  if (level < Logger::logger()->getLoggerLevel()) ; \
  else Logger::logger()->log(level,false)

#else

// modified versions of the above that use the ternary operator instead of
// if/else to avoid compiler warnings about including curly braces to avoid
// ambiguous else statements when the logging macros are used in single line
// if statements without curly braces themselves...

// These variants also use __FILE__ and __LINE__ to allow for selective
// debugging control.

// Note that the empty true term might not be technically legal C++, just a
// gcc extension maybe (false seemed to work as well?), but apparently (void)0
// is a valid 'do nothing' expression, but it just doesn't work in the ternary
// operator here because it doesn't return the same type (void) as the else
// expression; might be able to replace with a call to an empty lambda
// expression in C++11, which looks like "[](){}()", wow that looks weird,
// although it would probably have to be defined as returning the same type as
// the else parameter too to work, not void like I have here, and since log
// returns a std::ostream&, that's probably not going to work either, so I'll
// just stick with the empty expression for now:
//#define LNOOP
//#define LNOOP (void)0
//#define LNOOP [](){}()

//#define LOG(level) (level < Logger::logger()->getLoggerLevel(__FILE__,__LINE__)) ? LNOOP : Logger::logger()->log(level)
//#define LOGcnt(level) (level < Logger::logger()->getLoggerLevel(__FILE__,__LINE__)) ? LNOOP : Logger::logger()->log(level,false)
// actually, short circuits might work out for this:
#define LOG(level) (level >= Logger::logger()->getLoggerLevel(__FILE__,__LINE__)) && Logger::logger()->log(level)
#define LOGcnt(level) (level >= Logger::logger()->getLoggerLevel(__FILE__,__LINE__)) && Logger::logger()->log(level,false)

#endif // 0


// Much more convenient macros (to use like LDEBUG << "my dbg msg";):
#define LTRACE LOG(Logger::TRACE)
#define LDEBUG LOG(Logger::DEBUG)
#define LINFO LOG(Logger::INFO)
#define LMSG LOG(Logger::MSG)
#define LMESSAGE LOG(Logger::MSG)
#define LWARN LOG(Logger::WARNING)
#define LWARNING LOG(Logger::WARNING)
#define LERR LOG(Logger::ERROR)
#define LERROR LOG(Logger::ERROR)
#define LCRIT LOG(Logger::CRITICAL)
#define LCRITICAL LOG(Logger::CRITICAL)
#define LFORCE LOG(Logger::FORCELOG)

// have some instances where we'd like to not insert the prefix (like in a loop):
#define LTRACEcnt LOGcnt(Logger::TRACE)
#define LDEBUGcnt LOGcnt(Logger::DEBUG)
#define LINFOcnt LOGcnt(Logger::INFO)
#define LMSGcnt LOGcnt(Logger::MSG)
#define LMESSAGEcnt LOGcnt(Logger::MSG)
#define LWARNcnt LOGcnt(Logger::WARNING)
#define LWARNINGcnt LOGcnt(Logger::WARNING)
#define LERRcnt LOGcnt(Logger::ERROR)
#define LERRORcnt LOGcnt(Logger::ERROR)
#define LCRITcnt LOGcnt(Logger::CRITICAL)
#define LCRITICALcnt LOGcnt(Logger::CRITICAL)
#define LFORCEcnt LOGcnt(Logger::FORCE)

// more handy "cheating" macros:
#define LSETLOGLEVEL(x) Logger::logger()->setLoggerLevel(x)
#define LGETLOGLEVEL Logger::logger()->getLoggerLevel(__FILE__,__LINE__)
// if (LISLOGLEVEL(Logger::DEBUG)) { ... }
#define LISLOGLEVEL(x) LGETLOGLEVEL <= x
#define LISDEBUG LISLOGLEVEL(Logger::DEBUG)

#define LSETLOGSTREAM(x) Logger::logger()->setLoggerStream(x)
#define LSETLOGPREFIX(x) Logger::logger()->setLoggerPrefix(x)

#define LSETLOGTIMESTAMP(x) Logger::logger()->setLoggerTimestamp(x)
#define LNOTIMESTAMP LSETLOGTIMESTAMP("")

#define LSETTHISTHREADID(x) Logger::logger()->setThisThreadId(x)

// this is shorter for the lazy:
#define LEND std::endl

// handy little class for enter/exit messages for functions, etc:
class ScopedLogger {
public:
  ScopedLogger(std::string s,Logger::Levels l=Logger::DEBUG): lvl(l)
  {
    enter_msg = "scoped start: " + s;
    exit_msg = "scoped stop: " + s;
    LOG(lvl) << enter_msg << std::endl;
  }
  ~ScopedLogger()
  {
    LOG(lvl) << exit_msg << std::endl;
  }
  Logger::Levels lvl;
  std::string enter_msg;
  std::string exit_msg;
};

#define Lxstr(s) Lstr(s)
#define Lstr(s) #s

// handy macro for easy adding of ScopedLogger to a function
//#define LLOGFUNC ScopedLogger __func__##_ScopedLogger(__func__)

// an experiment while trying to get line number & file in there too...the
// following works, but __func__ is handled...differently (inserted as a local
// variable in the function, I think), so that doesn't work in here in any way
// other than by itself
//#define LLOGFUNC ScopedLogger __func__##_ScopedLogger(Lxstr(line=__LINE__ file=) __FILE__)

// ah, but since it is a variable, I can probably use temp strings to get the
// behavior I'm looking for, oh, and the use of __func__ in the name above is
// probably wrong too, so FILE & LINE it is::
#define LLOGFUNC ScopedLogger __FILE__##__LINE__##_ScopedLogger(std::string(__FILE__) + ":" + __func__ + "()")

#endif // __LOGGER_HPP__
