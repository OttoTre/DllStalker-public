#include "pch.h"

#ifdef ENABLE_DUMPER

#include "dumper/sdk_exporter.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace Engine::Dumper
{
namespace
{
struct TypeMapping { std::string_view unity; std::string_view cpp; };

constexpr TypeMapping kTypeMappings[] = {
	{ "System.Boolean",        "bool"     },
	{ "System.Byte",           "uint8_t"  },
	{ "System.SByte",          "int8_t"   },
	{ "System.Int16",          "int16_t"  },
	{ "System.UInt16",         "uint16_t" },
	{ "System.Int32",          "int32_t"  },
	{ "System.UInt32",         "uint32_t" },
	{ "System.Int64",          "int64_t"  },
	{ "System.UInt64",         "uint64_t" },
	{ "System.Single",         "float"    },
	{ "System.Double",         "double"   },
	{ "System.Char",           "uint16_t" },
	{ "System.IntPtr",         "void*"    },
	{ "System.UIntPtr",        "void*"    },
	{ "System.String",         "void*"    },
	{ "System.Object",         "void*"    },
	{ "UnityEngine.Vector2",   "float[2]" },
	{ "UnityEngine.Vector3",   "float[3]" },
	{ "UnityEngine.Vector4",   "float[4]" },
	{ "UnityEngine.Quaternion","float[4]" },
	{ "UnityEngine.Color",     "float[4]" },
	{ "UnityEngine.Color32",   "uint8_t[4]"},
};

std::string ToCppType(const std::string& unityType)
{
	for (const auto& m : kTypeMappings) {
		if (m.unity == unityType) {
			return std::string(m.cpp);
		}
	}
	return "void* /* " + unityType + " */";
}

std::string ToCsharpEnumUnderlying(const std::string& unityType)
{
	if (unityType == "System.Byte")   return "byte";
	if (unityType == "System.SByte")  return "sbyte";
	if (unityType == "System.Int16")  return "short";
	if (unityType == "System.UInt16") return "ushort";
	if (unityType == "System.UInt32") return "uint";
	if (unityType == "System.Int64")  return "long";
	if (unityType == "System.UInt64") return "ulong";
	return "int";
}

size_t CppTypeSize(const std::string& unityType)
{
	if (unityType == "System.Boolean" || unityType == "System.Byte"  || unityType == "System.SByte")  return 1;
	if (unityType == "System.Int16"   || unityType == "System.UInt16"|| unityType == "System.Char")   return 2;
	if (unityType == "System.Int32"   || unityType == "System.UInt32"|| unityType == "System.Single") return 4;
	if (unityType == "System.Int64"   || unityType == "System.UInt64"|| unityType == "System.Double") return 8;
	if (unityType == "UnityEngine.Vector2")                                                            return 8;
	if (unityType == "UnityEngine.Vector3")                                                            return 12;
	if (unityType == "UnityEngine.Vector4" || unityType == "UnityEngine.Quaternion"
										   || unityType == "UnityEngine.Color")                        return 16;
	if (unityType == "UnityEngine.Color32")                                                            return 4;
	return 8;
}

std::string Sanitize(const std::string& s)
{
	std::string out;
	out.reserve(s.size());
	for (char c : s) {
		out += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
	}
	if (!out.empty() && std::isdigit(static_cast<unsigned char>(out[0])))
		out = "_" + out;
	return out;
}

std::string EnumTypeBaseName(const std::string& fullType)
{
	const size_t dot = fullType.rfind('.');
	if (dot != std::string::npos && dot + 1 < fullType.size()) {
		return fullType.substr(dot + 1);
	}
	return fullType;
}

std::string CurrentTimestamp()
{
	std::time_t t = std::time(nullptr);
	char buf[64];
	struct tm tm_info{};
	localtime_s(&tm_info, &t);
	std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_info);
	return buf;
}

std::vector<FieldInfo> FilterInstanceFields(const std::vector<FieldInfo>& fields)
{
	std::vector<FieldInfo> relevant;
	for (const auto& f : fields) {
		if (f.isStatic) {
			continue;
		}
		relevant.push_back(f);
	}
	return relevant;
}

std::string UniqueStructName(const ClassInfo& klass, std::unordered_set<std::string>& used)
{
	std::string base = Sanitize(klass.name);
	if (used.count(base) != 0) {
		if (!klass.ns.empty()) {
			base = Sanitize(klass.ns) + "_" + base;
		}
	}
	int suffix = 2;
	std::string candidate = base;
	while (used.count(candidate) != 0) {
		candidate = base + "_" + std::to_string(suffix++);
	}
	used.insert(candidate);
	return candidate;
}

struct EnumTypeInfo {
	std::string typeName;
	std::string underlyingType = "System.Int32";
};

void CollectEnumTypes(const std::vector<ClassInfo>& classes,
					  FieldCatalog& fields,
					  std::unordered_map<void*, EnumTypeInfo>& out)
{
	for (const auto& klass : classes) {
		if (!klass.klassPtr) {
			continue;
		}
		for (const auto& f : FilterInstanceFields(fields.GetRawFields(klass.klassPtr))) {
			if (!f.isEnum || !f.enumKlass) {
				continue;
			}
			auto& info    = out[f.enumKlass];
			info.typeName = f.type;
			if (!f.underlyingType.empty()) {
				info.underlyingType = f.underlyingType;
			}
		}
	}
}

template<typename LogFn>
void EmitEnumsCpp(const LogFn& Log, FieldCatalog& fields, const std::unordered_map<void*, EnumTypeInfo>& enumTypes)
{
	for (const auto& entry : enumTypes) {
		void* enumKlass = entry.first;
		const EnumTypeInfo& info = entry.second;
		const auto        literals = fields.GetEnumLiterals(enumKlass);
		if (literals.empty()) {
			continue;
		}

		const std::string enumName = Sanitize(EnumTypeBaseName(info.typeName));
		const std::string cppBase  = ToCppType(info.underlyingType);

		Log("// %s\n", info.typeName.c_str());
		Log("enum class %s : %s {\n", enumName.c_str(), cppBase.c_str());
		for (size_t i = 0; i < literals.size(); ++i) {
			const auto& lit     = literals[i];
			const std::string safeLit = Sanitize(lit.name);
			Log("    %s = %lld%s\n",
				safeLit.c_str(),
				static_cast<long long>(lit.value),
				(i + 1 < literals.size()) ? "," : "");
		}
		Log("};\n\n");
	}
}

template<typename LogFn>
void EmitEnumsCsharp(const LogFn& Log,
					 FieldCatalog& fields,
					 const std::unordered_map<void*, EnumTypeInfo>& enumTypes)
{
	for (const auto& entry : enumTypes) {
		void* enumKlass = entry.first;
		const EnumTypeInfo& info = entry.second;
		const auto        literals = fields.GetEnumLiterals(enumKlass);
		if (literals.empty()) {
			continue;
		}

		const std::string enumName = Sanitize(EnumTypeBaseName(info.typeName));
		Log("    // %s\n", info.typeName.c_str());
		Log("    public enum %s : %s\n    {\n",
			enumName.c_str(),
			ToCsharpEnumUnderlying(info.underlyingType).c_str());
		for (size_t i = 0; i < literals.size(); ++i) {
			const auto& lit     = literals[i];
			const std::string safeLit = Sanitize(lit.name);
			Log("        %s = %lld%s\n",
				safeLit.c_str(),
				static_cast<long long>(lit.value),
				(i + 1 < literals.size()) ? "," : "");
		}
		Log("    }\n\n");
	}
}
} // anonymous namespace

