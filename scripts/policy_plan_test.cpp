// SPDX-License-Identifier: GPL-2.0
#include "policy_plan.hpp"

#include <cassert>
#include <fstream>
#include <iostream>

using namespace tosya;

namespace
{
class FixedRules final : public Rules {
    public:
	Pairs pairs;
	FixedRules()
		: Rules(Tool::HmaOss, {}, {})
	{
	}
	Pairs expand(const PackageDb &, const Presets &) const override
	{
		return pairs;
	}
	bool hides(std::string_view, std::string_view, bool,
		   const Presets &) const override
	{
		return false;
	}
};
} // namespace

int main()
{
	const auto file =
		std::filesystem::path{ "build/tests/policy-packages.xml" };
	std::filesystem::create_directories(file.parent_path());
	std::ofstream{ file } << R"(<packages>
<package name="com.example.caller" userId="10001"/>
<package name="com.example.alias" userId="10002"/>
<package name="com.example.target" userId="10002"/>
<package name="com.example.normal" userId="10003"/>
</packages>)";
	const auto db = PackageDb::load(file);
	assert(db);
	FixedRules rules;
	auto plan = build_policy_plan(rules, {}, *db, { 0, 999 });
	assert(plan && plan->pairs.empty() && plan->names.empty() &&
	       plan->callers.empty());
	rules.pairs = { { 10001, 10003 }, { 10001, 10002 }, { 10001, 10002 } };
	plan = build_policy_plan(rules, {}, *db, { 0, 999 });
	assert(plan);
	assert((plan->pairs == Pairs{ { 10001, 10002 },
				      { 10001, 10003 },
				      { 99910001, 99910002 },
				      { 99910001, 99910003 } }));
	assert(plan->names.size() == 3);
	assert(plan->names[0].name == "com.example.normal");
	assert(plan->names[1].name == "com.example.alias");
	assert(plan->names[2].name == "com.example.target");
	assert(plan->callers.size() == 1 &&
	       plan->callers.at("com.example.caller") == 10001);
	rules.pairs = { { 0, 10002 } };
	plan = build_policy_plan(rules, {}, *db, { 0, 999 });
	assert(plan && plan->callers.size() == 4);
	assert((plan->pairs == Pairs{ { 0, 10002 }, { 0, 99910002 } }));

	rules.pairs.clear();
	for (unsigned i = 0; i < KAUX_MAX_POLICY_PAIRS; ++i)
		rules.pairs.push_back({ 10001, 10000 + i });
	plan = build_policy_plan(rules, {}, *db, { 0 });
	assert(plan && plan->pairs.size() == KAUX_MAX_POLICY_PAIRS);
	rules.pairs.push_back({ 10001, 10000 + KAUX_MAX_POLICY_PAIRS });
	plan = build_policy_plan(rules, {}, *db, { 0 });
	assert(!plan && plan.error() == KAUX_MAX_POLICY_PAIRS + 1);
	std::cout
		<< "policy plan: empty, multiuser, ordering, shared UID, wildcard, capacity passed\n";
}
