// Patterns the Coccinelle rules in this directory must find. A rule that
// matches nothing here is broken, and static-analysis.sh fails on it.
#include <set>
#include <vector>

struct Thing { void Use() const; };

void RefThenErase(std::set<Thing*>& things, std::vector<Thing>& list)
{
	std::set<Thing*>::iterator it = things.begin();
	Thing* const& thing = *it;
	things.erase(it);
	thing->Use();

	std::vector<Thing>::iterator li = list.begin();
	const Thing& first = *li;
	list.erase(li);
	first.Use();
}
