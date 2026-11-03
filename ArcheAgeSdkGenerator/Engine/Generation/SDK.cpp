#include "SDK.h"


#include "Engine/structs.h"
#include "Engine/Core/Core.h"
#include "Engine/Core/EngineStructs.h"
#include "Engine/Userdefined/Datatypes.h"
#include "Frontend/Windows/LogWindow.h"
#include "Settings/EngineSettings.h"
#include "BasicType.h"
#include "packageSorter.h"
#include "Engine/Userdefined/FeatureFlags.h"
#include "Engine/Userdefined/Offsets.h"
#include "Engine/Userdefined/UEdefinitions.h"
#include "Memory/memory.h"
#include "IDAExporter.h"

#include <algorithm>
#include <sstream>
#include <unordered_set>

void SDKGeneration::printCredits(std::ostream& stream)
{
    stream <<
        R"(
/********************************************************
*                                                       *
*   Package generated using UEDumper by Spuckwaffel.    *
*                                                       *
********************************************************/

)";
}

void SDKGeneration::writeBasicType(std::ostream& BasicType)
{
    const auto& defs = basicDefinitions();
    auto noDefs = EngineCore::getAllUnknownTypes();
    for (const auto& [name, definition] : defs)
        if (auto it = std::ranges::find(noDefs, name); it != noDefs.end())
            noDefs.erase(it);

    BasicType << "#pragma once" << std::endl;
    BasicType << "#ifndef _IDA_\n#include <cstdint>\n#include <locale>\n#include <string>\n#endif" << std::endl;

    printCredits(BasicType);
    BasicType << "/// This file contains all definitions of structs that werent defined automatically.\n\n";

    auto addIfCustom = [&](const char* base, const char* user)
    {
        if (strcmp(base, user) != 0)
        {
            BasicType << "typedef " << base << " " << user << ";\n";
        }
    };

    addIfCustom("bool", TYPE_BOOLEAN);
    addIfCustom("unsigned char", TYPE_UCHAR);
    addIfCustom("char", TYPE_CHAR);
    addIfCustom("uint8_t", TYPE_UI8);
    addIfCustom("uint16_t", TYPE_UI16);
    addIfCustom("uint32_t", TYPE_UI32);
    addIfCustom("uint64_t", TYPE_UI64);
    addIfCustom("int8_t", TYPE_I8);
    addIfCustom("int16_t", TYPE_I16);
    addIfCustom("int32_t", TYPE_I32);
    addIfCustom("int64_t", TYPE_I64);
    addIfCustom("float", TYPE_FLOAT);
    addIfCustom("double", TYPE_DOUBLE);

    BasicType << "\n\n";

	// Native FField classes are template arguments, not reflected UObject definitions.
	std::set<std::string> fieldClasses;
	const auto collectFieldClasses = [&](const fieldType& type, auto& self) -> void
	{
		if (type.propertyType == PropertyType::FieldPathProperty)
		{
			for (const auto& child : type.subTypes)
				fieldClasses.insert(child.name);
		}
		for (const auto& child : type.subTypes)
			self(child, self);
	};
	for (const auto& package : EngineCore::getPackages())
	{
		for (const auto* structure : package.combinedStructsAndClasses)
		{
			for (const auto& member : structure->definedMembers)
				collectFieldClasses(member.type, collectFieldClasses);
			for (const auto& function : structure->functions)
			{
				collectFieldClasses(function.returnType, collectFieldClasses);
				for (const auto& parameter : function.params)
					collectFieldClasses(std::get<0>(parameter), collectFieldClasses);
			}
		}
	}
	for (const auto& name : fieldClasses)
		BasicType << "class " << name << ";\n";

    if (noDefs.size() > 0)
        BasicType << "///\n/// THERE ARE MISSING STRUCTS!! This will result in members having the SDK_UNDEFINED pragma!!!\n///\n\n";

    for (const auto& missingDef : noDefs)
    {
        windows::LogWindow::Log(windows::LogWindow::logLevels::LOGLEVEL_WARNING, "SDK GEN", "WARNING: SDK ERRORS WILL APPEAR!! Missing definition for struct %s!!!", missingDef.c_str());
        BasicType << "//TODO: Define " << missingDef << "!\n\n\n";
    }

    for (const auto& [name, definition] : defs)
    {
        BasicType << "/// Definition for " << name << "\n";
        BasicType << definition << "\n\n";
    }
}

void SDKGeneration::generateBasicType()
{
    std::ofstream BasicType(SDKPath / "BasicType.h");
    writeBasicType(BasicType);
    BasicType.close();
}


