#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include "unity_resolver.h"

#include <vector>

#include "types/dumper_types.h"

#include "dumper/class_catalog.h"
#include "dumper/collection_view.h"
#include "dumper/sdk_exporter.h"
#include "dumper/field_catalog.h"
#include "dumper/live_object_finder.h"
#include "dumper/method_catalog.h"
#include "dumper/method_invoker.h"
#include "dumper/object_identity.h"
#include "dumper/static_instance_finder.h"

namespace Engine
{
// Thin facade over the dumper modules.
class UnityDumper
{
public:
    explicit UnityDumper(UnityResolver& resolver);

    // --- GUI / metadata API ---
    std::vector<ClassInfo>  GetRawClasses(void* image)               { return m_classes.GetRawClasses(image); }
    std::vector<ImageInfo>  GetLoadedImages()                         { return m_classes.GetLoadedImages(); }
    std::vector<MethodInfo> GetRawMethods(void* klass)                { return m_methods.GetRawMethods(klass); }
    std::vector<FieldInfo>  GetRawFields(void* klass)                 { return m_fields.GetRawFields(klass); }
    std::vector<FieldInfo>  GetRawFields(void* klass, void* instance) { return m_fields.GetRawFields(klass, instance); }
    bool SetFieldValue(const FieldInfo& field, const std::string& newValue, std::string* error = nullptr) {
        return m_fields.SetFieldValue(field, newValue, error);
    }

    std::vector<EnumLiteral> GetEnumLiterals(void* enumKlass) {
        return m_fields.GetEnumLiterals(enumKlass);
    }

    std::vector<void*> FindStaticInstanceCandidates(void* klass)  { return m_staticFinder.FindStaticInstanceCandidates(klass); }
    std::vector<void*> GetLiveInstances(void* klass)               { return m_liveFinder.GetLiveInstances(klass); }

    Dumper::SdkExportResult ExportSdk(const std::vector<ClassInfo>& classes,
                                      const Dumper::SdkExportOptions& options) {
        return m_sdkExporter.Export(classes, options);
    }

    InvokeResult InvokeMethod(const MethodInfo& method,
                              void* instance,
                              const std::vector<std::string>& argInputs) const {
        return m_invoker.InvokeMethod(method, instance, argInputs);
    }

    std::string TryGetClassNameFromInstance(void* instance, void** outKlass = nullptr) const {
        return m_identity.TryGetClassNameFromInstance(instance, outKlass);
    }

    bool IsOrInheritsFrom(void* klass, const char* targetName) const {
        return m_identity.IsOrInheritsFrom(klass, targetName);
    }

    std::vector<FieldInfo> GetCollectionView(const FieldInfo& field) const {
        return m_collection.GetCollectionView(field);
    }

private:
    UnityResolver& m_resolver;

    Dumper::ObjectIdentity        m_identity;
    Dumper::ClassCatalog          m_classes;
    Dumper::MethodCatalog         m_methods;
    Dumper::FieldCatalog          m_fields;
    Dumper::CollectionView        m_collection;
    Dumper::StaticInstanceFinder  m_staticFinder;
    Dumper::LiveObjectFinder      m_liveFinder;
    Dumper::MethodInvoker         m_invoker;
    Dumper::SdkExporter           m_sdkExporter;
};

} // namespace Engine

#endif // ENABLE_DUMPER
