#pragma once
#include "stdafx.h"
#include "Engine/Core/EngineStructs.h"
#include "Engine/Core/PackageDependencyOrder.h"
#include "Frontend/Windows/LogWindow.h"
#include "Engine/Core/Core.h"

struct MergedPackage
{
	std::vector<EngineStructs::Package*> mergedPackages; //all packages this package contains
	EngineStructs::Package package; //the new package
};

namespace PackageSorter
{
	/**
	 * \brief How strongly a referenced type is needed by the generated C++.
	 */
	enum class TypeUsage
	{
		none,        ///< the generated code declares the type itself, no ordering needed
		declaration, ///< a forward declaration is enough
		complete     ///< the type has to be fully defined before the reference
	};

	/**
	 * \brief How a container template of BasicType.h stores its element type.
	 */
	enum class ElementStorage
	{
		none,    ///< the template does not use the element type at all
		pointer, ///< the element is only stored as a pointer
		byValue  ///< the element is stored by value, so it has to be complete
	};

	/**
	 * \brief Storage rule of the container templates of BasicType.h.
	 */
	inline ElementStorage ContainerStorage(const std::string& containerName)
	{
		// these only keep a pointer or an index of the element
		if (containerName == "TArray" || containerName == "TWeakObjectPtr" ||
			containerName == "TSoftObjectPtr" || containerName == "TLazyObjectPtr" ||
			containerName == "TScriptInterface" || containerName == "TFieldPath" ||
			containerName == "TEnumAsByte" || containerName == "TObjectPtr" ||
			containerName == "TSubclassOf")
			return ElementStorage::pointer;

		// these are opaque and never mention the element type
		if (containerName == "TSet" || containerName == "TOptional" ||
			containerName == "FDelegate" || containerName == "FMulticastDelegate")
			return ElementStorage::none;

		// TMap, TPair, TSetElement and everything unknown store by value
		return ElementStorage::byValue;
	}

	/**
	 * \brief Collects the reflected types a type expression depends on.
	 *
	 * \param type the type expression of a member, parameter or return value
	 * \param needsCompleteType true when the expression is used by value, so
	 *        the referenced type has to be fully defined
	 * \param structs receives the referenced structs
	 * \param enums receives the referenced enums
	 * \return how strongly the referenced types are needed
	 */
	inline TypeUsage CollectTypeUsage(const fieldType& type, bool needsCompleteType,
		std::vector<EngineStructs::Struct*>& structs, std::vector<EngineStructs::Enum*>& enums)
	{
		if (!type.clickable)
			return TypeUsage::none;

		// object and class properties are written as "class X*" / "struct X*".
		// The elaborated type specifier declares the type on its own, so the
		// package that owns it does not have to be written first.
		if (type.propertyType == PropertyType::ObjectProperty || type.propertyType == PropertyType::ClassProperty)
			return TypeUsage::none;

		if (!type.subTypes.empty())
		{
			// a container template, the element types decide what is needed
			const ElementStorage storage = ContainerStorage(type.name);
			if (storage == ElementStorage::none)
				return TypeUsage::none;

			TypeUsage result = TypeUsage::none;
			for (const auto& sub : type.subTypes)
			{
				const TypeUsage usage = CollectTypeUsage(sub, storage == ElementStorage::byValue, structs, enums);
				if (usage == TypeUsage::complete)
					result = TypeUsage::complete;
				else if (usage == TypeUsage::declaration && result != TypeUsage::complete)
					result = TypeUsage::declaration;
			}
			return result;
		}

		if (!type.info || !type.info->valid)
			return TypeUsage::none;

		if (type.info->type == ObjectInfo::OI_Enum)
			enums.push_back(static_cast<EngineStructs::Enum*>(type.info->target));
		else
			structs.push_back(static_cast<EngineStructs::Struct*>(type.info->target));

		return needsCompleteType ? TypeUsage::complete : TypeUsage::declaration;
	}

