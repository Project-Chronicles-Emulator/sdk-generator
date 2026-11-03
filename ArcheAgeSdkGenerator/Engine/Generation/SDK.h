#pragma once
#include "stdafx.h"
#include "Engine/Core/EngineStructs.h"

class SDKGeneration
{
	//whether we already initilaized and dumped all classes
	static inline bool completed;

	static inline std::filesystem::path SDKPath{};

	static inline std::vector<std::string> allnames{};

	static inline int undefinedCnt = 0;

	static void writeBasicType(std::ostream& stream);

	static void generateBasicType();

	// Emits SDK/EngineInternals.h: a single big copy-pasteable header with the
	// engine-side C++ struct layouts UEDumper uses (UObject/UField/UStruct/UClass/
	// UFunction/UEnum/FField/FProperty/FName/FText/FUObjectArray/...), the resolved
	// GNAMES/GOBJECTS/GWORLD offsets, and curated lists of important inheriting
	// classes (AActor, ACharacter, UBlueprintFunctionLibrary, UGameInstance, ...).
	static bool generateEngineInternals();


public:
	SDKGeneration();

	static void printCredits(std::ostream& stream);

	static void generatePackage(std::ostream& stream, const EngineStructs::Package& package, int featureFlags, std::unordered_map<std::string, std::string> &originalPackageToMerged);

	static bool Generate(int& progressDone, int& totalProgress, int featureFlags);
};