// MvlBuild.cs -- the Android build mvl_nx (the Switch loader) runs.
//
//   Unity -batchmode -quit -projectPath mmim_project \
//         -executeMethod MvlBuild.Android -logFile build.log
//
// IL2CPP, arm64 only, OpenGL ES 3 only (mvl_nx draws through dkgl, a GLES
// layer over deko3d; no Vulkan), the scenes of the build settings, written
// to Builds/mmim.apk. Managed stripping stays low: the loader's own C#
// hooks find Unity methods by name, and a stripped method is simply gone
// (MvL Accurate's APK had lost UnityEventBase.GetPersistentEventCount).

using System.Linq;
using UnityEditor;
using UnityEditor.Build.Reporting;
using UnityEngine;
using UnityEngine.Rendering;

public static class MvlBuild
{
    const string Output = "Builds/mmim.apk";
    const string PhoneOutput = "Builds/mmim_phone.apk";
    const int PhoneVersionCode = 3; // the phone APK's release number (3: touch controls hide with a controller)

    [MenuItem("MvL/Build Android APK (mvl_nx)")]
    public static void Android()
    {
        BuildApk(false);
    }

    // the phone APK: the same game for Android phones, with on-screen touch
    // controls (TouchControls) and without the console's parts (PortPlatform:
    // scripting define MMIM_PHONE, this build only)
    [MenuItem("MvL/Build phone APK (touch controls)")]
    public static void Phone()
    {
        BuildApk(true);
    }

    // the WebGL build for itch.io: Builds/web (index.html, Build/, TemplateData/),
    // zipped as it is for an HTML5 upload
    const string WebOutput = "Builds/web";

    [MenuItem("MvL/Build WebGL (itch.io)")]
    public static void Web()
    {
        EditorUserBuildSettings.SwitchActiveBuildTarget(BuildTargetGroup.WebGL, BuildTarget.WebGL);
        EditorUserBuildSettings.development = false;
        // itch.io serves the files without Content-Encoding: gzip with the
        // loader's own decompression (decompressionFallback)
        PlayerSettings.WebGL.compressionFormat = WebGLCompressionFormat.Gzip;
        PlayerSettings.WebGL.decompressionFallback = true;
        PlayerSettings.WebGL.dataCaching = true;
        // MMIM's code raises NullReferenceExceptions it catches (and some it
        // does not): without exception support a WebGL build aborts at the
        // first null instead of throwing
        PlayerSettings.WebGL.exceptionSupport = WebGLExceptionSupport.FullWithoutStacktrace;
        PlayerSettings.WebGL.linkerTarget = WebGLLinkerTarget.Wasm;
        PlayerSettings.WebGL.threadsSupport = false;
        // Assets/WebGLTemplates/MMIM: the whole window on any screen, a phone's
        // first touch full screen and sideways, no page scrolling or zoom
        PlayerSettings.WebGL.template = "PROJECT:MMIM";
        // function names for a crash's stack (Build/*.symbols.json, fetched only
        // when an error is shown)
        PlayerSettings.WebGL.debugSymbolMode = WebGLDebugSymbolMode.External;
        PlayerSettings.defaultWebScreenWidth = 1280;
        PlayerSettings.defaultWebScreenHeight = 720;
        PlayerSettings.runInBackground = true; // Photon keeps its connection while the tab is in the background
        PlayerSettings.SetUseDefaultGraphicsAPIs(BuildTarget.WebGL, false);
        PlayerSettings.SetGraphicsAPIs(BuildTarget.WebGL, new[] { GraphicsDeviceType.OpenGLES3 }); // WebGL 2
        PlayerSettings.SetManagedStrippingLevel(BuildTargetGroup.WebGL, ManagedStrippingLevel.Low);
        PlayerSettings.SetStackTraceLogType(LogType.Exception, StackTraceLogType.ScriptOnly);
        PlayerSettings.SetStackTraceLogType(LogType.Error, StackTraceLogType.ScriptOnly);

        WriteWarmupList();

        var scenes = EditorBuildSettings.scenes.Where(s => s.enabled).Select(s => s.path).ToArray();
        var options = new BuildPlayerOptions
        {
            scenes = scenes,
            locationPathName = WebOutput,
            target = BuildTarget.WebGL,
            targetGroup = BuildTargetGroup.WebGL,
            options = BuildOptions.None,
        };
        BuildReport report = BuildPipeline.BuildPlayer(options);
        Debug.Log($"[MvlBuild] {report.summary.result}: {WebOutput}, {report.summary.totalSize} bytes, " +
                  $"{report.summary.totalErrors} errors, {scenes.Length} scenes");
        if (Application.isBatchMode)
            EditorApplication.Exit(report.summary.result == BuildResult.Succeeded ? 0 : 1);
    }

