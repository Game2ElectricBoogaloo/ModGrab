#pragma once

#include "store.hpp"

#include <cstddef>
#include <cstdint>

#include <string>
#include <vector>

namespace layers {

std::string backupLocation(int modId, const std::string& path);
std::string stashLocation(int modId, const std::string& path);
std::string layerLocation(int modId, const std::string& path);

bool canMerge(Store& store, int modId, const std::string& path);
std::string join(Store& store, int modId, uint64_t titleId, const std::string& path);
std::string joinOrReplace(Store& store, int modId, uint64_t titleId, const std::string& path, std::vector<FileBackup>& created);
bool retriable(const MergedFile& file);
void rebuild(Store& store, const std::string& path);
size_t remergePending(Store& store);
std::vector<std::string> takeNotes();

std::string protect(Store& store, int modId, const std::string& dest, std::vector<FileBackup>& created);
std::vector<std::string> detach(Store& store, int modId, const std::string& path, const std::string& keepAt);

std::vector<std::string> removeMod(Store& store, int modId);
std::vector<std::string> disableMod(Store& store, int modId);
std::vector<std::string> enableMod(Store& store, int modId);
std::vector<std::string> discardStash(const InstalledMod& mod);
void appendErrors(std::vector<std::string>& into, std::vector<std::string> from);

}