void SDKGeneration::generatePackage(
    std::ostream& stream,
    const EngineStructs::Package& package,
    int featureFlags,
    std::unordered_map<std::string, std::string> &originalPackageToMerged
) {
    stream << "#pragma once\n";
    //flag some invalid characters in a name
    auto generateValidVarName = [](const std::string& str, bool allowBrackets = false)
    {
        std::string result = "";
        for (const char c : str)
        {
            if (c == ' ')
                continue;
            if (static_cast<int>(c) < 0 || !std::isalnum(c))
            {
                if(allowBrackets && (c == '[' ||  c == ']'))
                    result += c;
                else
					result += '_';
            }
                
            else
                result += c;

        }

        const static std::unordered_set<std::string> reservedNames{ "float", "int", "bool", "double", "long", "char", "TRUE", "FALSE", "try" };

        if (result.empty()) return std::string("noname");
        if (std::isdigit(static_cast<unsigned char>(result[0]))) result = "_" + result;
        if (reservedNames.contains(result)) result += "0";

        //guaranteed 0 termination
        return result;
    };

    if (featureFlags & FeatureFlags::SDK::ADD_INCLUDES)
    {
        for (auto& dependencies : package.dependencyPackages)
        {
            auto packageName = dependencies->packageName;
            if (originalPackageToMerged.contains(packageName))
                packageName = originalPackageToMerged[packageName];

            stream << "#include \"" << packageName << ".h\"" << std::endl;
        }
    }
    else
    {
        for (auto& dependencies : package.dependencyPackages)
        {
            stream << "/// dependency: " << dependencies->packageName << std::endl;
        }
    }

    stream << "\n";

    stream << "#pragma pack(push, 0x1)\n" << std::endl;

    auto generateForwardDecls = [&](const std::vector<EngineStructs::Struct*>& DataStruc)
    {
        std::set<std::string> forwardDeclTypes; // ordered in case SDK is in VCS
        for (const auto& struc : DataStruc)
        {
            if (!struc->isClass) continue;

            forwardDeclTypes.insert("class " + generateValidVarName(struc->cppName));
        }

        for (auto decl = forwardDeclTypes.begin(); decl != forwardDeclTypes.end(); decl++)
        {
            stream << *decl << ";" << std::endl;
        }
    };

    auto areAnyMembersUndefined = [&](EngineStructs::Member* member)
    {
        bool anyUndef = !member->type.info || !member->type.info->valid;
        if (!anyUndef && member->type.subTypes.size() > 0)
        {
            auto checkAllSubs = [&](const fieldType& type, auto& self) -> void
                {
                    for (auto& sub : type.subTypes)
                    {
                        if (sub.clickable && (!sub.info || !sub.info->valid))
                        {
                            anyUndef = true;
                            break;
                        }
                        if (sub.propertyType == PropertyType::Unknown)
                        {
                            anyUndef = true;
                            break;
                        }
                        if (sub.subTypes.size() > 0)
                            self(sub, self);
                    }
                };
            checkAllSubs(member->type, checkAllSubs);
        }

        return anyUndef;
    };

    auto areAnyParamsUndefined = [&](EngineStructs::Function func)
    {
        std::unordered_set<std::string> noDefs;
        for (auto type : EngineCore::getAllUnknownTypes()) noDefs.insert(type);

        bool anyUndef = false;

        auto checkAllSubs = [&](const fieldType& type, auto& self) -> void
        {
            for (auto& sub : type.subTypes)
            {
                if (noDefs.contains(sub.name)) {
                    anyUndef = true;
                    break;
                }
                if (sub.clickable && (!sub.info || !sub.info->valid))
                {
                    anyUndef = true;
                    break;
                }
                if (sub.propertyType == PropertyType::Unknown)
                {
                    anyUndef = true;
                    break;
                }
                if (sub.subTypes.size() > 0)
                    self(sub, self);
            }
        };

        if (func.returnType.propertyType == PropertyType::Unknown) anyUndef = true;
        else if (noDefs.contains(func.returnType.name)) anyUndef = true;
        else
        {
            for (auto param : func.params)
            {
                if (
                    std::get<0>(param).propertyType == PropertyType::Unknown ||
                    noDefs.contains(std::get<0>(param).name)
                ) {
                    anyUndef = true;
                    break;
                }
                else
                {
                    checkAllSubs(std::get<0>(param), checkAllSubs);
                }
            }
        }
        if (!anyUndef && func.returnType.subTypes.size() > 0)
        {
            checkAllSubs(func.returnType, checkAllSubs);            
        }

        return anyUndef;
    };

    std::unordered_set<const EngineStructs::Struct*> emittedHere;

    auto generateStaticAssertsForObjectSizes = [&](const std::vector<EngineStructs::Struct*>& DataStruc)
    {
        for (const auto& struc : DataStruc)
        {
            if (!emittedHere.contains(struc)) continue;
            char buf[1024] = { 0 };

            //maxsize has been decresed to 0 because of a super struct, we have to "fix it"
            //for the static assert as single structs with no member cant be 0 bytes

            uint64_t actualSize = struc->maxSize;

            if(struc->size == 1 && struc->maxSize == 0)
                actualSize = 1;

            sprintf_s(buf, "static_assert(sizeof(%s) == 0x%04llX); // %d bytes (0x%06X - 0x%06llX)", generateValidVarName(struc->cppName).c_str(), actualSize, struc->maxSize, struc->getInheritedSize(), actualSize);
            stream << buf << std::endl;
        }
    };

    auto generateStaticAssertsForMembers = [&](const std::vector<EngineStructs::Struct*>& DataStruc)
        {
            for (const auto& struc : DataStruc)
            {
                if (!emittedHere.contains(struc)) continue;
                const auto klass = generateValidVarName(struc->cppName);
                std::vector<std::string> usedNames{};

                int j = 0;
                for (int i = 0; i < struc->cookedMembers.size(); i++)
                {
                    const auto member = struc->getMemberForIndex(i);
                    std::string name = generateValidVarName(member->name, true);

                    if (std::isdigit(name[0]))
                        name = "_" + name;

                    if (name.empty())
                        name = "noname";

                    if (std::ranges::find(usedNames, name) != usedNames.end())
                        name += std::to_string(j++);

                    name = generateValidVarName(name);

                    usedNames.push_back(name);

                    if (areAnyMembersUndefined(member)) continue;

                    char buf[1024] = { 0 };
                    sprintf_s(buf, "static_assert(offsetof(%s, %s) == 0x%04X);", klass.c_str(), name.c_str(), member->offset);
                    stream << buf << std::endl;
                }
            }
        };

    auto generateStruct = [&](const std::vector<EngineStructs::Struct*>& DataStruc)
    {
        for (const auto& struc : DataStruc)
        {
            if (std::ranges::find(allnames, generateValidVarName(struc->cppName)) != allnames.end())
                continue;
            allnames.push_back(generateValidVarName(struc->cppName));
            emittedHere.insert(struc);

            if (struc->isClass)
                stream << "/// Class " << struc->fullName << std::endl;
            else
            {
                stream << "/// Struct " << struc->fullName << std::endl;
            }
            char buf[256] = { 0 };
            sprintf_s(
                buf, 
                "Size: 0x%04X (%d bytes) (0x%06X - 0x%06X) align %s MaxSize: 0x%04X",
                struc->size,
                struc->size,
                struc->getInheritedSize(), 
                struc->size, 
                (struc->minAlignment > 0 ? std::to_string(struc->minAlignment) : "n/a").c_str(),
                struc->maxSize
            );
            stream << "/// " << buf << std::endl;

            if (struc->isClass)
                stream << "class ";
            else
                stream << "struct ";


            /*if (needsHelp)
            {
                char buf1[100] = { 0 };
                sprintf_s(buf1, "alignas(0x%X) ", struc->minAlignment);
                stream << buf1;
            }*/

            stream << generateValidVarName(struc->cppName);

            if (struc->inherited && struc->isClass)
                stream << " : public " << generateValidVarName(struc->supers[0]->cppName);
            else if (struc->inherited)
                stream << " : " << generateValidVarName(struc->supers[0]->cppName);


            stream << "\n{ " << std::endl;

            if (struc->isClass)
                stream << "public:" << std::endl;

            std::vector<std::string> usedNames{};


            int j = 0;

            for (int i = 0; i < struc->cookedMembers.size(); i++)
            {
                const auto member = struc->getMemberForIndex(i);
                char finalBuf[600];
                char nameBuf[500];
                std::string name = generateValidVarName(member->name, true);

                if (name.empty())
                    name = "noname";

                else if (std::isdigit(name[0]))
                    name = "_" + name;

                if (std::ranges::find(usedNames, name) != usedNames.end())
                    name += std::to_string(j++);



                usedNames.push_back(name);

                std::string memberType = member->type.stringify().c_str();

                if (member->type.clickable && areAnyMembersUndefined(member))
                {
                    // Keep the exact size with an opaque byte array so the layout and
                    // static asserts stay valid; the reflected type is kept as a comment.
                    undefinedCnt++;
                    memberType = "uint8_t /* UNRESOLVED: " + memberType + " */";
                    name += "[0x" + [](int v) { char b[16]; sprintf_s(b, "%X", v); return std::string(b); }(member->size) + "]";
                } else if (member->arrayDim > 1) {
                    name += "[" + std::to_string(member->arrayDim) + "]";
                }

                if (member->isBit)
                    name += " : 1";
                sprintf_s(nameBuf, "%-50s %s;", memberType.c_str(), name.c_str());
                if (member->isBit)
                    sprintf_s(finalBuf, "	%-110s // 0x%04X:%d (0x%04X) ", nameBuf, member->offset, member->bitOffset, member->size);
                else
                    sprintf_s(finalBuf, "	%-110s // 0x%04X   (0x%04X) ", nameBuf, member->offset, member->size);
                stream << finalBuf << " "; // << static_cast<int>(member->type.propertyType);
                if (member->userEdited)
                    stream << "USER-MODIFIED";
                else if (member->missed)
                    stream << "MISSED";
                stream << std::endl;
            }

            // Add function section header
            if (!struc->functions.empty())
            {
                stream << "\n\n\t/// Functions" << std::endl;
            }

            uint32_t vtableIndex = 0;
            std::unordered_set<std::string> alreadyGeneratedFunctions;
            for (const auto& func : struc->functions)
            {
                if (alreadyGeneratedFunctions.contains(func.fullName)) {
                    // some blueprint functions will be duplicated with exact same signatures, offsets, the lot
                    windows::LogWindow::Log(windows::LogWindow::logLevels::LOGLEVEL_WARNING, "SDK GEN", "WARNING: Duplicate function %s in struct %s. Skipping...", func.fullName.c_str(), struc->fullName.c_str());
                    continue;
                }
                alreadyGeneratedFunctions.insert(func.fullName);

                static char funcBuf[0x10000];
                const auto bAddPrefix = featureFlags & FeatureFlags::SDK::FUNCTION_BODIES ? false : true;

                std::string params = "" + func.returnType.stringify(bAddPrefix) + " " + generateValidVarName(func.cppName).c_str() + "(";
                std::string funcPtrTypes = "";
                std::string funcPtrParams = "";

                std::unordered_set<std::string> alreadyUsedNames;

                for (auto param : func.params)
                {
                    params += std::get<0>(param).stringify(bAddPrefix);
                    funcPtrTypes += std::get<0>(param).stringify(bAddPrefix);

                    if (std::get<3>(param) > 1)
                    {
                        params += "*";
                        funcPtrTypes += "*";
                    }
                    else if (std::get<2>(param) & EPropertyFlags::CPF_OutParm)
                    {
                        params += "&";
                        funcPtrTypes += "&";
                    }

                    std::string paramName = generateValidVarName(std::get<1>(param));

                    // some func signatures re-use the variable name, so we need to make them unique
                    int duplicateParamCounter = 0;
                    while (alreadyUsedNames.contains(paramName)) paramName = generateValidVarName(std::get<1>(param)) + std::to_string(++duplicateParamCounter);
                    alreadyUsedNames.insert(paramName);

                    params += " " + paramName + ", ";
                    funcPtrTypes += ", ";
                    funcPtrParams += paramName + ", ";
                }
                if (func.params.size() > 0)
                {
                    params = params.erase(params.size() - 2);
                    funcPtrTypes = funcPtrTypes.erase(funcPtrTypes.size() - 2);
                    funcPtrParams = funcPtrParams.erase(funcPtrParams.size() - 2);
                }

                if (featureFlags & FeatureFlags::SDK::FUNCTION_BODIES && !areAnyParamsUndefined(func))
                {
                    params += ")";
                    sprintf_s(funcBuf, "	%s // [0x%llx] %-20s ", params.c_str(), func.binaryOffset, func.functionFlags.c_str());
                    stream << funcBuf << std::endl;

                    bool isVoidFunc = func.returnType.stringify() == "void";

                char funcBody[1200];
                sprintf_s(funcBody, R"EOF(	{
		typedef %s (*FuncPtr)(%s);
		auto vtablePtr = reinterpret_cast<uintptr_t*>(vtable);
		auto func = reinterpret_cast<FuncPtr>(vtablePtr[%d]);
		%sfunc(%s);
	})EOF", func.returnType.stringify().c_str(), funcPtrTypes.c_str(), vtableIndex, isVoidFunc ? "" : "return ", funcPtrParams.c_str());
                    stream << funcBody << std::endl;
                }
                else {
                    params += ");";
                    sprintf_s(funcBuf, "	%-120s // [0x%llx] %-20s ", params.c_str(), func.binaryOffset, func.functionFlags.c_str());
                    stream << funcBuf << std::endl;
                }

                vtableIndex++;

            }
        	stream << "};\n\n";


        }
    };

    if (featureFlags & FeatureFlags::SDK::FORWARD_DECLARATIONS) {
        generateForwardDecls(package.combinedStructsAndClasses);
        stream << "\n";
    }

    //first we generate enums

    std::unordered_set<std::string> definedEnums;

    for (const auto& enu : package.enums)
    {
        if (definedEnums.contains(enu.cppName))
            continue;
        definedEnums.insert(enu.cppName);

        stream << "/// Enum " << enu.fullName << std::endl;
        char buf[100] = { 0 };
        sprintf_s(buf, "Size: 0x%02d (%d bytes)", enu.size, enu.size);
        stream << "/// " << buf << std::endl;
        stream << "enum class " << enu.cppName << " : " << enu.type << std::endl;
        stream << "{" << std::endl;

        int j = 0;
        std::vector<std::string> usedNames{};

        for (const auto& member : enu.members)
        {
            j++;
            char memberBuf[300];

            std::string fir = generateValidVarName(member.first);

            if (std::ranges::find(usedNames, fir) != usedNames.end())
                fir += std::to_string(j);

            usedNames.push_back(fir);

            sprintf_s(memberBuf, "	%-80s = %lld%s", fir.c_str(), static_cast<long long>(member.second), j == enu.members.size() ? "" : ",");
            stream << memberBuf << std::endl;
        }
        stream << "};\n\n";
    }


    // this feature flag exists so that classes that reference structs in their call signatures
    // can be defined, without needing forward declarations of structs everywhere
    if (featureFlags & FeatureFlags::SDK::STRUCTS_BEFORE_CLASSES)
    {
        std::vector<EngineStructs::Struct*> structs;
        std::vector<EngineStructs::Struct*> classes;

        for (auto cpackage : package.combinedStructsAndClasses)
        {
            if (cpackage->isClass) classes.push_back(cpackage);
            else structs.push_back(cpackage);
        }

        generateStruct(structs);
        generateStruct(classes);
    }
    else
    {
        generateStruct(package.combinedStructsAndClasses);
    }

    stream << "#pragma pack(pop)\n\n" << std::endl;

    // add static asserts for objects
    if (featureFlags & FeatureFlags::SDK::STATIC_ASSERTS_OBJECT_SIZE)
        generateStaticAssertsForObjectSizes(package.combinedStructsAndClasses);
    // add static asserts for members
    if (featureFlags & FeatureFlags::SDK::STATIC_ASSERTS_MEMBERS)
        generateStaticAssertsForMembers(package.combinedStructsAndClasses);

}