    static void BuildApk(bool phone)
    {
        string output = phone ? PhoneOutput : Output;
        // the JDK, SDK, NDK and Gradle Unity Hub installed with the Android
        // module (batch mode does not pick them up on its own: "JDK not found")
        EditorPrefs.SetBool("JdkUseEmbedded", true);
        EditorPrefs.SetBool("SdkUseEmbedded", true);
        EditorPrefs.SetBool("NdkUseEmbedded", true);
        EditorPrefs.SetBool("GradleUseEmbedded", true);
        // the JDK by path: batch mode still answered "JDK not found" for the
        // embedded one (Hub installs an x86_64 JDK next to the arm64 editor)
        string jdk = System.Environment.GetEnvironmentVariable("MVL_JDK");
        if (string.IsNullOrEmpty(jdk))
            jdk = System.IO.Path.Combine(EditorApplication.applicationContentsPath,
                                         "../../PlaybackEngines/AndroidPlayer/OpenJDK");
        jdk = System.IO.Path.GetFullPath(jdk);
        EditorPrefs.SetBool("JdkUseEmbedded", false);
        UnityEditor.Android.AndroidExternalToolsSettings.jdkRootPath = jdk;
        Debug.Log($"[MvlBuild] JDK {jdk} (exists {System.IO.Directory.Exists(jdk)})");
        EditorUserBuildSettings.SwitchActiveBuildTarget(BuildTargetGroup.Android, BuildTarget.Android);
        EditorUserBuildSettings.buildAppBundle = false;
        EditorUserBuildSettings.exportAsGoogleAndroidProject = false;
        EditorUserBuildSettings.development = false;

        PlayerSettings.SetApplicationIdentifier(BuildTargetGroup.Android, "com.miibumm.mmim");
        PlayerSettings.SetScriptingBackend(BuildTargetGroup.Android, ScriptingImplementation.IL2CPP);
        PlayerSettings.Android.targetArchitectures = AndroidArchitecture.ARM64;
        PlayerSettings.SetUseDefaultGraphicsAPIs(BuildTarget.Android, false);
        PlayerSettings.SetGraphicsAPIs(BuildTarget.Android, new[] { GraphicsDeviceType.OpenGLES3 });
        PlayerSettings.SetManagedStrippingLevel(BuildTargetGroup.Android, ManagedStrippingLevel.Low);
        PlayerSettings.Android.minSdkVersion = AndroidSdkVersions.AndroidApiLevel22;
        // Android's version code: phones install a release over the last one only
        // when it is not lower (v1.0.0 = 1, v1.0.1 = 2). The version name stays
        // MMIM's own (bundleVersion "MMIM_1.0.0": the online AppVersion PC MMIM
        // players match on); the Switch loader reads neither
        PlayerSettings.Android.bundleVersionCode = PhoneVersionCode;
        PlayerSettings.defaultInterfaceOrientation = UIOrientation.LandscapeLeft;
        // the script's stack with every error and exception in the log (the
        // Switch's debug.log), not just the message
        PlayerSettings.SetStackTraceLogType(LogType.Exception, StackTraceLogType.ScriptOnly);
        PlayerSettings.SetStackTraceLogType(LogType.Error, StackTraceLogType.ScriptOnly);
        PlayerSettings.SetStackTraceLogType(LogType.Assert, StackTraceLogType.ScriptOnly);

        WriteWarmupList();

        var scenes = EditorBuildSettings.scenes.Where(s => s.enabled).Select(s => s.path).ToArray();
        var options = new BuildPlayerOptions
        {
            scenes = scenes,
            locationPathName = output,
            extraScriptingDefines = phone ? new[] { "MMIM_PHONE" } : null,
            target = BuildTarget.Android,
            targetGroup = BuildTargetGroup.Android,
            // the data as one LZ4 bundle, assets/bin/Data/data.unity3d (what
            // mvl_nx reads, as in the MvL Accurate APK it was brought up on)
            options = BuildOptions.CompressWithLz4,
        };
        BuildReport report = BuildPipeline.BuildPlayer(options);
        Debug.Log($"[MvlBuild] {report.summary.result}: {output}, {report.summary.totalSize} bytes, " +
                  $"{report.summary.totalErrors} errors, {scenes.Length} scenes");
        if (Application.isBatchMode)
            EditorApplication.Exit(report.summary.result == BuildResult.Succeeded ? 0 : 1);
    }

