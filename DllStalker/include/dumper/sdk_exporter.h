#pragma once

#ifdef ENABLE_DUMPER

#include <vector>

#include "unity_resolver.h"
#include "types/dumper_types.h"
#include "dumper/field_catalog.h"
#include "dumper/method_catalog.h"

namespace Engine::Dumper
{
enum class SdkExportFormat
{
	CppHeader,    // Padded C++ structs with offset comments
	CsharpStubs,  // Static C# classes with const int offset fields
};

struct SdkExportOptions
{
	SdkExportFormat format        = SdkExportFormat::CppHeader;
	const char*     outputPath    = nullptr; // nullptr → printf to console
	bool            includeMethods  = false;
	bool            includeEnums    = false;
};

struct SdkExportResult
{
	bool success     = false;
	int  classCount  = 0;
	int  fieldCount  = 0;
};

// Emits a C++ padded-struct header or C# offset-constants file from a class list.
// No GUI dependency — dumper layer only.
class SdkExporter
{
public:
	SdkExporter(UnityResolver& resolver,
				FieldCatalog&  fields,
				MethodCatalog& methods);

	SdkExportResult Export(const std::vector<ClassInfo>& classes, const SdkExportOptions& options);

private:
	SdkExportResult EmitCppHeader(const SdkExportOptions&        options,
								  const std::vector<ClassInfo>& classes);
	SdkExportResult EmitCsharpStubs(const SdkExportOptions&        options,
									const std::vector<ClassInfo>& classes);

	UnityResolver& m_resolver;
	FieldCatalog&  m_fields;
	MethodCatalog& m_methods;
};
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