// ===========================================================================
//  EngineInternals.h generator
// ---------------------------------------------------------------------------
//  Emits one big copy-pasteable C++ header containing:
//      * The UE_VERSION the SDK was generated for + image base.
//      * Resolved GNAMES / GOBJECTS / GWORLD offsets (and any other globals).
//      * Engine-side struct layouts (UObject, UField, UStruct, UClass,
//        UFunction, UEnum, FField, FFieldClass, FProperty, FName, FString,
//        FText, FUObjectItem, FUObjectArray, ...).
//      * Curated lists of important inheriting classes (AActor, ACharacter,
//        UBlueprintFunctionLibrary, UGameInstance, UPlayerController, ...).
//      * Quick reference for ULevel / UWorld / UNetDriver members so the
//        user can drop the file straight into their cheat/research project.
// ===========================================================================
namespace
{
    // Walk Struct->supers recursively and check if any ancestor cppName == target.
    bool structInheritsFromName(const EngineStructs::Struct* s, const std::string& target,
        std::unordered_set<const EngineStructs::Struct*>& visited)
    {
        if (!s || !visited.insert(s).second)
            return false;
        for (const auto* sup : s->supers)
        {
            if (!sup) continue;
            if (sup->cppName == target) return true;
            if (structInheritsFromName(sup, target, visited)) return true;
        }
        return false;
    }

    bool structInheritsFromName(const EngineStructs::Struct* s, const std::string& target)
    {
        std::unordered_set<const EngineStructs::Struct*> visited;
        return structInheritsFromName(s, target, visited);
    }

    bool isBlueprintActor(const EngineStructs::Struct* type)
    {
        return type && type->isClass &&
            type->fullName.starts_with("/Game/") &&
            type->cppName.ends_with("_C") &&
            std::ranges::find(type->superNames, "AActor") != type->superNames.end();
    }

    std::string blueprintActorFileName(const EngineStructs::Struct* type)
    {
        std::string name;
        for (const auto c : type->cppName)
            name += std::isalnum(static_cast<unsigned char>(c)) || c == '_' ? c : '_';
        return (name.empty() ? "noname" : name) + ".h";
    }

