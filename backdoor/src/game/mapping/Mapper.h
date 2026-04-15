#pragma once

#include <string>

enum GameVersions {
	UNKNOWN = 0,
	CASUAL_1_7_10 = 1,
	CASUAL_1_8 = 2,
	FORGE_1_7_10 = 3,
	FORGE_1_8 = 4,
	FEATHER_1_8 = 5,
	LUNAR_1_7_10 = 6,
	LUNAR_1_8 = 7
};

class Mapper
{
public:
	static void				Initialize(const GameVersions version);
	static std::string		Get(const char* mapping, int type = 1);
};
