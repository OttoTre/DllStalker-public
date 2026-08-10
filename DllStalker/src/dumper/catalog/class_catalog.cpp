#include "pch.h"

#ifdef ENABLE_DUMPER

#include "dumper/catalog/class_catalog.h"

#include <algorithm>

#include "services/bootstrap_log.h"

namespace Engine::Dumper
{
ClassCatalog::ClassCatalog(UnityResolver& resolver)
    : m_resolver(resolver)
{
}

std::vector<ImageInfo> ClassCatalog::GetLoadedImages() {
    m_resolver.module.EnsureThreadAttached();

    std::vector<ImageInfo> images = EnumerateLoadedImages();

    for (auto& img : images) {
        int classCount = 0;
        if (img.imagePtr) {
            if (m_resolver.module.isIL2CPP) {
                if (m_resolver.module.exports.fnImageGetClassCount)
                    classCount = m_resolver.module.exports.fnImageGetClassCount(img.imagePtr);
            }
            else if (m_resolver.module.exports.fnImageGetTableInfo
                  && m_resolver.module.exports.fnTableInfoGetRows) {
                void* tableInfo = m_resolver.module.exports.fnImageGetTableInfo(img.imagePtr, 2);
                if (tableInfo) classCount = m_resolver.module.exports.fnTableInfoGetRows(tableInfo);
            }
        }
        img.classCount = (std::max)(0, classCount);
    }

    return images;
}

std::vector<ImageInfo> ClassCatalog::EnumerateLoadedImages() const {
    std::vector<ImageInfo> images;

    m_resolver.images.ForEachImage([&](void* image, const char* name) {
        images.push_back({ name ? name : "UNKNOWN_IMAGE", image, 0 });
        return true;
    });

    if (!m_resolver.module.isIL2CPP) {
        // mono_assembly_foreach can yield duplicates if the same assembly is
        // referenced multiple ways; IL2CPP's domain_get_assemblies is already
        // unique so we skip the sort/dedup overhead there.
        std::sort(images.begin(), images.end(), [](const ImageInfo& a, const ImageInfo& b) {
            if (a.name == b.name) return a.imagePtr < b.imagePtr;
            return a.name < b.name;
        });
        images.erase(std::unique(images.begin(), images.end(), [](const ImageInfo& a, const ImageInfo& b) {
            return a.imagePtr == b.imagePtr && a.name == b.name;
        }), images.end());
    }

    return images;
}

std::vector<ClassInfo> ClassCatalog::GetRawClasses(void* image) {
    if (!image) return {};
    m_resolver.module.EnsureThreadAttached();

    if (!m_resolver.module.exports.fnClassFromIndex || !m_resolver.module.exports.fnClassGetName) {
        Engine::Services::BootstrapLog::Write(
            "[-] Required class enumeration functions not available.\n");
        return {};
    }

    int classCount = 0;
    if (m_resolver.module.isIL2CPP) {
        if (m_resolver.module.exports.fnImageGetClassCount)
            classCount = m_resolver.module.exports.fnImageGetClassCount(image);
    }
    else {
        if (m_resolver.module.exports.fnImageGetTableInfo && m_resolver.module.exports.fnTableInfoGetRows) {
            void* tableInfo = m_resolver.module.exports.fnImageGetTableInfo(image, 2);
            classCount = m_resolver.module.exports.fnTableInfoGetRows(tableInfo);
        }
    }

    if (classCount <= 0) {
        Engine::Services::BootstrapLog::Write(
            "[-] Failed to retrieve class count.\n");
        return {};
    }

    std::vector<ClassInfo> classPtrs;
    for (int i = 0; i < classCount; i++) {
        void* klass = nullptr;

        if (m_resolver.module.isIL2CPP) {
            klass = m_resolver.module.exports.fnClassFromIndex(image, i);
        }
        else {
            klass = m_resolver.module.exports.fnClassFromIndex(image, (i + 1) | 0x02000000);
            if (!klass) klass = m_resolver.module.exports.fnClassFromIndex(image, i + 1);
        }

        if (!klass) continue;

        const char* name = m_resolver.module.exports.fnClassGetName(klass);
        const char* ns = m_resolver.module.exports.fnClassGetNamespace
            ? m_resolver.module.exports.fnClassGetNamespace(klass) : "";
        classPtrs.push_back({ name ? name : "UNKNOWN_NAME", ns ? ns : "", klass });
    }

    return classPtrs;
}
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
