// P0.3 placeholder so the project builds; real plugin entry points come in P0.4.
#include <RED4ext/RED4ext.hpp>

RED4EXT_C_EXPORT bool RED4EXT_CALL Main(RED4ext::PluginHandle, RED4ext::EMainReason, const RED4ext::Sdk*)
{
    return true;
}
