// SPDX-License-Identifier: GPL-2.0
// The real Syncer is linked with fake netlink/status/watcher boundaries.
#include "sync.hpp"
#include "status.hpp"

#include <algorithm>
#include <cassert>
#include <deque>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using namespace tosya;

namespace
{
const fs::path root{ TOSYA_TEST_ROOT };
enum class Reply { Ok, Lost, Refused, BadCount, Failed, NoStatus };
std::deque<Reply> replies;
std::vector<std::vector<ApkEntry>> batches;
std::vector<std::string> events;
std::vector<Pairs> policies;
std::vector<std::vector<HiddenName>> names;
std::vector<std::string> displays;
kaux_status current_status{};
bool status_missing;
unsigned retries;

Config once_config()
{
	Config config;
	config.once = true;
	return config;
}

void write(const fs::path &path, std::string_view value = "apk")
{
	fs::create_directories(path.parent_path());
	std::ofstream stream{ path };
	stream << value;
}

void reset()
{
	replies.clear();
	batches.clear();
	events.clear();
	policies.clear();
	names.clear();
	displays.clear();
	current_status = {};
	status_missing = false;
	retries = 0;
}

void trace(std::string_view scenario)
{
	std::cout << scenario << ':';
	for (const auto &event : events)
		std::cout << ' ' << event;
	for (const auto &batch : batches)
		for (const auto &entry : batch)
			std::cout << ' ' << (entry.action ? "DEL" : "ADD")
				  << '/' << entry.uid << '/'
				  << fs::path{ entry.path }.filename().string();
	std::cout << " retry=" << retries << '\n';
}

void apk_state_machine()
{
	reset();
	Syncer sync{ once_config() };
	write(root / "a/base.apk");
	write(root / "b/base.apk");
	sync.callers_ = { { "a", 10001 }, { "b", 10002 } };
	sync.code_dirs_ = { { "a", root / "a" }, { "b", root / "b" } };
	assert(sync.publish_code_dirs());
	assert(batches.size() == 1 && batches[0].size() == 2);
	assert(sync.publish_code_dirs() && batches.size() == 1);
	trace("initial-and-unchanged");

	reset();
	sync.callers_["a"] = 10003;
	replies = { Reply::Lost };
	assert(!sync.publish_code_dirs());
	assert(batches.size() == 1 && batches[0][0].action == 1);
	assert(retries == 1);
	assert(sync.publish_code_dirs());
	assert(batches.size() == 3 && batches[1][0].action == 1 &&
	       batches[2][0].action == 0 && batches[2][0].uid == 10003);
	trace("lost-delete-before-replacement");

	reset();
	Syncer lost{ once_config() };
	lost.callers_ = sync.callers_;
	lost.code_dirs_ = sync.code_dirs_;
	replies = { Reply::Lost };
	assert(!lost.publish_code_dirs());
	lost.callers_.clear();
	assert(lost.publish_code_dirs());
	assert(batches.size() == 2 && batches[1].size() == 2);
	assert(std::ranges::all_of(batches[1], [](const auto &e) {
		return e.action == 1;
	}));
	trace("lost-add-then-config-removal");

	for (const auto failure :
	     { Reply::Refused, Reply::Lost, Reply::BadCount, Reply::Failed,
	       Reply::NoStatus }) {
		reset();
		Syncer recovering{ once_config() };
		recovering.callers_ = sync.callers_;
		recovering.code_dirs_ = sync.code_dirs_;
		replies = { Reply::Lost };
		assert(!recovering.publish_code_dirs());
		reset();
		replies = { failure, Reply::Ok };
		assert(!recovering.publish_code_dirs());
		const bool continues = failure == Reply::Refused ||
				       failure == Reply::Failed;
		assert(batches.size() == (continues ? 2u : 1u));
		assert(retries == 1);
		assert(!displays.empty());
		if (!continues)
			assert(displays.back() ==
			       "kernel unreachable, apk update pending");
		else
			assert(displays.back().ends_with("apk retry pending"));
		replies.clear();
		assert(recovering.publish_code_dirs());
		trace("recover-" + std::to_string(static_cast<int>(failure)));
	}
}

void apk_paths()
{
	reset();
	Syncer sync{ once_config() };
	write(root / "direct.apk");
	write(root / "Named/Named.apk");
	write(root / "odd/One.apk");
	write(root / "split/split_a.apk");
	write(root / "split/split_b.apk");
	sync.callers_ = { { "direct", 10001 }, { "named", 10002 },
			  { "odd", 10003 },    { "split", 10004 },
			  { "alias", 10001 },  { "gone", 10005 } };
	sync.code_dirs_ = {
		{ "direct", root / "direct.apk" }, { "named", root / "Named" },
		{ "odd", root / "odd" },	   { "split", root / "split" },
		{ "alias", root / "direct.apk" },  { "gone", root / "absent" }
	};
	assert(sync.publish_code_dirs());
	assert(batches.size() == 1 && batches[0].size() == 3);
	trace("apk-resolution-and-dedup");
}

void package_xml(bool extra = false)
{
	std::string xml = "<packages>";
	for (const auto &[name, uid] :
	     std::vector<std::pair<std::string, unsigned>>{
		     { "com.example.caller", 10001 },
		     { "com.example.target", 10002 },
		     { "com.example.alias", 10002 } }) {
		auto path = root / name / "base.apk";
		write(path);
		xml += "<package name=\"" + name + "\" userId=\"" +
		       std::to_string(uid) + "\" codePath=\"" + path.string() +
		       "\"/>";
	}
	if (extra) {
		write(root / "new/base.apk");
		xml += "<package name=\"com.example.new\" userId=\"10003\" codePath=\"" +
		       (root / "new").string() + "\"/>";
	}
	write(root / "packages.xml", xml + "</packages>");
}

void orchestration()
{
	reset();
	fs::create_directories(root / "users/0");
	fs::create_directories(root / "users/999");
	// Make the token deterministic and keep this test independent of /dev/urandom.
	write(root / "state/token", std::string(16, 't'));
	write(root / "config.json", R"({"configVersion":4,"scope":{
"com.example.caller":{"extraAppList":["com.example.target"]}}})");
	package_xml();
	Syncer sync{ once_config() };
	sync.sync_now();
	assert(sync.sync_ok_ && policies.size() == 1 && names.size() == 1);
	assert((policies[0] ==
		Pairs{ { 10001, 10002 }, { 99910001, 99910002 } }));
	assert(names[0].size() ==
	       2); // shared UID expands to both package names
	assert((events == std::vector<std::string>{ "policy", "status", "token",
						    "names", "add", "status",
						    "status" }));
	trace("startup-policy-token-names-apks");
	const auto policy = sync.pushed_;
	reset();
	sync.sync_now();
	assert(sync.sync_ok_ && policies.empty() && names.empty() &&
	       batches.empty());
	trace("unchanged-pass");

	write(root / "hint/base.apk");
	sync.code_dirs_["com.example.caller"] = root / "hint";
	sync.sync_now();
	assert(sync.code_dirs_.at("com.example.caller") == root / "hint");
	trace("early-path-hint-survives");
	reset();
	package_xml(true);
	sync.sync_now();
	assert(sync.sync_ok_ && policies.empty() && names.empty());
	assert(!batches.empty());
	assert(sync.code_dirs_.at("com.example.caller") != root / "hint");
	trace("unchanged-names-new-apk-and-snapshot");

	reset();
	write(root / "config.json", "{");
	sync.sync_now();
	assert(!sync.sync_ok_ && sync.pushed_ == policy && policies.empty());
	assert(retries == 1 && batches.empty());
	trace("invalid-input-keeps-policy");
	write(root / "config.json", R"({"configVersion":4,"scope":{}})");
	sync.sync_now();
	assert(!policies.empty() && policies.back().empty());
	trace("empty-config-clears-policy");
}
} // namespace

