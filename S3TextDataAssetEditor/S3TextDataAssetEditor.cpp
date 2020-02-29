/*
    S3TextDataAssetEditor 
    =====================================================

    Command-line utility to extract and modify strings inside S3TextDataAsset's found in Shenmue III.

	Example usage:
		S3TextDataAssetEditor --extract <path to directory to extract> <output dir>
		S3TextDataAssetEditor --replace <original path> <output path>

    Contributors:               LemonHaze

    Changelog: 
        * v1.0 - 28/02/20 - Initial Version.
*/
#define APP_TITLE	"S3TextDataAssetEditor"
#define APP_VER		"v1.0"
#define APP_STR		APP_TITLE " " APP_VER

#include <Windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <stdlib.h>
#include <stdio.h>
#include <filesystem>
#include <fstream>
#include <istream>
#include <ostream>
#include <iterator>
#include <sstream>
#include <assert.h>

std::vector <std::string> FindFilesOfExtension(std::string searchDir) {
	std::vector <std::string> res;
	for (auto& path : std::filesystem::recursive_directory_iterator(searchDir)) {
		std::filesystem::path uassetPath = path;
		uassetPath.replace_extension(".uasset");
		if (std::filesystem::exists(uassetPath) && ".uexp" == path.path().extension().string()) {
			res.push_back(path.path().string());
			(res.size() % 16 ? res.shrink_to_fit() : (void)0);
		}
	}
	return res;
}
std::string GetFilename(std::string fullPath, bool with_extension = true) {
	const size_t last_slash_idx = fullPath.find_last_of("\\/");
	if (std::string::npos != last_slash_idx) {
		fullPath.erase(0, last_slash_idx + 1);
	}
	if (!with_extension) {
		const size_t period_idx = fullPath.rfind('.');
		if (std::string::npos != period_idx)
			fullPath.erase(period_idx);
	}
	return fullPath;
}
std::vector<BYTE> readFile(std::string filename) {
	std::ifstream file(filename, std::ios::binary);
	file.unsetf(std::ios::skipws);
	file.seekg(0, std::ios::end);

	std::streampos fileSize = file.tellg();
	file.seekg(0, std::ios::beg);

	std::vector<BYTE> vec;
	vec.reserve(fileSize);
	vec.insert(vec.begin(),
		std::istream_iterator<BYTE>(file),
		std::istream_iterator<BYTE>());

	return vec;
}

