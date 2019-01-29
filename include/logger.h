#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <fstream>
#include <list>
#include <locale>
#include <string>
#include <ctime>
#include <boost/log/trivial.hpp>
#include <boost/format.hpp>

#define LogTrace	BOOST_LOG_TRIVIAL(trace)
#define LogDebug	BOOST_LOG_TRIVIAL(debug)
#define LogInfo		BOOST_LOG_TRIVIAL(info)
#define LogWarn		BOOST_LOG_TRIVIAL(warning)
#define LogError	BOOST_LOG_TRIVIAL(error)
#define LogFatal	BOOST_LOG_TRIVIAL(fatal)

#define LogDebugExt	LogDebug << __FILE__ << ",Line " << __LINE__ << ","
#define LogErrorExt LogError << __FILE__ << ",Line " << __LINE__ << ","
#define LogWarnExt LogWarn << __FILE__ << ",Line " << __LINE__ << ","
#define LogFatalExt LogFatal << __FILE__ << ",Line " << __LINE__ << ","


template<typename... Arguments>
void log_trace(std::string const& fmt, Arguments&&... args)
{
   BOOST_LOG_TRIVIAL(trace) << (boost::format(fmt) % ... %std::forward<Arguments>(args));
}

template<typename... Arguments>
void log_trace_ext(std::string const& fmt, Arguments&&... args)
{
   BOOST_LOG_TRIVIAL(trace) << (boost::format(fmt) % ... %std::forward<Arguments>(args));
}

template<typename... Arguments>
void log_debug(std::string const& fmt, Arguments&&... args)
{
   BOOST_LOG_TRIVIAL(debug) << (boost::format(fmt) % ... %std::forward<Arguments>(args));
}

template<typename... Arguments>
void log_info(std::string const& fmt, Arguments&&... args)
{
   BOOST_LOG_TRIVIAL(info) << (boost::format(fmt) % ... %std::forward<Arguments>(args));
}

template<typename... Arguments>
void log_warning(std::string const& fmt, Arguments&&... args)
{
   BOOST_LOG_TRIVIAL(warning) << (boost::format(fmt) % ... %std::forward<Arguments>(args));
}

template<typename... Arguments>
void log_error(std::string const& fmt, Arguments&&... args)
{
   BOOST_LOG_TRIVIAL(error) << (boost::format(fmt) % ... %std::forward<Arguments>(args));
}

template<typename... Arguments>
void log_fatal(std::string const& fmt, Arguments&&... args)
{
   BOOST_LOG_TRIVIAL(fatal) << (boost::format(fmt) % ... %std::forward<Arguments>(args));
}


//注意,异步日志在压力测试时,会因为日志队列导致内存不断增长

//必须先调用
void init_logging(const std::string &log_path, boost::log::trivial::severity_level filter_level);

void add_syslogging(const std::string& syslog_server_ip, int syslog_server_port, boost::log::trivial::severity_level filter_level);

#endif
