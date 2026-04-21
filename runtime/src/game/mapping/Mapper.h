#pragma once

#include <string>

enum GameVersions {
	UNKNOWN = 0,
	BADLION = 2,
	FORGE_1_8 = 4,
	FEATHER_1_8 = 5,
	LUNAR = 7
};

class Mapper
{
public:
	static void				Initialize(const GameVersions version);
	static std::string		Get(const char* mapping, int type = 1);
};
