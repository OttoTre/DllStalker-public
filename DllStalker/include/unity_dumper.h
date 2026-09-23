#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include "unity_resolver.h"

#include <vector>

#include "types/dumper_types.h"

#include "dumper/catalog/class_catalog.h"
#include "dumper/instances/collection_view.h"
#include "dumper/export/sdk_exporter.h"
#include "dumper/catalog/field_catalog.h"
#include "dumper/instances/live_object_finder.h"
#include "dumper/catalog/method_catalog.h"
#include "dumper/invoke/method_invoker.h"
#include "dumper/catalog/object_identity.h"
#include "dumper/instances/static_instance_finder.h"
#include "dumper/value_search/value_search_schema_cache.h"

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
    std::vector<MethodNameRow> EnumerateMethodNames(void* klass, size_t maxPerClass,
                                                    bool* truncated = nullptr) {
        return m_methods.EnumerateMethodNames(klass, maxPerClass, truncated);
    }
    std::vector<FieldInfo>  GetRawFields(void* klass)                 { return m_fields.GetRawFields(klass); }
    std::vector<FieldInfo>  GetRawFields(void* klass, void* instance,
                                         bool* enumerationComplete = nullptr,
                                         bool metadataOnly = false) {
        return m_fields.GetRawFields(klass, instance, enumerationComplete, metadataOnly);
    }
    bool SetFieldValue(const FieldInfo& field, const std::string& newValue, std::string* error = nullptr) {
        return m_fields.SetFieldValue(field, newValue, error);
    }

    // Live Array/List bounds check for a synthesized element write target.
    bool IsCollectionElementAddressLive(const FieldInfo& collectionField,
                                        uintptr_t elementAddress,
                                        std::string* error = nullptr) const {
        return m_collection.IsAddressInLiveElementBuffer(collectionField, elementAddress, error);
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

    // maxElements: 0 = uncapped (Inspector). Scan expand uses
    // kValueSearchMaxCollectionElements; resolve/nav uses index+1.
    // outEmptyButReadable: readable header + logical length 0 (no dummy rows).
    std::vector<FieldInfo> GetCollectionView(const FieldInfo& field,
                                             size_t maxElements = 0,
                                             bool* outEmptyButReadable = nullptr) const {
        return m_collection.GetCollectionView(field, maxElements, outEmptyButReadable);
    }
    bool TryWriteListLogicalSize(const FieldInfo& collectionField,
                                 std::string* error = nullptr) const {
        return m_collection.TryWriteListLogicalSize(collectionField, error);
    }

    // Deep Search / schema: ARRAY/LIST element klass (see CollectionView).
    void* TryResolveCollectionElementKlass(const FieldInfo& field) const {
        return m_collection.TryResolveElementKlass(field);
    }
    void* TryResolveCollectionElementKlass(void* ownerKlass, const char* fieldName) const {
        return m_collection.TryResolveElementKlass(ownerKlass, fieldName);
    }
    // Follow PTR schema: owner field → Il2Cpp type → klass (best-effort).
    void* TryResolveFieldTypeKlass(void* ownerKlass, const char* fieldName) const {
        return m_fields.TryResolveFieldTypeKlass(ownerKlass, fieldName);
    }
    void* KlassFromInstance(void* instance) const {
        return m_identity.KlassFromInstance(instance);
    }
    bool IsValueTypeKlass(void* klass) const {
        return klass && m_resolver.module.exports.fnClassIsValueType
            && m_resolver.module.exports.fnClassIsValueType(klass) != 0;
    }

    Dumper::ValueSearchSchemaCache& ValueSearchSchemas() { return m_valueSearchSchemas; }
    void ClearValueSearchSchemas() { m_valueSearchSchemas.Clear(); }

private:
    // Resolves dumper-only Unity exports (class/field/method enumeration).
    // Owned here — not on LiveObjectFinder.
    bool InitDumperExports();

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
    Dumper::ValueSearchSchemaCache m_valueSearchSchemas;
};

} // namespace Engine

#endif // ENABLE_DUMPER
