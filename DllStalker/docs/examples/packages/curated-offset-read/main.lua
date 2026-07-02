-- Curated Offset Read (Curated profile)
-- Copy this folder to stalker_runtime/mods beside version.dll.
-- Set TEST_OFFSET for your game. Keep WRITE_TEST false until the offset is verified.

local IMAGE = "Assembly-CSharp.dll"
local CLASS = "PlayerController"
local NAMESPACE = ""
local TEST_OFFSET = 0x0
local WRITE_TEST = false
local WRITE_VALUE = 100

ds.log("=== Curated Offset Read Tutorial ===")

if not ds.types or not ds.types.Instance then
    ds.log("Curated helpers are not available; check manifest profile")
    ds.stop()
    return
end

ds.log("ABI version", ds_abi.abi_version(), "features", ds_abi.feature_flags())

local target, find_err = ds.find_object(IMAGE, CLASS, NAMESPACE)
if not target then
    ds.log("instance not found", find_err or "not found")
    ds.stop()
    return
end

local address, address_err = target:address_hex()
ds.log("target", address or "no address", address_err or "")

local typed = ds.types.Instance.wrap(target)
local value, read_err = typed:read_i32(TEST_OFFSET)
if value == nil then
    ds.log("read_i32 failed", read_err or "unknown error")
    ds.stop()
    return
end

ds.log("read_i32", "offset", TEST_OFFSET, "value", value)

if WRITE_TEST then
    local ok, write_err = typed:write_i32(TEST_OFFSET, WRITE_VALUE)
    if not ok then
        ds.log("write_i32 failed", write_err or "unknown error")
    else
        ds.log("write_i32 ok", WRITE_VALUE)
    end
else
    ds.log("write skipped; set WRITE_TEST = true only after verifying TEST_OFFSET")
end

ds.log("=== Curated offset tutorial complete ===")
ds.stop()