    std::string offsetFlagToText(int flag)
    {
        std::string out;
        auto add = [&](const char* s) { if (!out.empty()) out += " | "; out += s; };
        if (flag & OFFSET_LIVE_EDITOR)   add("OFFSET_LIVE_EDITOR");
        if (flag & OFFSET_DS)            add("OFFSET_DS");
        if (flag & OFFSET_ADDRESS)       add("OFFSET_ADDRESS");
        if ((flag & OFFSET_SIGNATURE) && (flag & OFFSET_SIG_DIR))  add("OFFSET_SINGNATURE_DIRECT");
        else if ((flag & OFFSET_SIGNATURE) && (flag & OFFSET_SIG_RVA)) add("OFFSET_SIGNATURE_FOLLOW");
        else if (flag & OFFSET_SIGNATURE) add("OFFSET_SIGNATURE");
        if (out.empty()) out = "0";
        return out;
    }

    // Pretty-print a member with its offset and size on the right.
    void emitMember(std::ofstream& out, const EngineStructs::Member& m)
    {
        const auto type = m.type.stringify();
        char buf[512];
        sprintf_s(buf, "    // %-60s %-40s // 0x%04X (0x%04X)\n",
            type.c_str(), (m.name + ";").c_str(), m.offset, m.size);
        out << buf;
    }

    // Find a struct by cppName across all packages. Returns nullptr if absent.
    const EngineStructs::Struct* findStructByCppName(const std::string& name)
    {
        const auto* info = EngineCore::getInfoOfObject(name);
        if (!info || !info->valid) return nullptr;
        if (info->type != ObjectInfo::OI_Class && info->type != ObjectInfo::OI_Struct)
            return nullptr;
        return static_cast<const EngineStructs::Struct*>(info->target);
    }

    // Look for a member with one of several plausible names; returns nullptr otherwise.
    const EngineStructs::Member* findAnyMember(const EngineStructs::Struct* s,
        std::initializer_list<const char*> names)
    {
        if (!s) return nullptr;
        for (const auto* needle : names)
            for (const auto& m : s->definedMembers)
                if (m.name == needle) return &m;
        return nullptr;
    }

    // Dump the listed members of a struct (skipping any that don't exist) to stream
    // as "    Type  Name; // 0x.... (0x....)" lines.
    void dumpKnownMembers(std::ofstream& out, const EngineStructs::Struct* s,
        std::initializer_list<const char*> names)
    {
        if (!s) { out << "    // (struct not found in dump)\n"; return; }
        bool any = false;
        for (const auto* needle : names)
        {
            for (const auto& m : s->definedMembers)
            {
                if (m.name == needle)
                {
                    emitMember(out, m);
                    any = true;
                    break;
                }
            }
        }
        if (!any) out << "    // (no requested members present in this dump)\n";
    }
}

bool SDKGeneration::generateEngineInternals()
{
    std::ofstream out(SDKPath / "EngineInternals.h");
    if (!out.is_open())
    {
        windows::LogWindow::Log(windows::LogWindow::logLevels::LOGLEVEL_WARNING,
            "SDK GEN", "Could not open EngineInternals.h for writing!");
        return false;
    }

    const auto gNamesOff   = EngineCore::getOffsetForName("OFFSET_GNAMES");
    const auto gObjectsOff = EngineCore::getOffsetForName("OFFSET_GOBJECTS");
    const auto gWorldOff   = EngineCore::getOffsetForName("OFFSET_GWORLD");
    const auto base        = Memory::getBaseAddress();

    // -------------------- banner --------------------
    out <<
        R"(// ===========================================================================
//  EngineInternals.h  -  generated by UEDumper
// ---------------------------------------------------------------------------
//  This is a single, self-contained copy-paste header containing the
//  engine-side C++ struct layouts that UEDumper used to parse the target
//  process, plus the resolved global offsets and curated class lists.
//
//  Drop this file into your own project to get a quick "engine kit":
//      * Match-the-game UObject / UField / UStruct / UClass / UFunction / UEnum.
//      * FField / FFieldClass / FProperty skeletons (UE 4.25+).
//      * FName / FUObjectItem / FUObjectArray as actually laid out in memory.
//      * GNAMES / GOBJECTS / GWORLD relative offsets.
//      * Lists of useful classes (AActor / ACharacter children,
//        UBlueprintFunctionLibrary children, UGameInstance, UPlayerController...).
//
//  NOTE: types here intentionally mirror what UEDumper reads. They are NOT a
//  full Unreal Engine reimplementation - they are just enough to walk objects.
// ===========================================================================
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace ArcheAgeInternals
{

)";

    // -------------------- engine version + image base --------------------
    out << "// ---------------------------------------------------------------\n";
    out << "// Build info\n";
    out << "// ---------------------------------------------------------------\n";
    out << "// UE_VERSION constant (see UEdefinitions.h): " << UE_VERSION << "\n";
    out << "//   UE_4_20=1, UE_4_25=6, UE_4_27=8, UE_5_00=9, UE_5_03=12, UE_5_05=14, UE_5_06=15\n";
    char baseBuf[64]; sprintf_s(baseBuf, "0x%llX", static_cast<unsigned long long>(base));
    out << "// Image base (target process at dump time): " << baseBuf << "\n";
    out << "// FUObjectItem size:                        0x" << std::hex << FUOBJECTITEM_SIZE << std::dec << "\n";
    out << "// FUObjectItem::Object offset within slot:  0x" << std::hex << FUOBJECTITEM_OBJECTPTR_OFFSET << std::dec << "\n\n";

    // -------------------- global offsets --------------------
    out << "// ---------------------------------------------------------------\n";
    out << "// Global offsets (relative to image base)\n";
    out << "// ---------------------------------------------------------------\n";
    auto emitOff = [&](const char* niceName, const Offset& off)
    {
        if (!off) { out << "// " << niceName << ": <not configured>\n"; return; }
        uint64_t resolvedRva = 0;
        if (Memory::getStatus() == Memory::loaded)
        {
            const auto address = EngineCore::getOffsetAddress(off);
            if (address >= base)
                resolvedRva = address - base;
        }
        char b[256];
        const int written = snprintf(b, sizeof(b), "constexpr uintptr_t %-18s = 0x%llX;   // flags: %s%s\n",
            niceName, static_cast<unsigned long long>(resolvedRva),
            offsetFlagToText(off.flag).c_str(),
            resolvedRva ? "" : " (unresolved offline)");
        out << (written > 0 ? b : "// offset emit failed\n");
    };
    emitOff("OFFSET_GNAMES",   gNamesOff);
    emitOff("OFFSET_GOBJECTS", gObjectsOff);
    emitOff("OFFSET_GWORLD",   gWorldOff);
    printf("[HEADLESS] internals: offsets emitted\n");

    // also dump any other offsets the user has registered (forward-compat).
    out << "\n// All registered offsets:\n";
    for (const auto& o : EngineCore::getOffsets())
    {
        char b[160];
        sprintf_s(b, "//   %-22s  offset=0x%llX  flag=%s\n",
            o.name.c_str(), static_cast<unsigned long long>(o.offset),
            offsetFlagToText(o.flag).c_str());
        out << b;
    }
    out << "\n";

    // -------------------- core engine structs --------------------
    out << R"(// ---------------------------------------------------------------
// Core engine struct layouts
// ---------------------------------------------------------------
//
// These mirror exactly what UEDumper reads from the target. If you are
// targeting the same UE version you can drop them straight into your project.
//

// FName layout baked for this target's UE version.
struct FName
{
    uint32_t ComparisonIndex;
)";
#if UE_FNAME_OUTLINE_NUMBER == 0
    out << "    uint32_t Number;     // not present when UE_FNAME_OUTLINE_NUMBER==1\n";
#endif
#if WITH_CASE_PRESERVING_NAME
    out << "    uint32_t DisplayIndex; // WITH_CASE_PRESERVING_NAME\n";
#endif
    out << R"(};

// Aligned to what FUObjectArray::Objects[i] actually looks like in memory.
// UE 5.6+ prepends 8 unknown bytes before the Object pointer.
struct FUObjectItem
{
)";
#if UE_VERSION >= UE_5_06
    out << "    uint64_t Unknown;          // 0x00 - present in UE 5.6+\n";
    out << "    uintptr_t Object;          // 0x08\n";
    out << "    uint8_t   UnknownTail[0x08]; // 0x10 - semantics unverified\n";
#else
    out << "    uintptr_t Object;          // 0x00\n";
    out << "    int32_t   Flags;           // 0x08\n";
    out << "    int32_t   ClusterRootIndex;// 0x0C\n";
    out << "    int32_t   SerialNumber;    // 0x10\n";
#endif
    out << "};\n";
    out << "static_assert(sizeof(FUObjectItem) == 0x" << std::hex << FUOBJECTITEM_SIZE << std::dec << ", \"FUObjectItem size mismatch\");\n\n";

    out << R"(// TUObjectArray-style chunked array used by GUObjectArray (UE 4.20+).
struct FChunkedFixedUObjectArray
{
    FUObjectItem** Objects;          // pointer-to-chunks
    FUObjectItem*  PreAllocatedObjects;
    int32_t        MaxElements;
    int32_t        NumElements;
    int32_t        MaxChunks;
    int32_t        NumChunks;
};

// The GObjects global points directly at the chunked array on this target
// (UE 5.7.4): the signature resolves to the FChunkedFixedUObjectArray itself,
// so FUObjectArray here is that array (Objects@+0x00, NumElements@+0x14).
struct FUObjectArray
{
    FUObjectItem** Objects;          // 0x00 pointer-to-chunks
    FUObjectItem*  PreAllocatedObjects; // 0x08
    int32_t        MaxElements;      // 0x10
    int32_t        NumElements;      // 0x14
    int32_t        MaxChunks;        // 0x18
    int32_t        NumChunks;        // 0x1C
};

// Minimal TArray that matches Unreal's layout (Data + size + capacity).
template <class T>
struct TArray
{
    T*      Data;
    int32_t Count;
    int32_t Max;

    int32_t  Num()        const { return Count; }
    T&       operator[](int32_t i)       { return Data[i]; }
    const T& operator[](int32_t i) const { return Data[i]; }
};

// FString is a TArray<wchar_t>.
struct FString : public TArray<wchar_t>
{
    const wchar_t* c_str() const { return Data; }
};

// FText storage for this target. The second word's semantics are unverified.
struct FText
{
    uintptr_t Data;            // 0x00 - FTextData*
    uint8_t   Unknown[0x08];   // 0x08
};

// ----- UObject hierarchy -----
class UClass;

class UObject
{
public:
    void*       VTable;          // 0x00
    int32_t     ObjectFlags;     // 0x08
    int32_t     InternalIndex;   // 0x0C
    UClass*     ClassPrivate;    // 0x10
    FName       NamePrivate;     // 0x18
    UObject*    OuterPrivate;    // 0x20
};

)";