namespace tosya
{
bool NetlinkClient::push(std::span<const Pair> value)
{
	events.emplace_back("policy");
	policies.emplace_back(value.begin(), value.end());
	return true;
}
bool NetlinkClient::push_names(std::span<const HiddenName> value)
{
	events.emplace_back("names");
	names.emplace_back(value.begin(), value.end());
	return true;
}
bool NetlinkClient::push_token(std::span<const std::uint32_t, 4>)
{
	events.emplace_back("token");
	return true;
}
bool NetlinkClient::refresh()
{
	events.emplace_back("refresh");
	return true;
}
bool NetlinkClient::push_apks(std::span<const ApkEntry> value)
{
	batches.emplace_back(value.begin(), value.end());
	events.emplace_back(value.front().action ? "drop" : "add");
	auto reply = Reply::Ok;
	if (!replies.empty()) {
		reply = replies.front();
		replies.pop_front();
	}
	refused_ = reply == Reply::Refused;
	status_missing = reply == Reply::NoStatus;
	current_status.apk_offered = value.size() + (reply == Reply::BadCount);
	current_status.apk_failed = reply == Reply::Failed;
	return reply != Reply::Lost && reply != Reply::Refused;
}
std::optional<kaux_status> NetlinkClient::status()
{
	events.emplace_back("status");
	if (status_missing)
		return std::nullopt;
	return current_status;
}
void Watcher::arm_retry()
{
	++retries;
}
fs::path module_dir()
{
	return root;
}
void report_status(std::string_view value)
{
	displays.emplace_back(value);
}
DeviceGeometry read_device_config()
{
	return {};
}
} // namespace tosya

int main()
{
	apk_state_machine();
	apk_paths();
	orchestration();
	std::cout << "sync flow: passed\n";
}
