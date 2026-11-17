// FramePacer.cs -- the game's clock in whole display frames (Switch port, mmim_nx).
//
// The console's screen shows a new picture every 1/60 s, but Unity timed
// each frame from when its main loop happened to get there (MMIM's "Limit
// FPS (VSync)" off: Unity's own 60 fps limiter, the loader's swap waiting
// on the display). At a steady 60 the frames were shown evenly, yet each one
// moved the game by its own measured time -- 14 ms, then 19, then 17 -- so
// running players and the camera following them judder ("the game says 60,
// the camera doesn't move a smooth 60"). Here every frame moves the game by
// a whole number of display frames (Time.captureDeltaTime): one at 60 fps,
// two for a frame that took two, ... The real time not yet given is carried
// over, so the game's clock stays with the real one (within half a frame);
// a long stall (a loading screen) is not caught up on. Physics (1/60 s
// steps) then runs exactly one step a frame at 60.

using UnityEngine;

[DefaultExecutionOrder(-31999)]
public class FramePacer : MonoBehaviour
{
    const double Frame = 1.0 / 60.0;

    static FramePacer s_instance;
    double last = -1.0, debt;

    // FramePhases' [phases] line: frames that moved the game 1, 2, 3, 4+ display frames
    public static readonly int[] Steps = new int[4];
    public static double MinReal = double.MaxValue, MaxReal;

    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
    static void Create()
    {
        // the console's display (vsync at 60 with MMIM's own limiter off);
        // phones and browsers keep Unity's own timing
        if (s_instance || !PortPlatform.Switch)
            return;
        var go = new GameObject("FramePacer");
        DontDestroyOnLoad(go);
        s_instance = go.AddComponent<FramePacer>();
    }

    void Update()
    {
        double now = Time.realtimeSinceStartupAsDouble;
        if (last < 0.0)
        {
            last = now;
            return;
        }
        double real = now - last;
        last = now;
        if (real < MinReal)
            MinReal = real;
        if (real > MaxReal)
            MaxReal = real;
        double want = real + debt;
        int n = Mathf.Clamp((int)System.Math.Round(want / Frame), 1, 4);
        debt = want - n * Frame;
        if (debt > 0.1 || debt < -0.1)
            debt = 0.0; // a stall: the game does not run fast to make it up
        Steps[n - 1]++;
        Time.captureDeltaTime = (float)(n * Frame); // the next frame's step
    }

    public static void ResetStats()
    {
        for (int i = 0; i < Steps.Length; i++)
            Steps[i] = 0;
        MinReal = double.MaxValue;
        MaxReal = 0.0;
    }
}
