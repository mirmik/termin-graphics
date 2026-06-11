// Termin Slang prelude.
//
// User attributes declared here are consumed by termin_shaderc through
// slangc reflection JSON. They describe Termin runtime metadata, not backend
// register placement.

[__AttributeUsage(_AttributeTargets.Var)]
public struct TerminScopeAttribute
{
    string value;
}
