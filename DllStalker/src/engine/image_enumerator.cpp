#include "pch.h"

#include "engine/image_enumerator.h"

#include <cstring>

namespace Engine
{
namespace
{
// Trampoline for Mono's mono_assembly_foreach which is a C function pointer.
struct VisitorContext {
    const UnityModule* module = nullptr;
    const ImageEnumerator::ImageVisitor* visitor = nullptr;
    bool stop = false;
};

void __cdecl ForEachImageTrampoline(void* assembly, void* user_data) {
    auto* ctx = static_cast<VisitorContext*>(user_data);
    if (!ctx || ctx->stop || !assembly || !ctx->module || !ctx->visitor) return;

    auto* getImage = ctx->module->exports.fnGetImage;
    auto* getName  = ctx->module->exports.fnGetImageName;
    if (!getImage || !getName) return;

    void* image = getImage(assembly);
    if (!image) return;

    const char* name = getName(image);
    if (!(*ctx->visitor)(image, name ? name : "UNKNOWN_IMAGE")) {
        ctx->stop = true;
    }
}
} // namespace

ImageEnumerator::ImageEnumerator(UnityModule& module)
    : m_module(module)
{
}

void ImageEnumerator::ForEachImage(const ImageVisitor& visitor) const {
    if (!visitor) return;
    m_module.EnsureThreadAttached();

    if (m_module.isIL2CPP) {
        size_t size = 0;
        void** assemblies = m_module.exports.fnGetAssemblies(m_module.domain, &size);
        if (!assemblies) return;

        for (size_t i = 0; i < size; ++i) {
            void* image = m_module.exports.fnGetImage(assemblies[i]);
            if (!image) continue;

            const char* name = m_module.exports.fnGetImageName(image);
            if (!visitor(image, name ? name : "UNKNOWN_IMAGE")) return;
        }
        return;
    }

    // Release validity may allow Mono boot with domain_assembly_open only;
    // FindImage uses that fast-path. ForEachImage still needs foreach.
    if (!m_module.exports.fnAssemblyForeach) {
        return;
    }

    VisitorContext ctx{ &m_module, &visitor, false };
    m_module.exports.fnAssemblyForeach((void*)ForEachImageTrampoline, &ctx);
}

void* ImageEnumerator::FindImage(const char* assemblyName, int maxAttempts) {
    m_module.EnsureThreadAttached();
    if (!assemblyName || !assemblyName[0]) {
        return nullptr;
    }

    const int attempts = maxAttempts < 1 ? 1 : maxAttempts;
    int attempt = 0;

    printf("[*] Searching for image (substring): %s...\n", assemblyName);

    void* foundImage = nullptr;
    while (attempt < attempts && !foundImage) {
        // --- Stage A: Mono fast-path. domain_assembly_open returns the loaded
        // assembly directly when the image is already known to the runtime.
        if (!m_module.isIL2CPP && m_module.exports.fnMonoAssemblyOpen) {
            if (void* ass = m_module.exports.fnMonoAssemblyOpen(m_module.domain, assemblyName)) {
                if (void* img = m_module.exports.fnGetImage(ass)) {
                    foundImage = img;
                    break;
                }
            }
        }

        // --- Stage B: Fallback iterator (works for both backends).
        // Substring: first match wins — ambiguous for short needles.
        ForEachImage([&](void* image, const char* name) {
            if (name && strstr(name, assemblyName)) {
                foundImage = image;
                return false; // stop iteration
            }
            return true;
        });

        if (foundImage) break;

        ++attempt;
        if (attempt < attempts) {
            Sleep(1000);
            printf("[.] Retry number: %d\n", attempt);
        }
    }

    if (foundImage) {
        printf("[+] Image '%s' found after %d attempt(s).\n", assemblyName, attempt + 1);
        return foundImage;
    }

    printf("[-] Image '%s' NOT found (Timeout).\n", assemblyName);
    return nullptr;
}

void* ImageEnumerator::FindImageExact(const char* assemblyName, int maxAttempts) {
    m_module.EnsureThreadAttached();
    if (!assemblyName || !assemblyName[0]) {
        return nullptr;
    }

    const int attempts = maxAttempts < 1 ? 1 : maxAttempts;
    int attempt = 0;

    printf("[*] Searching for image (exact): %s...\n", assemblyName);

    void* foundImage = nullptr;
    while (attempt < attempts && !foundImage) {
        if (!m_module.isIL2CPP && m_module.exports.fnMonoAssemblyOpen) {
            if (void* ass = m_module.exports.fnMonoAssemblyOpen(m_module.domain, assemblyName)) {
                if (void* img = m_module.exports.fnGetImage(ass)) {
                    // Confirm the opened image's reported name matches exactly
                    // when GetImageName is available.
                    if (m_module.exports.fnGetImageName) {
                        const char* name = m_module.exports.fnGetImageName(img);
                        if (name && std::strcmp(name, assemblyName) == 0) {
                            foundImage = img;
                            break;
                        }
                    }
                    else {
                        foundImage = img;
                        break;
                    }
                }
            }
        }

        ForEachImage([&](void* image, const char* name) {
            if (name && std::strcmp(name, assemblyName) == 0) {
                foundImage = image;
                return false;
            }
            return true;
        });

        if (foundImage) break;

        ++attempt;
        if (attempt < attempts) {
            Sleep(1000);
            printf("[.] Retry number: %d\n", attempt);
        }
    }

    if (foundImage) {
        printf("[+] Image '%s' found (exact) after %d attempt(s).\n", assemblyName, attempt + 1);
        return foundImage;
    }

    printf("[-] Image '%s' NOT found exact (Timeout).\n", assemblyName);
    return nullptr;
}
} // namespace Engine
