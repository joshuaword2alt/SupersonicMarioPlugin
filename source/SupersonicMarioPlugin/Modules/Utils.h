#pragma once

#include <shlwapi.h>
#pragma comment(lib,"shlwapi.lib")
#include "shlobj.h"
#include "../Graphics/GraphicsTypes.h"
#include <math.h>
#include <sys/stat.h>

class Utils
{
public:
	std::wstring GetBakkesmodFolderPathWide();
	std::string GetBakkesmodFolderPath();
	void ParseObjFile(std::string path, std::vector<Vertex> *outVertices);
	std::vector<std::string> SplitStr(std::string str, char delimiter);
	float Distance(Vector v1, Vector v2);
	bool FileExists(std::string filename);


};