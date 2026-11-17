// PortPlatform.cs -- which build of the port this is.
//
// The project builds three things: the Switch's APK (MvlBuild.Android, run
// by the mmim_nx loader), a phone APK (MvlBuild.Phone, scripting define
// MMIM_PHONE: touch controls, no console menus) and a WebGL build for
// itch.io (MvlBuild.Web). Before the phone build an Android player was
// always the Switch; the Switch-only parts (the controller screen's Local
// Play, the console's button names, frame pacing for the console's display)
// ask Switch here.

using UnityEngine;
using UnityEngine.InputSystem;

public static class PortPlatform
{
#if MMIM_PHONE
    public static bool Switch => false;
    public static bool Phone => Application.platform == RuntimePlatform.Android;
#else
    public static bool Switch => Application.platform == RuntimePlatform.Android;
    public static bool Phone => false;
#endif
    public static bool Web => Application.platform == RuntimePlatform.WebGLPlayer;

    // on-screen controls in a match: a phone; in a browser, a touch screen in
    // use (TouchControls.WebTouch: a phone's from the start, an iPad's or a
    // touch laptop's once it is touched, off again when a key is pressed)
    public static bool TouchControls => Phone || (Web && global::TouchControls.WebTouch);

    // a browser's console: every log line is a call into JavaScript, and the
    // main menu's character preview alone raised thousands of Photon warnings
    // ("RPCs can only be sent in rooms"): errors and exceptions only
    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSceneLoad)]
    static void QuietWebLog()
    {
        if (Web && !Debug.isDebugBuild)
            Debug.unityLogger.filterLogType = LogType.Error;
    }

    // the platform's words in the port's note (MainMenu's green "!")
    public static string Name => Phone ? "Android" : Web ? "web" : "Nintendo Switch";
}