#if UE_VERSION >= UE_4_22
    out << R"(// Private base of UStruct (NOT of UField) on UE 4.22+ - two pointers used by
// the fast IsChildOf() implementation. (USTRUCT_FAST_ISCHILDOF_IMPL controls
// whether your build actually includes it; this dump assumed it does.)
class FStructBaseChain
{
public:
    FStructBaseChain** StructBaseChainArray;
    int32_t            NumStructBasesInChainMinusOne;
};

)";
#endif

    out << R"(class UField : public UObject
{
public:
    UField* Next;                // first member after UObject
};

)";

    out << "class UStruct : public UField";
#if UE_VERSION >= UE_4_22 && USTRUCT_FAST_ISCHILDOF_IMPL
    out << ", public FStructBaseChain";
#endif
    out << "\n{\npublic:\n";
    out << "    UStruct*           SuperStruct;\n";
    out << "    UField*            Children;\n";
#if UE_VERSION >= UE_4_25
    out << "    /* FField* */ void* ChildProperties;\n";
#endif
    out << "    int32_t            PropertiesSize;\n";
#if UE_VERSION >= UE_5_07
    out << "    int16_t            MinAlignment;\n";
    out << "    uint16_t           StructStateFlags;\n";
#else
    out << "    int32_t            MinAlignment;\n";
#endif
    out << "    TArray<uint8_t>    Script;\n";
    out << "    void*              PropertyLink;       // UProperty*/FProperty*\n";
    out << "    void*              RefLink;\n";
    out << "    void*              DestructorLink;\n";
    out << "    void*              PostConstructLink;\n";
#if UE_VERSION >= UE_4_25
    out << "    void*              ScriptAndPropertyObjectReferencesData;  // TArray.Data\n";
    out << "    int32_t            ScriptAndPropertyObjectReferencesCount;\n";
    out << "    int32_t            ScriptAndPropertyObjectReferencesMax;\n";
    out << "    uintptr_t          UnresolvedScriptProperties;\n";
    out << "    uintptr_t          UnversionedGameSchema;\n";
#endif
    out << "};\n\n";

    out << R"(class UScriptStruct : public UStruct {};
class UClass        : public UStruct {};

class UFunction : public UStruct
{
public:
    uint32_t   FunctionFlags;
    uint8_t    NumParms;
    uint16_t   ParmsSize;
    uint16_t   ReturnValueOffset;
    uint16_t   RPCId;
    uint16_t   RPCResponseId;
    void*      FirstPropertyToInit;     // UProperty*/FProperty*
)";
#if UE_BLUEPRINT_EVENTGRAPH_FASTCALLS
    out << "    UFunction* EventGraphFunction;\n    int32_t EventGraphCallOffset;\n";
#endif
#if UE_VERSION >= UE_5_00 && WITH_LIVE_CODING
    out << "    UFunction** SingletonPtr;\n";
#endif
    out << R"(    uintptr_t  Func;                    // native thunk
};

class UEnum : public UField
{
public:
    FString    CppType;
    /* TArray<TPair<FName,int64>> Names; or FNameData on 5.6+ */
    void*      Names_Data;
    int32_t    Names_Count;
    int32_t    Names_Max;
    int32_t    CppForm;                 // ECppForm: Regular/Namespaced/EnumClass
};

)";

#if UE_VERSION >= UE_4_25
    out << R"(// ----- FField hierarchy (replaces UProperty as UStruct child list in 4.25+) -----
class FFieldClass
{
public:
    FName     Name;
)";
#if UE_VERSION >= UE_5_07
    out << "    uint32_t ClassFlags;\n";
#endif
    out << R"(    uint64_t  Id;
    uint64_t  CastFlags;
)";
#if UE_VERSION < UE_5_07
    out << "    uint32_t ClassFlags;\n";
#endif
    out << R"(    FFieldClass* SuperClass;
    void*     DefaultObject;
    void*     ConstructFn;             // FField* (*)(...)
    int32_t   Unique;
};

class FFieldVariant
{
public:
    uintptr_t Container;               // tagged UObject* or FField* on 5.3+
)";
#if UE_VERSION < UE_5_03
    out << "    bool bIsUObject;\n";
