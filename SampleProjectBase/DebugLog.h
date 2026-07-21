#pragma once

#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <ctime>

/// <summary>
/// デバッグログ出力クラス C++17以上で動作
/// ログメッセージの例
/// DebugLog::log(DebugLog::INFO_LOG, "User information: Name = ", name, ", Age = ", age, ", Height = ", height);
/// DebugLog::log(DebugLog::WARNING_LOG, "Warning: Height is too low.");
/// DebugLog::log(DebugLog::ERROR_LOG, "Error: Invalid user data.");
/// </summary>

class DebugLog
{
	DebugLog();
	~DebugLog();
public:

	enum LogType {
		INFO_LOG	= 0,
		WARNING_LOG	= 1,
		ERROR_LOG	= 2,
	};

	// ログの種類を文字列に変換するヘルパー関数
	static const char* LogTypeToString(LogType type) {
		switch (type) {
		case INFO_LOG: return		"INFO";
		case WARNING_LOG: return	"WARNING";
		case ERROR_LOG: return		"ERROR";
		default: return				"UNKNOWN";
		}
	}

	// ログメッセージを表示する関数
	template <typename... Args>
	static void log(LogType type, const std::string& message, Args... args) {
		// 現在時刻を取得
		std::time_t currentTime = std::time(nullptr);
		std::tm* localTime = std::localtime(&currentTime);

		// 時刻のフォーマット
		std::ostringstream timeStream;
		timeStream << std::put_time(localTime, "%Y-%m-%d %H:%M:%S");

		// ログメッセージの作成
		std::ostringstream logStream;
		logStream << "[" << timeStream.str() << "] ";
		logStream << "[" << LogTypeToString(type) << "] ";
		logStream << message;

		// 可変引数で渡された変数を追加
		(logStream << ... << args);
 
		// 最後に改行を追加
		logStream << std::endl;

		// ログをコンソールに表示
		std::cout << logStream.str();
	}
};