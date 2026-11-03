#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace PackageDependencyOrder
{
	/**
	 * \brief Dependency graph of the generated SDK.
	 *
	 * The generated headers do not include each other, so the order in which
	 * the packages - and the types inside a package - are written decides
	 * whether the SDK compiles at all.  Every reference to a reflected type
	 * is therefore an edge in a graph and the graph has to be emitted in a
	 * topological order.
	 *
	 * There are two kinds of edges:
	 *
	 *  - required : the referenced type has to be *complete* before the
	 *                reference is seen.  A base class and a by value member
	 *                need a complete type.  Breaking such an edge is a hard
	 *                compile error ("base class undefined") and it also
	 *                invalidates the memory layout of the type.
	 *  - preferred: the referenced type only has to be *declared*.  A pointer
	 *                member, a template argument or a function signature
	 *                needs a declaration.  Breaking such an edge is a
	 *                compile error as well, but it never breaks a layout,
	 *                so it is the first kind of edge that gets dropped when
	 *                the graph contains a cycle.
	 *
	 * Both kinds are honoured whenever the graph allows it.
	 */
	struct Graph
	{
		/** \brief Human readable node names, only used for diagnostics. */
		std::vector<std::string> names;
		/** \brief required[i] holds the node indices that have to precede node i. */
		std::vector<std::vector<int>> required;
		/** \brief preferred[i] holds the node indices that should precede node i. */
		std::vector<std::vector<int>> preferred;
	};

	/**
	 * \brief Result of a sort.
	 */
	struct Result
	{
		/** \brief Node indices in emission order. */
		std::vector<int> order;
		/** \brief Required edges that had to be broken to resolve a cycle, as (node, dependency). */
		std::vector<std::pair<int, int>> droppedRequired;
		/** \brief Preferred edges that had to be broken to resolve a cycle, as (node, dependency). */
		std::vector<std::pair<int, int>> droppedPreferred;
		/** \brief Required edges whose dependency is written after the node, as (node, dependency). */
		std::vector<std::pair<int, int>> unsatisfiedRequired;
		/** \brief Preferred edges whose dependency is written after the node, as (node, dependency). */
		std::vector<std::pair<int, int>> unsatisfiedPreferred;

		/** \brief True when every required edge could be honoured. */
		bool complete() const { return unsatisfiedRequired.empty(); }
	};

	namespace Detail
	{
		enum class EdgeKind
		{
			required,
			preferred
		};

		/**
		 * \brief Finds one cycle inside the not yet emitted part of the graph.
		 * \return the node indices of the cycle or an empty vector
		 */
		inline std::vector<int> FindCycle(const std::vector<char>& emitted,
			const std::vector<std::vector<int>>& dependencies,
			const std::vector<int>& candidates)
		{
			enum class Color
			{
				white,
				grey,
				black
			};

			std::vector<Color> color(dependencies.size(), Color::white);
			std::vector<int> parent(dependencies.size(), -1);

			for (const int start : candidates)
			{
				if (color[start] != Color::white)
					continue;

				struct Frame
				{
					int node;
					std::size_t next;
				};

				std::vector<Frame> stack;
				stack.push_back({ start, 0 });
				color[start] = Color::grey;

				while (!stack.empty())
				{
					Frame& frame = stack.back();
					const std::vector<int>& deps = dependencies[frame.node];

					if (frame.next >= deps.size())
					{
						color[frame.node] = Color::black;
						stack.pop_back();
						continue;
					}

					const int dependency = deps[frame.next++];
					if (emitted[dependency])
						continue;

					if (color[dependency] == Color::grey)
					{
						// the dependency is an ancestor of the current node
						std::vector<int> cycle;
						int walk = frame.node;
						while (walk != dependency)
						{
							cycle.push_back(walk);
							walk = parent[walk];
						}
						cycle.push_back(dependency);
						std::reverse(cycle.begin(), cycle.end());
						return cycle;
					}

					if (color[dependency] == Color::white)
					{
						color[dependency] = Color::grey;
						parent[dependency] = frame.node;
						stack.push_back({ dependency, 0 });
					}
				}
			}

			return {};
		}
	}

	/**
	 * \brief Sorts the nodes of a graph into a topological order.
	 *
	 * Every required edge is honoured whenever that is possible.  If the graph
	 * contains a cycle, one edge of that cycle is broken: preferred edges
	 * first, because they only need a declaration, and among those the edge
	 * that affects the fewest other nodes.  A required edge is only broken
	 * when the cycle consists of required edges alone, which means the
	 * generated SDK has a real inheritance cycle that cannot be ordered.
	 *
	 * The order is deterministic: nodes are emitted in the order of the graph
	 * whenever several nodes are ready at the same time.
	 *
	 * \param graph the dependency graph
	 * \return the emission order and the edges that had to be broken
	 */
	inline Result Sort(const Graph& graph)
	{
		const std::size_t count = graph.names.size();

		Result result;
		result.order.reserve(count);

		std::vector<std::vector<int>> requiredDependencies(count);
		std::vector<std::vector<int>> preferredDependencies(count);
		std::vector<std::vector<int>> dependencies(count);
		std::vector<std::vector<int>> dependents(count);

		std::vector<int> pendingRequired(count, 0);
		std::vector<int> pendingPreferred(count, 0);
		std::vector<int> preferredDependentCount(count, 0);
		std::vector<std::vector<char>> requiredActive(count);
		std::vector<std::vector<char>> preferredActive(count);
		std::vector<char> emitted(count, 0);

		const auto contains = [](const std::vector<int>& list, int value)
		{
			return std::ranges::find(list, value) != list.end();
		};

		const auto addEdge = [&](int node, int dependency, Detail::EdgeKind kind)
		{
			if (dependency < 0 || dependency >= static_cast<int>(count) || dependency == node)
				return;

			if (kind == Detail::EdgeKind::required)
			{
				if (contains(requiredDependencies[node], dependency))
					return;
				requiredDependencies[node].push_back(dependency);
				requiredActive[node].push_back(1);
				pendingRequired[node]++;
			}
			else
			{
				// a required edge already covers the dependency
				if (contains(requiredDependencies[node], dependency))
					return;
				if (contains(preferredDependencies[node], dependency))
					return;
				preferredDependencies[node].push_back(dependency);
				preferredActive[node].push_back(1);
				pendingPreferred[node]++;
			}

			dependencies[node].push_back(dependency);
			dependents[dependency].push_back(node);
		};

		for (std::size_t node = 0; node < count; node++)
		{
			if (node < graph.required.size())
			{
				for (const int dependency : graph.required[node])
					addEdge(static_cast<int>(node), dependency, Detail::EdgeKind::required);
			}

			if (node < graph.preferred.size())
			{
				for (const int dependency : graph.preferred[node])
					addEdge(static_cast<int>(node), dependency, Detail::EdgeKind::preferred);
			}
		}

		for (std::size_t node = 0; node < count; node++)
		{
			for (const int dependency : preferredDependencies[node])
				preferredDependentCount[dependency]++;
		}

		std::vector<int> candidates(count);
		for (std::size_t index = 0; index < count; index++)
			candidates[index] = static_cast<int>(index);

		while (result.order.size() < count)
		{
			// collect the nodes whose dependencies are all emitted.  A preferred
			// dependency is honoured as well, it only needs a declaration.
			std::vector<int> ready;
			for (std::size_t node = 0; node < count; node++)
			{
				if (emitted[node] || pendingRequired[node] != 0 || pendingPreferred[node] != 0)
					continue;
				ready.push_back(static_cast<int>(node));
			}

			if (ready.empty())
			{
				// the remaining graph contains a cycle, break one of its edges
				std::vector<int> pending;
				for (std::size_t node = 0; node < count; node++)
				{
					if (!emitted[node])
						pending.push_back(static_cast<int>(node));
				}

				const std::vector<int> cycle = Detail::FindCycle(emitted, dependencies, pending);
				if (cycle.empty())
				{
					// should not happen, but never loop forever
					result.order.push_back(pending.front());
					emitted[pending.front()] = 1;
					continue;
				}

				// FindCycle follows node -> dependency. Consecutive entries
				// therefore represent (cycle[i - 1], cycle[i]), not its reverse.
				struct Candidate
				{
					int node;
					int dependency;
					Detail::EdgeKind kind;
				};

				std::vector<Candidate> cycleEdges;
				for (std::size_t index = 1; index < cycle.size(); index++)
				{
					const int node = cycle[index - 1];
					const int dependency = cycle[index];
					const bool isRequired = contains(requiredDependencies[node], dependency);
					cycleEdges.push_back({ node, dependency, isRequired ? Detail::EdgeKind::required : Detail::EdgeKind::preferred });
				}
				{
					const int node = cycle.back();
					const int dependency = cycle.front();
					const bool isRequired = contains(requiredDependencies[node], dependency);
					cycleEdges.push_back({ node, dependency, isRequired ? Detail::EdgeKind::required : Detail::EdgeKind::preferred });
				}

				// preferred edges are cheaper to break than required ones
				const auto isPreferred = [](const Candidate& candidate)
				{
					return candidate.kind == Detail::EdgeKind::preferred;
				};

				// break the edge that affects the fewest other nodes
				const auto affected = [&](const Candidate& candidate)
				{
					int affectedCount = 0;
					for (const int dependent : dependents[candidate.dependency])
					{
						if (!emitted[dependent])
							affectedCount++;
					}
					return affectedCount;
				};

				const auto chosen = std::ranges::min_element(cycleEdges,
					[&](const Candidate& left, const Candidate& right)
					{
						const bool leftPreferred = isPreferred(left);
						const bool rightPreferred = isPreferred(right);
						if (leftPreferred != rightPreferred)
							return leftPreferred;
						return affected(left) < affected(right);
					});

				const int node = chosen->node;
				const int dependency = chosen->dependency;

				if (chosen->kind == Detail::EdgeKind::required)
				{
					const auto position = std::ranges::find(requiredDependencies[node], dependency);
					if (position != requiredDependencies[node].end())
					{
						const auto index = static_cast<std::size_t>(position - requiredDependencies[node].begin());
						requiredActive[node][index] = 0;
						pendingRequired[node]--;
					}
					result.droppedRequired.emplace_back(node, dependency);
				}
				else
				{
					const auto position = std::ranges::find(preferredDependencies[node], dependency);
					if (position != preferredDependencies[node].end())
					{
						const auto index = static_cast<std::size_t>(position - preferredDependencies[node].begin());
						preferredActive[node][index] = 0;
						pendingPreferred[node]--;
					}
					result.droppedPreferred.emplace_back(node, dependency);
				}

				// remove the edge from the dependency list so it is not followed again
				const auto position = std::ranges::find(dependencies[node], dependency);
				if (position != dependencies[node].end())
					dependencies[node].erase(position);

				continue;
			}

			// prefer the node that satisfies the most preferred edges, this keeps
			// the number of "unknown type name" errors as low as possible
			const auto score = [&](int node)
			{
				return -preferredDependentCount[node];
			};

			const int node = *std::ranges::min_element(ready,
				[&](int left, int right)
				{
					if (score(left) != score(right))
						return score(left) < score(right);
					return left < right;
				});

			result.order.push_back(node);
			emitted[node] = 1;

			for (const int dependent : dependents[node])
			{
				if (emitted[dependent])
					continue;

				const auto requiredPosition = std::ranges::find(requiredDependencies[dependent], node);
				if (requiredPosition != requiredDependencies[dependent].end())
				{
					const auto index = static_cast<std::size_t>(requiredPosition - requiredDependencies[dependent].begin());
					if (requiredActive[dependent][index])
					{
						requiredActive[dependent][index] = 0;
						pendingRequired[dependent]--;
					}
				}

				const auto preferredPosition = std::ranges::find(preferredDependencies[dependent], node);
				if (preferredPosition != preferredDependencies[dependent].end())
				{
					const auto index = static_cast<std::size_t>(preferredPosition - preferredDependencies[dependent].begin());
					if (preferredActive[dependent][index])
					{
						preferredActive[dependent][index] = 0;
						pendingPreferred[dependent]--;
						preferredDependentCount[node]--;
					}
				}
			}
		}

		// report the edges that are not honoured by the final order
		std::vector<int> position(count, -1);
		for (std::size_t index = 0; index < result.order.size(); index++)
			position[result.order[index]] = static_cast<int>(index);

		for (std::size_t node = 0; node < count; node++)
		{
			if (position[node] < 0)
				continue;

			for (const int dependency : requiredDependencies[node])
			{
				if (position[dependency] > position[node])
					result.unsatisfiedRequired.emplace_back(static_cast<int>(node), dependency);
			}

			for (const int dependency : preferredDependencies[node])
			{
				if (position[dependency] > position[node])
					result.unsatisfiedPreferred.emplace_back(static_cast<int>(node), dependency);
			}
		}

		return result;
	}
}