void PrintUsage() {
	printf("\nModes:\n==========================\n");
	printf("-e | --extract - Extract strings\n");
	printf("-r | --replace - Replace strings\n\n");

	printf("\nOptional flags:\n==========================\n");
	printf("'t' - Dumps text strings to console while processing\n\n");
}
int main(int argc, char ** argp)
{
	printf(APP_STR "\n==========================\n");
	if (argc < 3) {
		PrintUsage(); return -1;
	} else {
		std::string modeArg = argp[1], sourceArg = argp[2], targetArg = argp[3];
		if (modeArg.empty() || sourceArg.empty() || targetArg.empty()) {
			PrintUsage(); return -1;
		} else {
			int processed = 0, processedEntries = 0, failedEntries = 0;
			bool verbose = false, txtdump = false;

			if (modeArg.find("v") != std::string::npos)
				verbose = true;
			if (modeArg.find("t") != std::string::npos)
				txtdump = true;

			std::vector<std::string> uassetFiles = FindFilesOfExtension(sourceArg);
			printf("Found %d .uasset files\n", (int)uassetFiles.size());
			
			bool replaceOrExtract = (modeArg.find("-r") != std::string::npos || modeArg.find("--replace") != std::string::npos);
			if (replaceOrExtract) {
				struct searchReplace {
					std::string search, replace;
					searchReplace(std::string s, std::string r) {
						search = s;
						replace = r;
					}
				};
				std::vector<searchReplace> replacements;

				// Load CSV
				if (argc != 5) {
					PrintUsage();
					return -1;
				}
				std::filesystem::path csvPath = argp[4];
				if (!std::filesystem::exists(csvPath)) {
					printf("[Error] CSV does not exist.\n");  PrintUsage();
					return -1;
				}
				std::string field, line, replacement;
				std::ifstream file(csvPath);
				while (!file.eof()) {
					std::getline(file, line);
					std::stringstream ssline(line);

					std::string toFind, toReplace;
					ssline >> std::quoted(toFind);
					std::getline(ssline, field, ',');
					ssline >> std::quoted(toReplace);
					std::getline(ssline, replacement);

					std::string tempSearch = toFind, tempReplace = toReplace;
					replacements.push_back(searchReplace(tempSearch, tempReplace));

					if (replacements.size() % 16)
						replacements.shrink_to_fit();

					(verbose ? printf("Added replacement %d ['%s' ---> '%s']\n", (int)replacements.size(), toFind.c_str(), toReplace.c_str()) : 0);
				}

				// Patch UEXP
				for (auto file : uassetFiles) {
					bool bHasModified = false;
					std::filesystem::path outputPath = targetArg + "\\" + GetFilename(file);
					outputPath.replace_extension(".uexp");

					std::vector<BYTE> buffer = readFile(file);
					auto oldSize = buffer.size() - 4;
					if ((int)buffer.size() > 1000) {
						char entryCount = buffer[0x21], charaID = buffer[0x34];
						printf("entryCount = 0x%08X\ncharaID = 0x%08X\n", buffer[0x21], buffer[0x34]);

						if (charaID != -1 && entryCount != -1) {
							int modifications = 0;
							for (auto replacement : replacements) {
								std::vector<BYTE> newData;
								for (auto c : replacement.replace) 
									newData.push_back(c);
									if (newData.size() % 16) 
										newData.shrink_to_fit();
								

								std::vector<BYTE>::iterator it = std::search(buffer.begin(), buffer.end(), replacement.search.begin(), replacement.search.end());
								if (it != buffer.end()) {
									auto sizepos = buffer.erase(it - 4);
									sizepos = buffer.insert(sizepos, replacement.replace.size()+1);

									auto cursor = buffer.erase(sizepos + 4, (sizepos + 4) + replacement.search.size());
									buffer.insert(cursor, newData.begin(), newData.end());

									printf("[%d] Patched text data.\n", modifications);
									bHasModified = true;
									modifications++;
									if (modifications % 2)
										buffer.shrink_to_fit();
								} else {
									continue;
								}
							}
						}

						if (bHasModified) {
							// Write out UEXP
							std::ofstream outFile(outputPath, std::ios::binary);
							outFile.write(reinterpret_cast<char*>(&buffer[0]), buffer.size());
							printf("Written 0x%X bytes to %ws\n", (int)buffer.size(), outputPath.c_str());
							outFile.close();

							// Patch UASSET
							printf("Patching uasset..\n");
							std::filesystem::path uassetPath = file;
							uassetPath.replace_extension(".uasset");

							if (std::filesystem::exists(uassetPath)) {
								std::vector<BYTE> uassetBuffer = readFile(uassetPath.string());
								if (uassetBuffer.size() > 256) {
									auto serializedSize = (buffer.size() - 4);
									int currentSerializedSize = (uassetBuffer[uassetBuffer.size() - 0x5B] << 8) | uassetBuffer[uassetBuffer.size() - 0x5C];
									assert(currentSerializedSize == oldSize && "Invalid size in UASSET!");

									auto pos = uassetBuffer.erase(uassetBuffer.end() - 0x5C, uassetBuffer.end() - 0x5B);
									uassetBuffer.insert(pos, serializedSize);

									(verbose ? printf("currentSerializedSize = 0x%X\nnewSize = 0x%X\noldSize = 0x%zX\n", currentSerializedSize, serializedSize, oldSize) : 0);

									// Write out UEXP
									outputPath = targetArg + "\\" + GetFilename(file);
									outputPath.replace_extension(".uasset");
									std::ofstream outFile(outputPath, std::ios::binary);
									outFile.write(reinterpret_cast<char*>(&uassetBuffer[0]), uassetBuffer.size());
									printf("Written 0x%X bytes to %ws\n", (int)uassetBuffer.size(), outputPath.c_str());
									outFile.close();

									processed++;
								}
							}
							else {
								printf("[Error] Could not find uasset at path %s\n", uassetPath.string().c_str());
								continue;
							}
						}
					}
				}
			} 
			else if (!replaceOrExtract) {
				for (auto file : uassetFiles) {
					std::filesystem::path outputPath = targetArg + "\\" + GetFilename(file);
					outputPath.replace_extension(".log");
					
					std::ofstream outFile(outputPath);
					std::ifstream ifs(file, std::ios::binary);

					if (ifs.good()) {
						ifs.seekg(0x21, std::ios::beg);

						char entryCount = -1, charaID = -1;
						ifs.read(&entryCount, sizeof entryCount);

						ifs.seekg(0x34, std::ios::cur);
						ifs.read(&charaID, 1);

						if (charaID != -1 && entryCount != -1) {
							printf(R"(Extracting "%s")" "\n", file.c_str());
							for (int entry = 1; entry < entryCount + 1; entry++) {
								if (entry > 1) {
									int safetyfirst = 0;
									while ((char)ifs.get() != charaID && safetyfirst < 1000) { safetyfirst++; }
									if (safetyfirst > 999) {
										printf("unknown error happened, couldn't find type/chara id in file %s (curr offs = %d)\n", file.c_str(), (int)ifs.tellg());
										continue;
									}
								}
								
								char size = -1;
								ifs.seekg(0x39, std::ios::cur);
								auto sizepos = ifs.tellg();
								ifs.read(&size, 1);
								if (size == -1) {
									printf("failed to read entry size (offs = %d)\n", (int)ifs.tellg());
									continue;
								}

								std::string textData;
								if (size < -0) {
									size *= -2;
									for (int c = 0; c < size; ++c) {
										wchar_t buf = (wchar_t)ifs.get();
										if (buf != 0x00) textData += buf;
									}
								} else {
									//printf("[entry %d/%d] size = 0x%X \t curr offs = 0x%X \t file : %s\n", entry, entryCount, size, (int)ifs.tellg(), file.c_str());
									ifs.seekg(0x3, std::ios::cur);
									auto prevPos = ifs.tellg();
									char t1 = (char)ifs.get(), t2 = (char)ifs.get();

									if (t1 == 0x00 || t1 == 0x1A || t2 == 0x00 || t2 == 0x1A) {
										printf("!!! empty entry, no string data found !!!\n");
										failedEntries++;
										continue;
									}
									else {
										ifs.seekg(prevPos);
										for (int c = 0; c < size; ++c) {
											textData += (char)ifs.get();
										}
									}
								}
								outFile << textData << std::endl;
								(txtdump ? printf("%s\n", textData.c_str()) : 0);
								processedEntries++;
							}
						}
					}
					ifs.close();
					outFile.close();
					processed++;
				}
			}
			printf("Processed %d files, %d failed.\n", processedEntries, failedEntries);
		}
	}
}