// MatchWarmup.cs -- the first item of each kind does not hitch (Switch port, mmim_nx).
//
// A block's item, its bump and its coin, a power-up's projectile (Fireball,
// TailAttack, Hammer...), a spawned enemy and every sound are loaded from
// Resources the first time one appears in a match (PhotonNetwork.Instantiate
// / Resources.Load: the prefab, its sprites, animators and sounds read and
// set up; a sound read and decoded). On the Switch that cost a frame or more
// at the first use of each kind (HW round 17: 67-219 ms, 0.3-0.8 s after a
// power-up, at its first throw). They are loaded in the background while the
// room's lobby is up (Resources.LoadAsync, the list from MvlBuild:
// Resources/SwitchPort/warmup.txt) and kept from then on. Not while the level
// loads: Resources.LoadAll there froze the loading screen 1.4 s (HW round 6),
// and background loads still queued there were finished at once by the
// level's first synchronous load (its player prefab): 1.6 s (HW round 7). So
// they go out 16 at a time, only while the lobby is up: at most one batch
// can be left when Start Game is pressed, and the rest waits for the next
// lobby.

using System.Collections;
using System.Collections.Generic;
using Photon.Pun;
using UnityEngine;
using UnityEngine.SceneManagement;

public class MatchWarmup : MonoBehaviour
{
    static MatchWarmup s_instance;
    static readonly List<Object> s_kept = new List<Object>();
    static string[] s_paths;
    static int s_next; // the first path not asked for yet
    static bool s_loading;
    const int Batch = 16;

    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
    static void Init()
    {
        var go = new GameObject("MatchWarmup");
        DontDestroyOnLoad(go);
        s_instance = go.AddComponent<MatchWarmup>();
        var list = Resources.Load<TextAsset>("SwitchPort/warmup");
        s_paths = list ? list.text.Split(new[] { '\n' }, System.StringSplitOptions.RemoveEmptyEntries) : new string[0];
        DefaultPool.OnLoaded = (id, t0) => NoteLoad("prefab " + id, t0);
        SceneManager.sceneLoaded += (scene, mode) =>
        {
            if (scene.name == "MainMenu" || scene.name == "Loading")
            {
                s_levelStart = s_levelBegan = -1f;
                return;
            }
            s_levelStart = s_levelBegan = Time.realtimeSinceStartup;
            s_hitches = 0;
            s_loadLogs = 0;
            s_gcCount = System.GC.CollectionCount(0);
        };
    }

    // debug.log: a match's long frames (over 50 ms) in its first minute, and
    // whether a garbage collection ran in them, for the next hitch hunt
    static float s_levelStart = -1f, s_levelBegan = -1f; // (s_levelStart: the first minute only)
    static int s_gcCount, s_hitches;
    static float s_lastReal;

    void Update()
    {
        // in a room's lobby: load them now, long before Start Game
        if (s_next < s_paths.Length && !s_loading && InLobby())
            StartCoroutine(Load());
        float dt = Time.realtimeSinceStartup - s_lastReal; // (real time: the game's steps are whole display frames, FramePacer)
        s_lastReal = Time.realtimeSinceStartup;
        if (s_levelStart < 0f)
            return;
        float since = Time.realtimeSinceStartup - s_levelStart;
        int gc = System.GC.CollectionCount(0);
        if (dt > 0.05f && since < 60f && s_hitches < 40)
        {
            s_hitches++;
            Debug.LogWarning("[hitch] " + (dt * 1000f).ToString("F0") + " ms, " + since.ToString("F1") + " s into the level" +
                             (gc != s_gcCount ? ", a GC ran" : "") + (s_loading ? ", warmup loading" : ""));
        }
        s_gcCount = gc;
        if (since >= 60f)
            s_levelStart = -1f;
    }

    static bool InLobby()
    {
        return PhotonNetwork.InRoom && SceneManager.GetActiveScene().name == "MainMenu";
    }

    // debug.log: something a match loaded from Resources for the first time
    // (not warmed up), with what it cost: the next one to add to the list
    static int s_loadLogs;
    public static void NoteLoad(string what, long startTicks)
    {
        if (s_levelBegan < 0f || s_loadLogs >= 60)
            return;
        double ms = (System.Diagnostics.Stopwatch.GetTimestamp() - startTicks) * 1000.0 / System.Diagnostics.Stopwatch.Frequency;
        if (ms < 1.0)
            return;
        s_loadLogs++;
        Debug.LogWarning("[load] " + what + ": " + ms.ToString("F1") + " ms, " +
                         (Time.realtimeSinceStartup - s_levelBegan).ToString("F1") + " s into the level");
    }

    static IEnumerator Load()
    {
        s_loading = true;
        float start = Time.realtimeSinceStartup;
        int first = s_next, prefabs = 0, sounds = 0;
        // the main thread's share of finishing them: at most ~4 ms a frame
        // (BelowNormal), not Normal's ~10
        ThreadPriority priority = Application.backgroundLoadingPriority;
        Application.backgroundLoadingPriority = ThreadPriority.BelowNormal;
        var requests = new List<ResourceRequest>(Batch);
        while (s_next < s_paths.Length && InLobby())
        {
            requests.Clear();
            for (int i = 0; i < Batch && s_next < s_paths.Length; i++, s_next++)
            {
                string path = s_paths[s_next].Trim();
                ResourceRequest r = path.StartsWith("sound/")
                    ? Resources.LoadAsync<AudioClip>(path)
                    : Resources.LoadAsync<GameObject>(path);
                r.priority = -10; // behind anything the menus load
                requests.Add(r);
            }
            foreach (ResourceRequest r in requests)
            {
                while (!r.isDone)
                    yield return null;
                if (!r.asset)
                    continue;
                s_kept.Add(r.asset);
                if (r.asset is AudioClip)
                    sounds++;
                else
                    prefabs++;
            }
        }
        s_loading = false;
        Application.backgroundLoadingPriority = priority;
        Debug.LogWarning("[warmup] " + prefabs + " prefabs and " + sounds + " sounds of " + (s_next - first) +
                         " asked for loaded in the background in the lobby, " +
                         ((Time.realtimeSinceStartup - start) * 1000f).ToString("F0") + " ms" +
                         (s_next < s_paths.Length ? "; " + (s_paths.Length - s_next) + " left for the next lobby" : ""));
    }
}