#endif
    out << R"(};

class FField
{
public:
    void*           VTable;            // 0x00
    FFieldClass*    ClassPrivate;      // 0x08
    FFieldVariant   Owner;             // 0x10
    FField*         Next;              // 0x20
    FName           NamePrivate;       // 0x28
    int32_t         FlagsPrivate;      // 0x30
};

class FProperty : public FField
{
public:
    int32_t     ArrayDim;
    int32_t     ElementSize;
    uint64_t    PropertyFlags;
    uint16_t    RepIndex;
    uint8_t     BlueprintReplicationCondition;
    int32_t     Offset_Internal;
    FName       RepNotifyFunc;
    FProperty*  PropertyLinkNext;
    FProperty*  NextRef;
    FProperty*  DestructorLinkNext;
    FProperty*  PostConstructLinkNext;
};

)";
#endif

    // -------------------- helpers + accessors --------------------
    out << R"(// ---------------------------------------------------------------
// Convenience accessors
// ---------------------------------------------------------------
//
// You will need a Read() primitive that reads memory from the target process.
// Replace ReadMem with your own (kernel/usermode/Frida/...).
//

template <class T> T ReadMem(uintptr_t addr); // <- provide your own implementation.

inline uintptr_t GetImageBase();              // <- provide your own implementation.

inline FUObjectArray* GetGObjects()
{
    return reinterpret_cast<FUObjectArray*>(GetImageBase() + OFFSET_GOBJECTS);
}

inline void* GetGNames()
{
    return reinterpret_cast<void*>(GetImageBase() + OFFSET_GNAMES);
}

inline UObject* GetGWorldPtr()
{
)";
    if (gWorldOff)
        out << "    // Only available when a GWorld offset has been resolved.\n"
               "    if (!OFFSET_GWORLD) return nullptr;\n"
               "    return ReadMem<UObject*>(GetImageBase() + OFFSET_GWORLD);\n";
    else
        out << "    return nullptr; // GWorld was not configured for this dump.\n";
    out << R"(}

// Walk GUObjectArray and read the UObject* stored at slot i (handles chunking
// + the UE5.6 offset for FUObjectItem::Object).
inline UObject* GetObjectAt(int32_t index)
{
    auto* arr = GetGObjects();
    if (!arr || index < 0) return nullptr;
    const auto header = ReadMem<FUObjectArray>(reinterpret_cast<uintptr_t>(arr));
    if (index >= header.NumElements || !header.Objects) return nullptr;
    const int32_t numPerChunk = 64 * 1024; // UObjectArray default chunk size
    const int32_t chunkIdx = index / numPerChunk;
    const int32_t itemIdx  = index % numPerChunk;
    if (chunkIdx >= header.NumChunks) return nullptr;
    FUObjectItem* chunk = ReadMem<FUObjectItem*>(
        reinterpret_cast<uintptr_t>(header.Objects) +
        chunkIdx * sizeof(FUObjectItem*));
    if (!chunk) return nullptr;
    auto slotAddr = reinterpret_cast<uintptr_t>(chunk) + itemIdx * sizeof(FUObjectItem);
    return ReadMem<UObject*>(slotAddr + offsetof(FUObjectItem, Object));
}

)";

    // -------------------- FName / FText layouts from the dump --------------------
    out << "// ---------------------------------------------------------------\n";
    out << "// FName / FText layouts as registered by this dump\n";
    out << "// ---------------------------------------------------------------\n\n";

    if (const auto* fname = findStructByCppName("FName"))
    {
        out << "/* FName from this dump (size = 0x" << std::hex << fname->maxSize << std::dec << "): */\n";
        out << "/*\n";
        for (const auto& m : fname->definedMembers) emitMember(out, m);
        out << "*/\n\n";
    }
    if (const auto* ftext = findStructByCppName("FText"))
    {
        out << "/* FText from this dump (size = 0x" << std::hex << ftext->maxSize << std::dec << "): */\n";
        out << "/*\n";
        for (const auto& m : ftext->definedMembers) emitMember(out, m);
        out << "*/\n\n";
    }
    printf("[HEADLESS] internals: core layouts emitted\n");

    // -------------------- curated inheritance lists --------------------
    out << "// ---------------------------------------------------------------\n";
    out << "// Curated inheritance lists (cppName  |  full path)\n";
    out << "// ---------------------------------------------------------------\n";
    out << "//\n";
    out << "// These are walked from the dumped Struct->supers chain (recursive).\n";
    out << "// Same-named bases are excluded.\n\n";

    auto dumpChildrenOf = [&](const char* base)
    {
        out << "// --- inherits from " << base << " ---\n";
        size_t count = 0;
        for (const auto& pkg : EngineCore::getPackages())
        {
            for (const auto& s : pkg.classes)
            {
                if (s.cppName == base) continue;
                if (!structInheritsFromName(&s, base)) continue;
                char b[512];
                sprintf_s(b, "//   %-60s  %s\n", s.cppName.c_str(), s.fullName.c_str());
                out << b;
                ++count;
            }
        }
        if (count == 0) out << "//   (none found in this dump)\n";
        out << "// total: " << count << "\n\n";
    };

    dumpChildrenOf("AActor");
    dumpChildrenOf("ACharacter");
    dumpChildrenOf("APawn");
    dumpChildrenOf("APlayerController");
    dumpChildrenOf("UGameInstance");
    dumpChildrenOf("UBlueprintFunctionLibrary");
    dumpChildrenOf("UGameplayStatics");
    dumpChildrenOf("UKismetSystemLibrary");
    dumpChildrenOf("UDataAsset");
    dumpChildrenOf("AGameModeBase");
    printf("[HEADLESS] internals: inheritance lists emitted\n");

    // -------------------- ULevel / UWorld / UNetDriver / AActor member offsets --------------------
    out << "// ---------------------------------------------------------------\n";
    out << "// Common member offsets (UWorld / ULevel / UNetDriver / AActor / APawn)\n";
    out << "// ---------------------------------------------------------------\n\n";

    auto dumpStructHeader = [&](const char* name)
    {
        const auto* s = findStructByCppName(name);
        out << "// ---- " << name;
        if (s) out << "  (size 0x" << std::hex << s->maxSize << std::dec << ", "
                   << s->definedMembers.size() << " members) ----\n";
        else   out << "  (NOT FOUND in this dump)\n";
        return s;
    };

    out << "/* UWorld highlights */\n";
    {
        const auto* s = dumpStructHeader("UWorld");
        dumpKnownMembers(out, s, {
            "PersistentLevel", "NetDriver", "DemoNetDriver",
            "OwningGameInstance", "GameState", "AuthorityGameMode",
            "Levels", "LevelCollections", "TimeSeconds", "RealTimeSeconds",
            "WorldComposition", "CurrentLevel"
        });
        out << "\n";
    }

    out << "/* ULevel highlights */\n";
    {
        const auto* s = dumpStructHeader("ULevel");
        dumpKnownMembers(out, s, {
            "Actors", "OwningWorld", "Model", "URL", "LevelScriptActor",
            "NetworkActors", "ActorsForGC"
        });
        out << "\n";
    }

    out << "/* UNetDriver highlights */\n";
    {
        const auto* s = dumpStructHeader("UNetDriver");
        dumpKnownMembers(out, s, {
            "ClientConnections", "ServerConnection", "World", "NetDriverName",
            "Time", "ConnectionlessHandler"
        });
        out << "\n";
    }

    out << "/* AActor highlights */\n";
    {
        const auto* s = dumpStructHeader("AActor");
        dumpKnownMembers(out, s, {
            "RootComponent", "Owner", "Instigator", "Role", "RemoteRole",
            "ReplicatedMovement", "Tags", "InstanceComponents",
            "OwnedComponents", "BlueprintCreatedComponents"
        });
        out << "\n";
    }

    out << "/* APawn highlights */\n";
    {
        const auto* s = dumpStructHeader("APawn");
        dumpKnownMembers(out, s, {
            "PlayerState", "Controller", "BaseEyeHeight", "AutoPossessPlayer",
            "AutoPossessAI", "AIControllerClass"
        });
        out << "\n";
    }

    out << "/* ACharacter highlights */\n";
    {
        const auto* s = dumpStructHeader("ACharacter");
        dumpKnownMembers(out, s, {
            "Mesh", "CharacterMovement", "CapsuleComponent", "JumpKeyHoldTime",
            "JumpMaxCount", "BaseRotationOffset", "BaseTranslationOffset"
        });
        out << "\n";
    }

    out << "/* APlayerController highlights */\n";
    {
        const auto* s = dumpStructHeader("APlayerController");
        dumpKnownMembers(out, s, {
            "PlayerCameraManager", "Player", "AcknowledgedPawn",
            "ControllingDirTrackInst", "HiddenActors", "MyHUD",
            "TargetViewRotation", "ControlRotation"
        });
        out << "\n";
    }

    out << "/* UGameInstance highlights */\n";
    {
        const auto* s = dumpStructHeader("UGameInstance");
        dumpKnownMembers(out, s, {
            "LocalPlayers", "OnlineSession", "ReferencedObjects",
            "WorldContext"
        });
        out << "\n";
    }

    // -------------------- footer --------------------
    out << R"(// ---------------------------------------------------------------
