#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace OuterChainPolicy
{
	constexpr size_t MAX_DEPTH = 64;

	template <typename Node, typename Next, typename Identity>
	bool TryCollect(Node* first, std::vector<Node*>& chain, Next next, Identity identity)
	{
		std::unordered_set<uint64_t> seen;

		for (Node* current = first; current; current = next(current))
		{
			const uint64_t pointer = identity(current);

			if (!pointer || chain.size() >= MAX_DEPTH || !seen.insert(pointer).second)
				return false;

			chain.push_back(current);
		}

		return true;
	}
}