// ---------------------------------------------------------------------------

SdkExporter::SdkExporter(UnityResolver& resolver, FieldCatalog& fields, MethodCatalog& methods)
	: m_resolver(resolver)
	, m_fields(fields)
	, m_methods(methods)
{
	(void)m_resolver;
}

SdkExportResult SdkExporter::Export(const std::vector<ClassInfo>& classes, const SdkExportOptions& options)
{
	if (classes.empty()) {
		printf("[SdkExporter] No classes to export.\n");
		return {};
	}

	switch (options.format) {
	case SdkExportFormat::CppHeader:
		return EmitCppHeader(options, classes);
	case SdkExportFormat::CsharpStubs:
		return EmitCsharpStubs(options, classes);
	}
	return {};
}

SdkExportResult SdkExporter::EmitCppHeader(const SdkExportOptions&        options,
										   const std::vector<ClassInfo>& classes)
{
	std::ofstream outFile;
	const bool    toFile = (options.outputPath != nullptr);
	if (toFile) {
		outFile.open(options.outputPath, std::ios::out | std::ios::trunc);
		if (!outFile.is_open()) {
			printf("[SdkExporter] Failed to open '%s' for writing.\n", options.outputPath);
			return {};
		}
	}

	auto Log = [&](const char* fmt, ...) {
		char buf[4096];
		va_list args;
		va_start(args, fmt);
		vsnprintf(buf, sizeof(buf), fmt, args);
		va_end(args);
		if (toFile) {
			outFile << buf;
		}
		else {
			printf("%s", buf);
		}
	};

	Log("// Auto-generated by DllStalker SDK Exporter\n");
	Log("// Timestamp: %s\n", CurrentTimestamp().c_str());
	Log("// WARNING: Offsets are version-specific. Verify before use.\n\n");
	Log("#pragma once\n");
	Log("#include <cstdint>\n\n");
	Log("namespace SDK {\n\n");

	if (options.includeEnums) {
		std::unordered_map<void*, EnumTypeInfo> enumTypes;
		CollectEnumTypes(classes, m_fields, enumTypes);
		EmitEnumsCpp(Log, m_fields, enumTypes);
	}

	std::unordered_set<std::string> usedStructNames;
	int                             emitted     = 0;
	int                             totalFields = 0;

	for (const auto& klass : classes) {
		if (!klass.klassPtr) {
			continue;
		}

		auto relevant = FilterInstanceFields(m_fields.GetRawFields(klass.klassPtr));
		if (relevant.empty() && !options.includeMethods) {
			continue;
		}

		std::sort(relevant.begin(), relevant.end(),
				  [](const FieldInfo& a, const FieldInfo& b) { return a.offset < b.offset; });

		const std::string safeName = UniqueStructName(klass, usedStructNames);

		if (!klass.ns.empty()) {
			Log("// [%s] %s::%s\n", klass.ns.c_str(), klass.ns.c_str(), klass.name.c_str());
		}
		else {
			Log("// %s\n", klass.name.c_str());
		}

		Log("struct %s {\n", safeName.c_str());

		size_t cursor = 0;
		for (const auto& f : relevant) {
			if (f.offset > cursor) {
				Log("    char __pad_%04zX[0x%zX]; // padding\n", cursor, f.offset - cursor);
			}
			else if (f.offset < cursor) {
				Log("    // overlapping field '%s' at 0x%zX (skipped)\n", f.name.c_str(), f.offset);
				continue;
			}

			const std::string cppType   = ToCppType(f.isEnum ? f.underlyingType : f.type);
			const std::string safeFName = Sanitize(f.name);

			Log("    %-20s %s; // 0x%04zX  %s\n",
				cppType.c_str(), safeFName.c_str(), f.offset, f.type.c_str());

			cursor = f.offset + CppTypeSize(f.isEnum ? f.underlyingType : f.type);
			++totalFields;
		}

		if (options.includeMethods) {
			const auto methods = m_methods.GetRawMethods(klass.klassPtr);
			if (!methods.empty()) {
				Log("\n    // --- Methods ---\n");
				for (const auto& m : methods) {
					Log("    // %s  [0x%p]  %s\n",
						m.name.c_str(),
						reinterpret_cast<void*>(m.address),
						m.parameters.c_str());
				}
			}
		}

		Log("}; // struct %s\n\n", safeName.c_str());
		++emitted;
	}

	Log("} // namespace SDK\n");

	if (toFile) {
		outFile.close();
		printf("[SdkExporter] C++ header written to '%s' (%d classes, %d fields).\n",
			   options.outputPath,
			   emitted,
			   totalFields);
	}
	else {
		printf("[SdkExporter] C++ header done (%d classes, %d fields).\n", emitted, totalFields);
	}

	SdkExportResult result{};
	result.success    = true;
	result.classCount = emitted;
	result.fieldCount = totalFields;
	return result;
}