// End of EngineInternals.h
// ---------------------------------------------------------------
} // namespace ArcheAgeInternals
)";

    printf("[HEADLESS] internals: member offsets emitted, closing\n");
    out.close();
    if (!out)
        return false;
    windows::LogWindow::Log(windows::LogWindow::logLevels::LOGLEVEL_INFO,
        "SDK GEN", "EngineInternals.h written.");
    return true;
}

SDKGeneration::SDKGeneration()
{
}

bool SDKGeneration::Generate(int& progressDone, int& totalProgress, int featureFlags)
{
    windows::LogWindow::Log(windows::LogWindow::logLevels::LOGLEVEL_INFO, "SDK GEN", "Baking SDK...");
    allnames.clear();
    undefinedCnt = 0;
    bool success = true;

    SDKPath = EngineSettings::getWorkingDirectory() / "SDK";
    std::error_code directoryError;
    std::filesystem::create_directories(SDKPath, directoryError);
    if (directoryError)
    {
        windows::LogWindow::Log(windows::LogWindow::logLevels::LOGLEVEL_ERROR,
            "SDK GEN", "Could not create SDK directory: %s", directoryError.message().c_str());
        return false;
    }

    //the master header contains all the imports sorted
    std::ofstream masterHeader(SDKPath / "SDK.h");
    if (!masterHeader)
    {
        windows::LogWindow::Log(windows::LogWindow::logLevels::LOGLEVEL_ERROR,
            "SDK GEN", "Could not open SDK.h for writing.");
        return false;
    }

    //yes, so all the SDK files are in path/to/stuff/SDK/SDK/
    //because i can imagine theres some stupid game that has a package called SDK and that would ruin our master file
    //so it will look like this
    /// path/to/shit/
    ///				-> SDK/
    ///					  -> SDK.h					// master header you will include
    ///					  -> SDK/					// folder with all the packages
    ///							-> Engine.h			//random package by the game
    ///							-> XGame.h			//random package by the game
    ///							-> CoreUObject.h	//random package by the game
    ///							...
    ///
    SDKPath = SDKPath / "SDK";
    directoryError.clear();
    std::filesystem::create_directories(SDKPath, directoryError);
    if (directoryError)
    {
        windows::LogWindow::Log(windows::LogWindow::logLevels::LOGLEVEL_ERROR,
            "SDK GEN", "Could not create package directory: %s", directoryError.message().c_str());
        return false;
    }



    masterHeader <<
        R"(
/********************************************************
*                                                       *
*     SDK generated using UEDumper by Spuckwaffel.		*
*                                                       *
********************************************************/

/// Include this file into your project.
/// If there are still structs missing, read the Engine/Generation/BasicType file.
/// All manual defined structs are in BasicType.h

#pragma once
#include <cstdint>
#include <string>

)";

    // Signatures and container arguments need declarations, not full layouts.
    // Declare all reflected names up front so mutual references do not create
    // false ordering cycles. Native templates live in BasicType.h instead.
    std::set<std::string> declarations;
    for (const auto& package : EngineCore::getPackages())
    {
        if (package.packageName == "BasicType") continue;
        for (const auto* type : package.combinedStructsAndClasses)
            declarations.insert(std::string(type->isClass ? "class " : "struct ") +
                type->cppName + ";\n");
        for (const auto& type : package.enums)
            declarations.insert("enum class " + type.cppName + " : " + type.type + ";\n");
    }
    for (const auto& declaration : declarations)
        masterHeader << declaration;
    masterHeader << '\n';

    std::vector<MergedPackage> newPackages;
    const auto sortedPackages = sortPackages(progressDone, totalProgress, newPackages);
    std::ostringstream unrealHeader;
    unrealHeader <<
        "#pragma once\n"
        "#define _IDA_ 1\n"
        "typedef unsigned __int8 uint8_t;\n"
        "typedef unsigned __int16 uint16_t;\n"
        "typedef unsigned __int32 uint32_t;\n"
        "typedef unsigned __int64 uint64_t;\n"
        "typedef __int8 int8_t;\n"
        "typedef __int16 int16_t;\n"
        "typedef __int32 int32_t;\n"
        "typedef __int64 int64_t;\n"
        "typedef uint64_t uintptr_t;\n"
        "typedef uint64_t size_t;\n"
        "struct FArcheAgeBundle_20261023_1 { uint8_t Present; };\n"
        "class FFrame_IDA_UE53;\n"
        "class FField;\n"
        "class FProperty;\n\n";
    for (const auto& declaration : declarations)
        unrealHeader << declaration;


    std::unordered_map<std::string, std::string> originalPackageToMerged;
    for (auto& pack : newPackages)
    {
        for (auto& mergedPack : pack.mergedPackages) {
            originalPackageToMerged.insert(std::pair<std::string, std::string>(mergedPack->packageName, pack.package.packageName));
        }
    }

    std::vector<EngineStructs::Struct*> blueprintActors;
    for (auto& package : EngineCore::getPackages())
    {
        for (auto& type : package.classes)
        {
            if (isBlueprintActor(&type))
                blueprintActors.push_back(&type);
        }
    }
    std::ranges::sort(blueprintActors, [](const auto* left, const auto* right)
    {
        if (left->superNames.size() != right->superNames.size())
            return left->superNames.size() < right->superNames.size();
        return left->fullName < right->fullName;
    });

    std::ostringstream blueprintActorIdaText;
    std::vector<std::string> blueprintActorFiles;
    std::unordered_set<std::string> generatedBlueprintActors;
    for (auto* actor : blueprintActors)
    {
        if (!generatedBlueprintActors.insert(actor->cppName).second)
            continue;

        EngineStructs::Package actorPackage;
        actorPackage.packageName = actor->cppName;
        actorPackage.combinedStructsAndClasses.push_back(actor);

        std::ostringstream actorStream;
        printCredits(actorStream);
        generatePackage(actorStream, actorPackage, featureFlags, originalPackageToMerged);
        auto actorText = actorStream.str();

        std::string owningHeader = actor->owningPackage->packageName;

        std::string includes = "#include \"" + owningHeader + ".h\"\n";
        if (!actor->supers.empty() && isBlueprintActor(actor->supers.front()))
            includes += "#include \"" + blueprintActorFileName(actor->supers.front()) + "\"\n";

        const auto pragmaEnd = actorText.find('\n', actorText.find("#pragma once"));
        actorText.insert(pragmaEnd == std::string::npos ? 0 : pragmaEnd + 1, includes);

        const auto fileName = blueprintActorFileName(actor);
        std::ofstream actorFile(SDKPath / fileName);
        actorFile << actorText;
        actorFile.close();
        success = success && static_cast<bool>(actorFile);
        blueprintActorFiles.push_back(fileName);

        for (auto position = actorText.find("#include \""); position != std::string::npos;)
        {
            const auto end = actorText.find('\n', position);
            actorText.replace(position, end == std::string::npos ? actorText.size() - position : end - position,
                "// Dependency emitted in this combined header");
            position = actorText.find("#include \"", position);
        }
        blueprintActorIdaText << actorText << '\n';
    }

    puts("------sorted packages------");
    for (auto& pack : sortedPackages)
    {
        printf("%s\n", pack->package.packageName.c_str());
    }
    puts("---------------------------");

    //generateBasicType();

    totalProgress = sortedPackages.size();
    progressDone = 0;
    int tooLongNames = 0;
    for (auto& pack : sortedPackages)
    {
        std::string packageName = pack->package.packageName;
        if(packageName.length() > 100)
        {
            packageName = "tooLongPackage_" + std::to_string(tooLongNames++);
            masterHeader << "// Package name has been changed because the name was too long. Original name:" << std::endl;
            masterHeader << "// " << pack->package.packageName << std::endl;
        }

        windows::LogWindow::Log(windows::LogWindow::logLevels::LOGLEVEL_INFO, "SDK GEN", "Baking package %s", packageName.c_str());

    	masterHeader << "#include \"SDK/" + packageName + ".h\"" << std::endl;
        if (pack->package.packageName == "BasicType")
        {
            std::ostringstream packageText;
            writeBasicType(packageText);
            const auto text = packageText.str();
            std::ofstream package(SDKPath / "BasicType.h");
            package << text;
            package.close();
            success = success && static_cast<bool>(package);
            unrealHeader << text << '\n';
        }
        else
        {
            std::string _packageName = packageName + ".h";
            std::ostringstream packageText;
            printCredits(packageText);
            generatePackage(packageText, pack->package, featureFlags, originalPackageToMerged);
            const auto text = packageText.str();
            std::ofstream package(SDKPath / _packageName);
            package << text;
            package.close();
            success = success && static_cast<bool>(package);

            auto idaText = text;
            for (auto position = idaText.find("#include \""); position != std::string::npos;)
            {
                const auto end = idaText.find('\n', position);
                idaText.replace(position, end == std::string::npos ? idaText.size() - position : end - position,
                    "// Dependency emitted in this combined header");
                position = idaText.find("#include \"", position);
            }
            unrealHeader << idaText << '\n';
        }

        progressDone++;
    }

    unrealHeader << blueprintActorIdaText.str();
    for (const auto& fileName : blueprintActorFiles)
        masterHeader << "#include \"SDK/" << fileName << "\"" << std::endl;
    windows::LogWindow::Log(windows::LogWindow::logLevels::LOGLEVEL_INFO,
        "SDK GEN", "Wrote %zu Blueprint actor class headers.", blueprintActorFiles.size());
    printf("[HEADLESS] packages baked, generating EngineInternals...\n");

    // Emit the engine-internals header (one big copy-paste file) and reference it
    // from the master SDK.h so users can grab everything from a single include.
    success = generateEngineInternals() && success;
    masterHeader << "#include \"SDK/EngineInternals.h\"" << std::endl;

	if (undefinedCnt != 0)
	{
		// Unresolved members are emitted as sized byte arrays (see generateStruct),
		// so the SDK still compiles with correct offsets. Report, don't fail.
		masterHeader << "\n// NOTE: " << undefinedCnt
			<< " member(s) use an unresolved reflected type and are emitted as opaque byte arrays (grep UNRESOLVED).\n";
		printf("[HEADLESS] %d unresolved member type(s) emitted as opaque byte arrays\n", undefinedCnt);
	}

    masterHeader.close();
    success = success && static_cast<bool>(masterHeader);
    printf("[HEADLESS] EngineInternals done, writing IDA bundle...\n");

    unrealHeader <<
        "\n// IDA-only actionable global layouts.\n"
        "struct FOutParmRec_IDA;\n"
        "struct FFrame_IDA_UE53 {\n"
        "    void* VTable;                       // 0x00 FOutputDevice vtable\n"
        "    uint64_t OutputDeviceState;          // 0x08 FOutputDevice state\n"
        "    UFunction* Node;                    // 0x10 executing function\n"
        "    UObject* Object;                    // 0x18 executing object\n"
        "    uint8_t* Code;                      // 0x20 bytecode cursor\n"
        "    uint8_t* Locals;                    // 0x28 parameter/local storage\n"
        "    FProperty* MostRecentProperty;      // 0x30\n"
        "    uint8_t* MostRecentPropertyAddress;// 0x38\n"
        "    uint8_t* MostRecentPropertyContainer;// 0x40\n"
        "    uint8_t FlowStack[0x30];            // 0x48 version-dependent flow stack\n"
        "    FFrame_IDA_UE53* PreviousFrame;     // 0x78\n"
        "    FOutParmRec_IDA* OutParms;          // 0x80\n"
        "    FField* PropertyChainForCompiledIn; // 0x88 (live-validated)\n"
        "    UFunction* CurrentNativeFunction;   // 0x90\n"
        "    uint8_t TailUnknown[0x08];           // 0x98\n"
        "};\n"
        "struct FOutParmRec_IDA {\n"
        "    FProperty* Property;                // 0x00\n"
        "    uint8_t* PropertyValue;             // 0x08\n"
        "    FOutParmRec_IDA* NextOutParm;       // 0x10\n"
        "};\n"
        "struct FNamePool_IDA_UE53 {\n"
        "    uint8_t Lock[0x08];                 // 0x00 FRWLock storage\n"
        "    uint32_t CurrentBlock;              // 0x08\n"
        "    uint32_t CurrentByteCursor;         // 0x0C\n"
        "    uint8_t* Blocks[8192];              // 0x10 name-entry blocks\n"
        "};\n"
        "struct FUObjectItem_IDA { uint64_t Unknown; uintptr_t Object; uint8_t UnknownTail[8]; };\n"
        "struct FUObjectArray_IDA {\n"
        "    FUObjectItem_IDA** Objects; FUObjectItem_IDA* PreAllocatedObjects;\n"
        "    int32_t MaxElements; int32_t NumElements;\n"
        "    int32_t MaxChunks; int32_t NumChunks;\n"
        "};\n";

    std::string exportError;
    if (!IDAExporter::WriteBundle(EngineSettings::getWorkingDirectory(), unrealHeader.str(), exportError))
    {
        windows::LogWindow::Log(windows::LogWindow::logLevels::LOGLEVEL_ERROR,
            "SDK GEN", "IDA bundle failed: %s", exportError.c_str());
        success = false;
    }
    else
    {
        windows::LogWindow::Log(windows::LogWindow::logLevels::LOGLEVEL_INFO,
            "SDK GEN", "IDA bundle written to %s",
            (EngineSettings::getWorkingDirectory() / "IDA").string().c_str());
    }

    progressDone = totalProgress;
    return success;
}
