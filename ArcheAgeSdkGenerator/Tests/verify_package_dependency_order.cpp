#include "../Engine/Core/PackageDependencyOrder.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	PackageDependencyOrder::Graph MakeGraph(const std::vector<std::string>& names)
	{
		PackageDependencyOrder::Graph graph;
		graph.names = names;
		graph.required.resize(names.size());
		graph.preferred.resize(names.size());
		return graph;
	}

	bool IsBefore(const PackageDependencyOrder::Result& result, int first, int second)
	{
		int firstPosition = -1;
		int secondPosition = -1;
		for (std::size_t index = 0; index < result.order.size(); index++)
		{
			if (result.order[index] == first)
				firstPosition = static_cast<int>(index);
			if (result.order[index] == second)
				secondPosition = static_cast<int>(index);
		}
		return firstPosition >= 0 && secondPosition >= 0 && firstPosition < secondPosition;
	}
}

int main()
{
	// an empty graph produces an empty order
	{
		PackageDependencyOrder::Graph graph = MakeGraph({});
		const PackageDependencyOrder::Result result = PackageDependencyOrder::Sort(graph);
		assert(result.order.empty());
		assert(result.complete());
	}

	// a single node stays where it is
	{
		PackageDependencyOrder::Graph graph = MakeGraph({ "Only" });
		const PackageDependencyOrder::Result result = PackageDependencyOrder::Sort(graph);
		assert(result.order.size() == 1);
		assert(result.order[0] == 0);
		assert(result.complete());
	}

	// independent nodes keep their input order
	{
		PackageDependencyOrder::Graph graph = MakeGraph({ "Zeta", "Alpha", "Mu" });
		const PackageDependencyOrder::Result result = PackageDependencyOrder::Sort(graph);
		assert(result.order.size() == 3);
		assert(result.order[0] == 0 && result.order[1] == 1 && result.order[2] == 2);
		assert(result.complete());
	}

	// a chain of required dependencies is ordered from the bottom up
	{
		PackageDependencyOrder::Graph graph = MakeGraph({ "A", "B", "C", "D" });
		graph.required[1].push_back(0);
		graph.required[2].push_back(1);
		graph.required[3].push_back(2);
		const PackageDependencyOrder::Result result = PackageDependencyOrder::Sort(graph);
		assert(result.complete());
		assert(result.order.size() == 4);
		assert(IsBefore(result, 0, 1));
		assert(IsBefore(result, 1, 2));
		assert(IsBefore(result, 2, 3));
	}

	// a required dependency always wins over the input order
	{
		PackageDependencyOrder::Graph graph = MakeGraph({ "AudioMixer", "Engine" });
		graph.required[0].push_back(1);
		const PackageDependencyOrder::Result result = PackageDependencyOrder::Sort(graph);
		assert(result.complete());
		assert(IsBefore(result, 1, 0));
	}

	// a cycle breaks the preferred edge, the required edge survives
	{
		PackageDependencyOrder::Graph graph = MakeGraph({ "AudioMixer", "Engine" });
		graph.required[0].push_back(1);  // AudioMixer needs Engine
		graph.preferred[1].push_back(0); // Engine only wants a declaration of AudioMixer
		const PackageDependencyOrder::Result result = PackageDependencyOrder::Sort(graph);
		assert(result.complete());
		assert(result.unsatisfiedRequired.empty());
		assert(result.unsatisfiedPreferred.size() == 1);
		assert(IsBefore(result, 1, 0));
	}

	// a cycle that only consists of required edges has to drop one of them
	{
		PackageDependencyOrder::Graph graph = MakeGraph({ "A", "B" });
		graph.required[0].push_back(1);
		graph.required[1].push_back(0);
		const PackageDependencyOrder::Result result = PackageDependencyOrder::Sort(graph);
		assert(!result.complete());
		assert(result.droppedRequired.size() == 1);
		assert(result.unsatisfiedRequired.size() == 1);
		// every node is still written exactly once
		assert(result.order.size() == 2);
	}

	// the cheapest edge of a cycle is broken, the one with the fewest users
	{
		PackageDependencyOrder::Graph graph = MakeGraph({ "A", "B", "C", "D", "E" });
		graph.required[0].push_back(1);   // A needs B
		graph.preferred[1].push_back(0);  // B only wants a declaration of A
		graph.preferred[2].push_back(0);  // C only wants a declaration of A
		graph.preferred[3].push_back(1);  // D only wants a declaration of B
		graph.preferred[4].push_back(1);  // E only wants a declaration of B
		const PackageDependencyOrder::Result result = PackageDependencyOrder::Sort(graph);
		assert(result.complete());
		assert(result.order.size() == 5);
		assert(IsBefore(result, 1, 0));
	}

	// a preferred dependency is honoured when the graph allows it
	{
		PackageDependencyOrder::Graph graph = MakeGraph({ "A", "B", "C" });
		graph.preferred[2].push_back(0);
		const PackageDependencyOrder::Result result = PackageDependencyOrder::Sort(graph);
		assert(result.complete());
		assert(result.droppedPreferred.empty());
		assert(IsBefore(result, 0, 2));
	}

	// self references and invalid indices are ignored
	{
		PackageDependencyOrder::Graph graph = MakeGraph({ "A", "B" });
		graph.required[0].push_back(0);
		graph.required[0].push_back(99);
		graph.required[0].push_back(-1);
		const PackageDependencyOrder::Result result = PackageDependencyOrder::Sort(graph);
		assert(result.complete());
		assert(result.order.size() == 2);
	}

	// the sort is deterministic
	{
		PackageDependencyOrder::Graph graph = MakeGraph({ "A", "B", "C", "D", "E", "F" });
		graph.required[3].push_back(0);
		graph.required[5].push_back(1);
		graph.preferred[2].push_back(4);
		graph.preferred[4].push_back(2);

		const PackageDependencyOrder::Result first = PackageDependencyOrder::Sort(graph);
		const PackageDependencyOrder::Result second = PackageDependencyOrder::Sort(graph);
		assert(first.order == second.order);
		assert(first.droppedRequired == second.droppedRequired);
		assert(first.droppedPreferred == second.droppedPreferred);
	}

	// no node is ever lost, even with a broken cycle
	{
		PackageDependencyOrder::Graph graph = MakeGraph({ "A", "B", "C", "D" });
		graph.required[0].push_back(1);
		graph.required[1].push_back(0);
		graph.required[2].push_back(3);
		graph.required[3].push_back(2);
		const PackageDependencyOrder::Result result = PackageDependencyOrder::Sort(graph);
		assert(result.order.size() == 4);
		for (int node = 0; node < 4; node++)
		{
			bool found = false;
			for (const int written : result.order)
				found = found || written == node;
			assert(found);
		}
	}

	// A directed three-node cycle must break an actual edge, not its reverse.
	{
		auto graph = MakeGraph({ "A", "B", "C" });
		graph.required[0] = { 1 };
		graph.required[1] = { 2 };
		graph.preferred[2] = { 0 };
		const auto result = PackageDependencyOrder::Sort(graph);
		assert(result.complete());
		assert(result.order.size() == 3);
		assert(result.droppedPreferred.size() == 1);
		assert((result.droppedPreferred[0] == std::pair<int, int>(2, 0)));
		assert(IsBefore(result, 2, 1) && IsBefore(result, 1, 0));
	}

	std::cout << "PASS: dependency aware package and type ordering\n";
	return 0;
}
