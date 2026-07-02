-- Metadata Lookup (Safe)
-- Copy this folder to stalker_runtime/mods beside version.dll.
-- Edit IMAGE / CLASS / FIELD / METHOD below. IMAGE must match the runtime assembly name (often *.dll).

local IMAGE = "Assembly-CSharp.dll"
local CLASS = "PlayerController"
local NAMESPACE = ""
local FIELD = "health"
local METHOD = "Heal"
local METHOD_ARG_COUNT = 1

local function log_handle(label, handle, err)
    if not handle then
        ds.log(label, "failed", err or "unknown error")
        return false
    end

    local address, address_err = handle:address_hex()
    ds.log(label, address or "no address", address_err or "")
    return true
end

ds.log("=== Metadata Lookup Tutorial ===")

local images, images_err = ds.images()
if not images then
    ds.log("ds.images failed", images_err or "unknown error")
    ds.stop()
    return
end

ds.log("loaded image count", #images)
for index = 1, math.min(#images, 5) do
    local image = images[index]
    ds.log("image", index, image.name, "classes", image.class_count)
end

local image, image_err = ds.find_image(IMAGE)
log_handle("image", image, image_err)

local class, class_err = ds.find_class(IMAGE, CLASS, NAMESPACE)
if not log_handle("class", class, class_err) then
    ds.stop()
    return
end

local field, field_err = ds.find_field(class, FIELD)
log_handle("field", field, field_err)

local method, method_err = ds.find_method(class, METHOD, METHOD_ARG_COUNT)
log_handle("method", method, method_err)

ds.log("=== Metadata lookup complete ===")
ds.stop()