	/**
	 * \brief Collects the types a struct depends on.
	 */
	inline void CollectStructDependencies(const EngineStructs::Struct* struc,
		std::vector<EngineStructs::Struct*>& required,
		std::vector<EngineStructs::Struct*>& preferred,
		std::vector<EngineStructs::Enum*>& requiredEnums,
		std::vector<EngineStructs::Enum*>& preferredEnums)
	{
		// the base class has to be a complete type
		if (struc->inherited && !struc->supers.empty() && struc->supers[0])
			required.push_back(struc->supers[0]);

		// members that are used by value need a complete type
		for (const auto& member : struc->definedMembers)
		{
			std::vector<EngineStructs::Struct*> structs;
			std::vector<EngineStructs::Enum*> enums;
			const TypeUsage usage = CollectTypeUsage(member.type, true, structs, enums);
			if (usage == TypeUsage::none)
				continue;

			if (usage == TypeUsage::complete)
			{
				required.insert(required.end(), structs.begin(), structs.end());
				requiredEnums.insert(requiredEnums.end(), enums.begin(), enums.end());
			}
			else
			{
				preferred.insert(preferred.end(), structs.begin(), structs.end());
				preferredEnums.insert(preferredEnums.end(), enums.begin(), enums.end());
			}
		}

		// function signatures are only declarations, so a forward declaration
		// of the referenced type is enough
		for (const auto& func : struc->functions)
		{
			std::vector<EngineStructs::Struct*> structs;
			std::vector<EngineStructs::Enum*> enums;

			CollectTypeUsage(func.returnType, false, structs, enums);
			for (const auto& param : func.params)
				CollectTypeUsage(std::get<0>(param), false, structs, enums);

			preferred.insert(preferred.end(), structs.begin(), structs.end());
			preferredEnums.insert(preferredEnums.end(), enums.begin(), enums.end());
		}
	}

	/**
	 * \brief Orders the types of a single package so that every type is
	 * written after the types it depends on.
	 *
	 * Enums are always written at the top of a package, so they never take
	 * part in the ordering of the structs and classes.
	 */
	inline void SortPackageTypes(EngineStructs::Package& package)
	{
		std::vector<EngineStructs::Struct*>& types = package.combinedStructsAndClasses;
		if (types.size() < 2)
			return;

		PackageDependencyOrder::Graph graph;
		graph.names.reserve(types.size());
		graph.required.resize(types.size());
		graph.preferred.resize(types.size());

		std::unordered_map<const EngineStructs::Struct*, int> index;
		for (std::size_t i = 0; i < types.size(); i++)
		{
			graph.names.push_back(types[i]->cppName);
			index[types[i]] = static_cast<int>(i);
		}

		const auto addDependencies = [&](std::size_t node, const std::vector<EngineStructs::Struct*>& structs)
		{
			for (const EngineStructs::Struct* dependency : structs)
			{
				const auto position = index.find(dependency);
				if (position == index.end())
					continue; // the dependency lives in another package
				const int dependencyIndex = position->second;
				if (dependencyIndex == static_cast<int>(node))
					continue;
				graph.required[node].push_back(dependencyIndex);
			}
		};

		for (std::size_t i = 0; i < types.size(); i++)
		{
			std::vector<EngineStructs::Struct*> required;
			std::vector<EngineStructs::Struct*> preferred;
			std::vector<EngineStructs::Enum*> requiredEnums;
			std::vector<EngineStructs::Enum*> preferredEnums;

			CollectStructDependencies(types[i], required, preferred, requiredEnums, preferredEnums);

			addDependencies(i, required);

			// the preferred dependencies are added as preferred edges
			for (const EngineStructs::Struct* dependency : preferred)
			{
				const auto position = index.find(dependency);
				if (position == index.end())
					continue;
				const int dependencyIndex = position->second;
				if (dependencyIndex == static_cast<int>(i))
					continue;
				graph.preferred[i].push_back(dependencyIndex);
			}
		}

		const PackageDependencyOrder::Result result = PackageDependencyOrder::Sort(graph);

		std::vector<EngineStructs::Struct*> ordered;
		ordered.reserve(types.size());
		for (const int node : result.order)
		{
			if (node < 0 || node >= static_cast<int>(types.size()))
				continue;
			ordered.push_back(types[node]);
		}

		// never lose a type, the order is only a reordering
		if (ordered.size() != types.size())
		{
			for (EngineStructs::Struct* type : types)
			{
				if (std::ranges::find(ordered.begin(), ordered.end(), type) == ordered.end())
					ordered.push_back(type);
			}
		}

		types = ordered;
	}