    // Resources/SwitchPort/warmup.txt: what a match loads from Resources the
    // first time it is used (MatchWarmup.cs loads them in the background in a
    // room's lobby; Resources has no way to list a folder without loading all
    // of it at once): items, bumps, particles, the projectiles and attacks
    // (Fireball, TailAttack, Hammer...: the HW log's mid-match hitches came
    // 0.3-0.8 s after a power-up, at its first throw), enemies, the cloud
    // platform, and every sound a match plays (sound/..., AudioClips; music
    // and menu sounds are not in it)
    static void WriteWarmupList()
    {
        string root = "Assets/Resources/";
        var lines = new System.Collections.Generic.List<string>();
        void Add(string folder, string pattern, System.IO.SearchOption depth, System.Func<string, bool> keep = null)
        {
            string dir = root + folder;
            if (!System.IO.Directory.Exists(dir))
                return;
            foreach (string f in System.IO.Directory.GetFiles(dir, pattern, depth))
            {
                string rel = f.Replace('\\', '/').Substring(root.Length);
                rel = rel.Substring(0, rel.LastIndexOf('.'));
                if (keep == null || keep(rel))
                    lines.Add(rel);
            }
        }
        foreach (string folder in new[] { "prefabs/Powerup", "prefabs/bump", "prefabs/particle", "prefabs/enemy" })
            Add(folder, "*.prefab", System.IO.SearchOption.AllDirectories);
        // the projectiles and attacks beside the players' own prefabs (loaded with the level)
        Add("prefabs", "*.prefab", System.IO.SearchOption.TopDirectoryOnly,
            rel => !System.IO.Path.GetFileName(rel).StartsWith("Player"));
        Add("prefabs/static", "CloudFlowerPlatform.prefab", System.IO.SearchOption.TopDirectoryOnly);
        foreach (string folder in new[] { "sound/character", "sound/enemy", "sound/player", "sound/powerup", "sound/world" })
            Add(folder, "*.ogg", System.IO.SearchOption.AllDirectories);
        lines.Sort(System.StringComparer.Ordinal);
        System.IO.Directory.CreateDirectory(root + "SwitchPort");
        System.IO.File.WriteAllText(root + "SwitchPort/warmup.txt", string.Join("\n", lines) + "\n");
        AssetDatabase.ImportAsset(root + "SwitchPort/warmup.txt");
        Debug.Log($"[MvlBuild] warmup list: {lines.Count} prefabs and sounds");
    }
}
