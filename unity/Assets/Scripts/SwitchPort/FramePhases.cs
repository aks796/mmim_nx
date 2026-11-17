// FramePhases.cs -- where the main thread's frame goes (Switch port, mmim_nx).
//
// Four players ran at 35 fps on the console with the main thread busy the
// whole frame (2 players: 58-59). Every 10 s debug.log gets a [phases] line:
// the frame's time, and of it the physics step (FixedUpdate to the first
// Update), the scripts and animation (first Update to last LateUpdate), URP's
// rendering of every camera (its context, with each camera's share) and the
// rest (UI canvases, present, garbage collection). Together with the loader's
// [prof] windows (the functions it was in, LocalPlay.Profile in split
// matches) it says what to make cheaper.

using System.Collections.Generic;
using System.Diagnostics;
using UnityEngine;
using UnityEngine.Rendering;

[DefaultExecutionOrder(-32000)]
public class FramePhases : MonoBehaviour
{
    static FramePhases s_instance;
    static readonly double s_msPerTick = 1000.0 / Stopwatch.Frequency;

    long fixedStart = -1, updateStart, renderStart, cameraStart;
    long fixedTicks, updateTicks, renderTicks, windowStart;
    int frames, cameraCalls;
    readonly Dictionary<int, string> cameraNames = new Dictionary<int, string>();
    readonly Dictionary<string, long> cameraTicks = new Dictionary<string, long>();
    readonly Dictionary<string, int> cameraCount = new Dictionary<string, int>();
    readonly System.Text.StringBuilder line = new System.Text.StringBuilder(256);

    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
    static void Create()
    {
        if (s_instance)
            return;
        var go = new GameObject("FramePhases");
        DontDestroyOnLoad(go);
        s_instance = go.AddComponent<FramePhases>();
        go.AddComponent<FramePhasesLate>();
    }

    void OnEnable()
    {
        RenderPipelineManager.beginContextRendering += BeginContext;
        RenderPipelineManager.endContextRendering += EndContext;
        RenderPipelineManager.beginCameraRendering += BeginCamera;
        RenderPipelineManager.endCameraRendering += EndCamera;
        windowStart = Stopwatch.GetTimestamp();
    }

    void OnDisable()
    {
        RenderPipelineManager.beginContextRendering -= BeginContext;
        RenderPipelineManager.endContextRendering -= EndContext;
        RenderPipelineManager.beginCameraRendering -= BeginCamera;
        RenderPipelineManager.endCameraRendering -= EndCamera;
    }

    void FixedUpdate()
    {
        if (fixedStart < 0)
            fixedStart = Stopwatch.GetTimestamp(); // the frame's first physics step
    }

    void Update()
    {
        long now = Stopwatch.GetTimestamp();
        if (fixedStart >= 0)
            fixedTicks += now - fixedStart;
        fixedStart = -1;
        updateStart = now;
    }

    // the last LateUpdate (FramePhasesLate)
    public static void ScriptsDone()
    {
        if (!s_instance)
            return;
        s_instance.updateTicks += Stopwatch.GetTimestamp() - s_instance.updateStart;
        s_instance.frames++;
        s_instance.Report();
    }

    void BeginContext(ScriptableRenderContext context, List<Camera> cameras)
    {
        renderStart = Stopwatch.GetTimestamp();
    }

    void EndContext(ScriptableRenderContext context, List<Camera> cameras)
    {
        renderTicks += Stopwatch.GetTimestamp() - renderStart;
    }

    void BeginCamera(ScriptableRenderContext context, Camera camera)
    {
        cameraStart = Stopwatch.GetTimestamp();
    }

    void EndCamera(ScriptableRenderContext context, Camera camera)
    {
        long ticks = Stopwatch.GetTimestamp() - cameraStart;
        int id = camera.GetInstanceID();
        if (!cameraNames.TryGetValue(id, out string name))
        {
            // "Camera (P2)" counts as "Camera"
            name = camera.name;
            int cut = name.IndexOf(" (");
            if (cut > 0)
                name = name.Substring(0, cut);
            cameraNames[id] = name;
        }
        cameraTicks.TryGetValue(name, out long sum);
        cameraTicks[name] = sum + ticks;
        cameraCount.TryGetValue(name, out int n);
        cameraCount[name] = n + 1;
        cameraCalls++;
    }

    void Report()
    {
        long now = Stopwatch.GetTimestamp();
        double window = (now - windowStart) * s_msPerTick;
        if (window < 10000.0 || frames == 0)
            return;
        double f = 1.0 / frames;
        double frame = window * f, physics = fixedTicks * s_msPerTick * f, scripts = updateTicks * s_msPerTick * f,
               render = renderTicks * s_msPerTick * f;
        line.Length = 0;
        line.Append("[phases] ").Append(LocalPlay.Split ? LocalPlay.Players + "P " : "")
            .Append(GameManager.Instance ? "match" : "menus").Append(": frame ").Append(frame.ToString("F1"))
            .Append(" ms (").Append((1000.0 / frame).ToString("F1")).Append(" fps): physics ").Append(physics.ToString("F1"))
            .Append(", scripts ").Append(scripts.ToString("F1")).Append(", URP ").Append(render.ToString("F1"))
            .Append(" (").Append((cameraCalls * f).ToString("F1")).Append(" cameras a frame");
        foreach (KeyValuePair<string, long> kv in cameraTicks)
            line.Append("; ").Append(kv.Key).Append(' ').Append((kv.Value * s_msPerTick * f).ToString("F2"))
                .Append(" ms x").Append((cameraCount[kv.Key] * f).ToString("F1"));
        line.Append("), the rest ").Append((frame - physics - scripts - render).ToString("F1"))
            .Append(" ms; GC count ").Append(System.GC.CollectionCount(0))
            .Append("; paced in display frames 1/2/3/4+: ").Append(FramePacer.Steps[0]).Append('/').Append(FramePacer.Steps[1])
            .Append('/').Append(FramePacer.Steps[2]).Append('/').Append(FramePacer.Steps[3])
            .Append(", real frame ").Append((FramePacer.MinReal * 1000.0).ToString("F1")).Append('-')
            .Append((FramePacer.MaxReal * 1000.0).ToString("F1")).Append(" ms");
        FramePacer.ResetStats();
        UnityEngine.Debug.LogWarning(line.ToString());
        windowStart = now;
        frames = cameraCalls = 0;
        fixedTicks = updateTicks = renderTicks = 0;
        cameraTicks.Clear();
        cameraCount.Clear();
    }
}