	/**
	 * \brief Builds the dependency graph of the packages.
	 */
	inline PackageDependencyOrder::Graph BuildPackageGraph(std::vector<MergedPackage>& packages)
	{
		PackageDependencyOrder::Graph graph;
		graph.names.reserve(packages.size());
		graph.required.resize(packages.size());
		graph.preferred.resize(packages.size());

		// every original package belongs to exactly one merged package
		std::unordered_map<const EngineStructs::Package*, int> packageIndex;
		for (std::size_t i = 0; i < packages.size(); i++)
		{
			for (const EngineStructs::Package* merged : packages[i].mergedPackages)
				packageIndex[merged] = static_cast<int>(i);
		}

		for (std::size_t i = 0; i < packages.size(); i++)
		{
			graph.names.push_back(packages[i].package.packageName);

			const auto addPackage = [&](const EngineStructs::Package* dependency, bool required)
			{
				if (!dependency)
					return;
				const auto position = packageIndex.find(dependency);
				if (position == packageIndex.end())
					return;
				if (position->second == static_cast<int>(i))
					return;
				if (required)
					graph.required[i].push_back(position->second);
				else
					graph.preferred[i].push_back(position->second);
			};

			for (const EngineStructs::Struct* struc : packages[i].package.combinedStructsAndClasses)
			{
				std::vector<EngineStructs::Struct*> required;
				std::vector<EngineStructs::Struct*> preferred;
				std::vector<EngineStructs::Enum*> requiredEnums;
				std::vector<EngineStructs::Enum*> preferredEnums;

				CollectStructDependencies(struc, required, preferred, requiredEnums, preferredEnums);

				for (const EngineStructs::Struct* dependency : required)
					addPackage(dependency->owningPackage, true);
				for (const EngineStructs::Struct* dependency : preferred)
					addPackage(dependency->owningPackage, false);
				for (const EngineStructs::Enum* dependency : requiredEnums)
					addPackage(dependency->owningPackage, true);
				for (const EngineStructs::Enum* dependency : preferredEnums)
					addPackage(dependency->owningPackage, false);
			}
		}

		return graph;
	}
}

inline std::vector<MergedPackage*> sortPackages(int& progressDone, int& totalProgress, std::vector<MergedPackage>& newPackages)
{

	//first we cast all packages to merged packages
	for (auto& pack : EngineCore::getPackages())
	{
		MergedPackage p;
		p.mergedPackages = { &pack };
		p.package = pack;
		newPackages.push_back(p);
	}

	totalProgress = newPackages.size();
	windows::LogWindow::Log(windows::LogWindow::logLevels::LOGLEVEL_INFO,
		"SORTER", "Package merging disabled; preserving %zu package files.", newPackages.size());

	progressDone = 0;
	totalProgress = newPackages.size();

	//now we reorder the structs inside the package so the compiler doesnt throw errors
	windows::LogWindow::Log(windows::LogWindow::logLevels::LOGLEVEL_INFO, "SORTER", "Reordering structs");

	for (auto& [mergedPackages, package] : newPackages)
	{
		PackageSorter::SortPackageTypes(package);
		progressDone++;
	}

	windows::LogWindow::Log(windows::LogWindow::logLevels::LOGLEVEL_INFO, "MDK GEN", "Reordering packages");

	const PackageDependencyOrder::Graph packageGraph = PackageSorter::BuildPackageGraph(newPackages);
	const PackageDependencyOrder::Result packageResult = PackageDependencyOrder::Sort(packageGraph);

	for (const auto& [node, dependency] : packageResult.droppedRequired)
	{
		windows::LogWindow::Log(windows::LogWindow::logLevels::LOGLEVEL_ERROR, "MDK GEN",
			"Unable to order %s before %s, the dependency cycle cannot be resolved.",
			packageGraph.names[node].c_str(), packageGraph.names[dependency].c_str());
	}

	for (const auto& [node, dependency] : packageResult.droppedPreferred)
	{
		windows::LogWindow::Log(windows::LogWindow::logLevels::LOGLEVEL_WARNING, "MDK GEN",
			"Dependency cycle: %s is written before %s, the type has to be declared before it is used.",
			packageGraph.names[dependency].c_str(), packageGraph.names[node].c_str());
	}

	std::vector<MergedPackage*> orderedPackages;
	orderedPackages.reserve(newPackages.size());

	for (const int node : packageResult.order)
	{
		if (node < 0 || node >= static_cast<int>(newPackages.size()))
			continue;
		orderedPackages.push_back(&newPackages[node]);
	}

	// never lose a package, the order is only a reordering
	for (auto& package : newPackages)
	{
		if (std::ranges::find(orderedPackages.begin(), orderedPackages.end(), &package) == orderedPackages.end())
			orderedPackages.push_back(&package);
	}

	progressDone = totalProgress;

	return orderedPackages;
}
