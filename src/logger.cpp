#include "logger.hpp"
#include "queue.hpp"
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
    Logger::logToStdout("Logger created");
}

Logger::~Logger()
{
    keepProcessing = false;
    Logger::instance().logInfo("Logger shutdown");  // also called to wake up processThread to exit main loop
    processingThread.join();
    try
    {
        fileHandle.open(logFileName, std::ios::app);
        while (auto task = logQueue.dequeue())  // finish logging unprocessed tasks after thread shutdown
        {
            const std::string logMsg = getLogStr(*task);
            Logger::instance().fileHandle.write(logMsg.data(), std::size(logMsg));
        }
        fileHandle.close();
    } catch (std::exception& e) {
        logToStdout(std::string("Logger Error when writin log file: ") + e.what());
    }
    Logger::logToStdout("Logger destroyed");
}

void Logger::processLogRequests() noexcept
{
    while(Logger::instance().keepProcessing)
    {
        try
        {
            if (std::unique_ptr<LogTask> task = Logger::instance().logQueue.waitDequeue())
            {
                const std::string logMsg = getLogStr(*task);
                Logger::instance().fileHandle.open(logFileName, std::ios::app);
                Logger::instance().fileHandle.write(logMsg.data(), std::size(logMsg));
                Logger::instance().fileHandle.close();
            }
        } catch (std::exception& e) {
            logToStdout(std::string("Logger Error when writin log file: ") + e.what());
        }
    }
}

std::string Logger::getLogStr(const LogTask& task) noexcept
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

void Logger::logToStdout(const std::string &msg) noexcept
{
    #ifndef NDEBUG
    std::cout << getLogStr({LogLevel::DEBUG, msg, std::time(nullptr)}) << std::endl;
    #endif
}

void Logger::logMessage(LogLevel level, const std::string& msg) noexcept
{
    if (instance().shouldLogLevel(level))
    {
        LogTask task{level, msg, std::time(nullptr)};
        try
        {
            instance().logQueue.enqueue(std::move(task));
        } catch (std::exception& e) {
            logToStdout(std::string("Logger Error adding log message: ") + e.what());
        }
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

void Logger::logTask(LogTask task) noexcept
{
    if (instance().shouldLogLevel(task.level))
    {
        try
        {
            instance().logQueue.enqueue(std::move(task));
        } catch (std::exception& e) {
            logToStdout(std::string("Logger Error adding log message: ") + e.what());
        }
    }
}
