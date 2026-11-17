// WebBuildOptions.cs -- IL2CPP options for the WebGL build only.
//
// An integer divided by zero (x % 0, x / 0) gives 0 on the Switch's and a
// phone's ARM and throws DivideByZeroException on PC, but in WebAssembly it
// traps: the whole page died going back to the main menu after a match
// ("RuntimeError: remainder by zero"). IL2CPP leaves the check out unless
// asked; with it the web build throws, as the PC game does.

#if UNITY_WEBGL
using Unity.IL2CPP.CompilerServices;

[assembly: Il2CppSetOption(Option.DivideByZeroChecks, true)]
#endif
