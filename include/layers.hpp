#pragma once

#include "store.hpp"

#include <string>
#include <vector>

namespace layers {

std::string backupLocation(int modId, const std::string& path);
std::string stashLocation(int modId, const std::string& path);

std::string protect(Store& store, int modId, const std::string& dest, std::vector<FileBackup>& created);
std::vector<std::string> detach(Store& store, int modId, const std::string& path, const std::string& keepAt);

std::vector<std::string> removeMod(Store& store, int modId);
std::vector<std::string> disableMod(Store& store, int modId);
std::vector<std::string> enableMod(Store& store, int modId);
std::vector<std::string> discardStash(const InstalledMod& mod);
void appendErrors(std::vector<std::string>& into, std::vector<std::string> from);

}
