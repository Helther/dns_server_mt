#pragma once

#include "queue.hpp"
#include <atomic>
#include <bits/types/time_t.h>
#include <fstream>
#include <string>
#include <memory>
#include <map>
#include <thread>
#include <ctime>
#include <iostream>


enum class LogLevel
{
    WARNING = 0,
    ERROR,
    INFO,
    DEBUG
};

inline const std::map<LogLevel, std::string> levelNames = {
    {LogLevel::WARNING, "WARNING"},
    {LogLevel::ERROR, "ERROR"},
    {LogLevel::INFO, "INFO"},
    {LogLevel::DEBUG, "DEBUG"}
};


struct LogTask
{
    LogLevel level;
    std::string msg;
    time_t time;
};


/*
    File logger singleton class
    Lazy initializes via instance() call and starts a dedicated processing thread
    for writing log entries in a thread safe queue
    thread then joins and finishes left over tasks in destructor
*/
class Logger
{
    // open file and set handle, start processing thread for log requests
    Logger();

    bool shouldLogLevel(LogLevel level) const noexcept { return level <= this->level; }
    // wait for a task and write an entry to the file
    static void processLogRequests() noexcept;
    static std::string getLogStr(LogTask task) noexcept;
    static std::string getCurrentTimeStr(time_t currTime) noexcept;
    
    std::ofstream fileHandle;
    LogLevel level = LogLevel::DEBUG;
    static constexpr auto logFileName = PROJECT_LOG_NAME;
    static constexpr auto separator = " - ";
    ThreadSafeQueue<LogTask> logQueue;
    std::thread processingThread;
    std::atomic_bool keepProcessing{true};

public:
    // wait for processing thread to finish current queue and exit
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(Logger&) = delete;

    // lazy initialize the instance
    static Logger& instance();  // <- access through this
    static void logToStdout(const std::string& msg) noexcept;
    void setLevel(LogLevel level) noexcept { this->level = level; }
    LogLevel getLevel() const noexcept { return level; }

    // send log task to the process thread for file logging
    static void logMessage(LogLevel level, const std::string& msg) noexcept;
    static void logError(const std::string& msg) noexcept;
    static void logWarning(const std::string& msg) noexcept;
    static void logInfo(const std::string& msg) noexcept;
    static void logDebug(const std::string& msg) noexcept;

};
