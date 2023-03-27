#include "logger.hpp"
#include <bits/types/time_t.h>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>


Logger::Logger()
{
    fileHandle.exceptions(std::ofstream::failbit | std::ofstream::badbit);
    fileHandle.open(logFileName, std::ios::app);  // create file if doesn't exist
    fileHandle.close();
    processingThread = std::thread(processLogRequests);
    #ifndef NDEBUG
    Logger::logToStdout("Logger created");
    #endif
}

Logger::~Logger()
{
    keepProcessing = false;
    Logger::instance().logInfo("logger shutdown");  // TODO called to wake up processThread to exit main loop, think of something better
    processingThread.join();
    try
    {
        fileHandle.open(logFileName, std::ios::app);
        while (auto task = logQueue.try_pop())  // finish logging unprocessed tasks after thread shutdown
        {
            const std::string logMsg = getLogStr(*task);
            Logger::instance().fileHandle.write(logMsg.data(), std::size(logMsg));
        }
        fileHandle.close();
    } catch (std::exception& e) {
        #ifndef NDEBUG
        logToStdout(std::string("Error when writin log file: ") + e.what());
        #endif
    }
    #ifndef NDEBUG
    Logger::logToStdout("Logger destroyed");
    #endif
}

void Logger::processLogRequests() noexcept
{
    while(Logger::instance().keepProcessing)
    {
        LogTask task;
        Logger::instance().logQueue.wait_pop(task);
        const std::string logMsg = getLogStr(task);
        try
        {
            Logger::instance().fileHandle.open(logFileName, std::ios::app);
            Logger::instance().fileHandle.write(logMsg.data(), std::size(logMsg));
            Logger::instance().fileHandle.close();
        } catch (std::exception& e) {
            #ifndef NDEBUG
            logToStdout(std::string("Error when writin log file: ") + e.what());
            #endif
        }
    }
}

std::string Logger::getLogStr(LogTask task) noexcept
{
    std::ostringstream ss;
    ss << getCurrentTimeStr(task.time) << separator << PROJECT_NAME << separator << levelNames.at(task.level) << separator << task.msg << '\n';
    return ss.str();
}

std::string Logger::getCurrentTimeStr(time_t currTime) noexcept
{
    auto tm = *std::localtime(&currTime);
    char timeString[std::size("yyyy-mm-ddThh:mm:ssZ")];
    std::strftime(std::data(timeString), std::size(timeString), "%FT%TZ", &tm);
    return timeString;
}

Logger& Logger::instance()
{
    static Logger instance;
    return instance;
}

void Logger::logMessage(LogLevel level, const std::string& msg) noexcept
{
    if (shouldLogLevel(level))
    {
        LogTask task{level, msg, std::time(nullptr)};
        logQueue.push(task);
    }
}

void Logger::logError(const std::string& msg) noexcept
{
    logMessage(LogLevel::ERROR, msg);
}

void Logger::logWarning(const std::string& msg) noexcept
{
    logMessage(LogLevel::WARNING, msg);
}

void Logger::logInfo(const std::string& msg) noexcept
{
    logMessage(LogLevel::INFO, msg);
}

void Logger::logDebug(const std::string& msg) noexcept
{
    logMessage(LogLevel::DEBUG, msg);
}