SdkExportResult SdkExporter::EmitCsharpStubs(const SdkExportOptions&        options,
											 const std::vector<ClassInfo>& classes)
{
	std::ofstream outFile;
	const bool    toFile = (options.outputPath != nullptr);
	if (toFile) {
		outFile.open(options.outputPath, std::ios::out | std::ios::trunc);
		if (!outFile.is_open()) {
			printf("[SdkExporter] Failed to open '%s' for writing.\n", options.outputPath);
			return {};
		}
	}

	auto Log = [&](const char* fmt, ...) {
		char buf[4096];
		va_list args;
		va_start(args, fmt);
		vsnprintf(buf, sizeof(buf), fmt, args);
		va_end(args);
		if (toFile) {
			outFile << buf;
		}
		else {
			printf("%s", buf);
		}
	};

	Log("// Auto-generated by DllStalker SDK Exporter\n");
	Log("// Timestamp: %s\n", CurrentTimestamp().c_str());
	Log("// WARNING: Offsets are version-specific. Verify before use.\n\n");
	Log("namespace SDK\n{\n\n");

	if (options.includeEnums) {
		std::unordered_map<void*, EnumTypeInfo> enumTypes;
		CollectEnumTypes(classes, m_fields, enumTypes);
		EmitEnumsCsharp(Log, m_fields, enumTypes);
	}

	std::unordered_set<std::string> usedStructNames;
	int                             emitted     = 0;
	int                             totalFields = 0;

	for (const auto& klass : classes) {
		if (!klass.klassPtr) {
			continue;
		}

		auto relevant = FilterInstanceFields(m_fields.GetRawFields(klass.klassPtr));
		if (relevant.empty() && !options.includeMethods) {
			continue;
		}

		std::sort(relevant.begin(), relevant.end(),
				  [](const FieldInfo& a, const FieldInfo& b) { return a.offset < b.offset; });

		const std::string safeName = UniqueStructName(klass, usedStructNames);

		if (!klass.ns.empty()) {
			Log("    // %s::%s\n", klass.ns.c_str(), klass.name.c_str());
		}
		else {
			Log("    // %s\n", klass.name.c_str());
		}

		Log("    public static class %s_Offsets\n    {\n", safeName.c_str());

		for (const auto& f : relevant) {
			const std::string safeFName = Sanitize(f.name);
			Log("        public const int %-40s = 0x%04zX; // %s\n",
				safeFName.c_str(), f.offset, f.type.c_str());
			++totalFields;
		}

		if (options.includeMethods) {
			const auto methods = m_methods.GetRawMethods(klass.klassPtr);
			if (!methods.empty()) {
				Log("\n        // --- Methods ---\n");
				for (const auto& m : methods) {
					Log("        // %s  [0x%p]  %s\n",
						m.name.c_str(),
						reinterpret_cast<void*>(m.address),
						m.parameters.c_str());
				}
			}
		}

		Log("    } // class %s_Offsets\n\n", safeName.c_str());
		++emitted;
	}

	Log("} // namespace SDK\n");

	if (toFile) {
		outFile.close();
		printf("[SdkExporter] C# stubs written to '%s' (%d classes, %d fields).\n",
			   options.outputPath,
			   emitted,
			   totalFields);
	}
	else {
		printf("[SdkExporter] C# stubs done (%d classes, %d fields).\n", emitted, totalFields);
	}

	SdkExportResult result{};
	result.success    = true;
	result.classCount = emitted;
	result.fieldCount = totalFields;
	return result;
}

} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
