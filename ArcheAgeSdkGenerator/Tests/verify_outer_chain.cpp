#include "../Engine/UEClasses/OuterChainPolicy.h"

#include <cassert>

struct Node
{
	uint64_t Id;
	Node* Next;
};

bool Collect(Node* first, std::vector<Node*>& chain)
{
	return OuterChainPolicy::TryCollect(
		first, chain,
		[](Node* node) { return node->Next; },
		[](Node* node) { return node->Id; });
}

int main()
{
	Node root { 1, nullptr };
	Node child { 2, &root };
	std::vector<Node*> chain;

	assert(Collect(&child, chain));
	assert(chain.size() == 2);
	assert(chain[0] == &child && chain[1] == &root);

	Node cycle { 3, nullptr };
	cycle.Next = &cycle;
	chain.clear();
	assert(!Collect(&cycle, chain));

	Node longChain[OuterChainPolicy::MAX_DEPTH + 1] {};
	for (size_t index = 0; index < OuterChainPolicy::MAX_DEPTH + 1; ++index)
	{
		longChain[index].Id = index + 1;
		longChain[index].Next = index ? &longChain[index - 1] : nullptr;
	}

	chain.clear();
	assert(!Collect(&longChain[OuterChainPolicy::MAX_DEPTH], chain));

	chain.clear();
	assert(Collect(&longChain[OuterChainPolicy::MAX_DEPTH - 1], chain));
	assert(chain.size() == OuterChainPolicy::MAX_DEPTH);

	Node invalid { 0, nullptr };
	chain.clear();
	assert(!Collect(&invalid, chain));
}
