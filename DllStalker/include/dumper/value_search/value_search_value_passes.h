#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include "types/dumper_types.h"
#include "types/value_search_types.h"
#include "unity_dumper.h"

namespace Engine::Dumper
{
bool ValuePasses(UnityDumper& dumper, const FieldInfo& field, const ValueSearchParams& params);
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
