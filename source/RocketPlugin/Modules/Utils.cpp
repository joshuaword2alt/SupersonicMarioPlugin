#include "Utils.h"

std::string Utils::GetBakkesmodFolderPath()
{
	wchar_t szPath[MAX_PATH];
	wchar_t* s = szPath;
	if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, 0, (LPWSTR)szPath)))
	{
		PathAppend((LPWSTR)szPath, _T("\\bakkesmod\\bakkesmod\\"));

		std::ostringstream stm;

		while (*s != L'\0') {
			stm << std::use_facet< std::ctype<wchar_t> >(std::locale()).narrow(*s++, '?');
		}
		return stm.str();
	}
	return "";
}

void Utils::ParseObjFile(std::string path, std::vector<Vertex>* outVertices)
{
	std::ifstream file(path);
	std::string line;

	std::vector<Vertex> vertices;
	std::vector<Vertex> normals;
	while (std::getline(file, line))
	{
		if (line.size() == 0) continue;
		auto split = SplitStr(line, ' ');
		auto type = split[0];
		auto lastIndex = split.size() - 1;
		if (type == "v")
		{
			Vertex vertex;
			vertex.pos.x = std::stof(split[lastIndex - 2], nullptr);
			vertex.pos.y = std::stof(split[lastIndex - 1], nullptr);
			vertex.pos.z = std::stof(split[lastIndex], nullptr);
			vertex.color.x = 1.0f;
			vertex.color.y = 1.0f;
			vertex.color.z = 1.0f;
			vertex.color.w = 0.0f;
			vertex.texCoord.x = 1.0f;
			vertex.texCoord.y = 0.0f;
			vertices.push_back(vertex);
		}
		else if (type == "vn")
		{
			Vertex vertex;
			vertex.normal.x = std::stof(split[lastIndex - 2], nullptr);
			vertex.normal.y = std::stof(split[lastIndex - 1], nullptr);
			vertex.normal.z = std::stof(split[lastIndex], nullptr);
			normals.push_back(vertex);
		}
		else if (type == "f")
		{
			auto vertIndex1 = std::stoul(SplitStr(split[lastIndex - 2], '/')[0], nullptr);
			auto vertIndex2 = std::stoul(SplitStr(split[lastIndex - 1], '/')[0], nullptr);
			auto vertIndex3 = std::stoul(SplitStr(split[lastIndex], '/')[0], nullptr);
			auto normalIndex1 = std::stoul(SplitStr(split[lastIndex - 2], '/')[2], nullptr);
			auto normalIndex2 = std::stoul(SplitStr(split[lastIndex - 1], '/')[2], nullptr);
			auto normalIndex3 = std::stoul(SplitStr(split[lastIndex], '/')[2], nullptr);
			vertices[vertIndex1].normal.x = normals[normalIndex1].normal.x;
			vertices[vertIndex1].normal.y = normals[normalIndex1].normal.y;
			vertices[vertIndex1].normal.z = normals[normalIndex1].normal.z;
			vertices[vertIndex2].normal.x = normals[normalIndex2].normal.x;
			vertices[vertIndex2].normal.y = normals[normalIndex2].normal.y;
			vertices[vertIndex2].normal.z = normals[normalIndex2].normal.z;
			vertices[vertIndex3].normal.x = normals[normalIndex3].normal.x;
			vertices[vertIndex3].normal.y = normals[normalIndex3].normal.y;
			vertices[vertIndex3].normal.z = normals[normalIndex3].normal.z;
			outVertices->push_back(vertices[vertIndex1]);
			outVertices->push_back(vertices[vertIndex2]);
			outVertices->push_back(vertices[vertIndex3]);
		}
	}

}

std::vector<std::string> Utils::SplitStr(std::string str, char delimiter)
{
	std::stringstream stringStream(str);
	std::vector<std::string> seglist;
	std::string segment;
	while (std::getline(stringStream, segment, delimiter))
	{
		seglist.push_back(segment);
	}
	return seglist;
}

float Utils::Distance(Vector v1, Vector v2)
{
	return (float)sqrt(pow(v2.X - v1.X, 2.0) + pow(v2.Y - v1.Y, 2.0) + pow(v2.Z - v1.Z, 2.0));
}

