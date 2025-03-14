#pragma once
#include "pch.h"

class LLogger
{
public:

	LLogger(){}
	~LLogger(){}

	template<typename T>
	static void LogString(T val, bool bLogToFile = true)
	{
		LogStringImpl(val, bLogToFile, std::is_enum<T>());
	}

protected:

	// Declaration for the specialization for non-enums
	template <typename String>
	static void LogStringImpl(String string, bool bLogToFile, std::false_type dummy) 
	{
#if _DEBUG
		std::cout << string << std::endl;
#endif
		if (bLogToFile)
		{
			LogToFile();
		}
	}

	// Declaration for the specialization for enums
	template <typename Enum>
	static void LogStringImpl(Enum enumValue, bool bLogToFile, std::true_type dummy)
	{
#if _DEBUG
		std::cout << magic_enum::enum_name(enumValue) << std::endl;
#endif
		if (bLogToFile)
		{
			LogToFile();
		}
	}

	// todo
	static void LogToFile()
	{

	}
};

