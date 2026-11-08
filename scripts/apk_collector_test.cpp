// SPDX-License-Identifier: GPL-2.0
#include "apk_collector.hpp"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>

using namespace tosya;
namespace fs = std::filesystem;

int main()
{
	// A short-lived Linux fixture keeps the 10k-file boundary test off Android.
	std::string pattern =
		(fs::temp_directory_path() / "tosya-collector-XXXXXX").string();
	const char *created = ::mkdtemp(pattern.data());
	assert(created);
	const fs::path root{ created };
	struct Cleanup {
		fs::path path;
		~Cleanup()
		{
			fs::remove_all(path);
		}
	} cleanup{ root };
	const auto caller = root / "caller.apk";
	std::ofstream{ caller } << "apk";
	std::ofstream xml{ root / "packages.xml" };
	xml << "<packages>";
	xml << "<package name=\"zz.caller\" userId=\"10001\" codePath=\""
	    << caller.string() << "\"/>";
	for (unsigned i = 0; i < kApkCollectionLimit; ++i) {
		const auto path = root / (std::to_string(i) + ".apk");
		fs::create_hard_link(caller, path);
		xml << "<package name=\"aa.app" << i
		    << "\" userId=\"10002\" codePath=\"" << path.string()
		    << "\"/>";
	}
	xml << "</packages>";
	xml.close();
	const auto db = PackageDb::load(root / "packages.xml");
	assert(db && db->by_name().size() == kApkCollectionLimit + 1);
	std::map<std::string, fs::path, std::less<>> paths;
	const auto entries =
		collect_apks(&*db, { { "zz.caller", 10001 } }, paths);
	assert(entries.size() == kApkCollectionLimit);
	assert(entries.front().path == caller.string() &&
	       entries.front().uid == 10001);
	// Collection deduplicates paths; publication independently folds dev/ino.
	assert(entries[1].path != entries.front().path &&
	       entries[1].ino == entries.front().ino);
	std::cout
		<< "apk collector: caller priority and independent 10000-file cap passed\n";
}
