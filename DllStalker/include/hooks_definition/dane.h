#pragma once

namespace Hooks
{
namespace Dane
{
typedef void(__fastcall* _Awake)(void* __this);
extern _Awake oAwake;

void __fastcall hkAwake(void* __this);

} // namespace Dane
} // namespace Hooks
