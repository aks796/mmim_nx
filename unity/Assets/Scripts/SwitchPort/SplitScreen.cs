// SplitScreen.cs -- each local player's own part of the screen (Switch port, mmim_nx).
//
// The layout, as console games split one screen:
//   2 players   top and bottom (as HDErick's MvL Accurate local play does):
//               player 1 above, player 2 below, each the full width
//   3 players   quarters: 1 top left, 2 top right, 3 bottom left; the fourth
//               is the star's camera (StarCamera: the Big Star, a loose
//               star, else the leader) with the standings in its corner
//               (MMIM's scoreboard, always open there)
// With 2 or 4 players every part has its own standings instead (a copy of
// MMIM's scoreboard where MMIM has it, at the part's HUD size, as split-screen
// racers show each player the ranking), open as MMIM's setting says.
//   4 players   quarters: 1 top left, 2 top right, 3 bottom left, 4 bottom right
// Every part shows the stage's full height: the camera keeps MvL's
// orthographic size whatever the part's shape (HorizontalCamera), so in a
// half or a quarter the world is drawn smaller and nobody plays with the
// stage cut off; a top or bottom half shows more of the stage across.
//
// A level's camera rig is three URP cameras: "Camera" (base, layer 1 only:
// the parallax backgrounds, moved by its BackgroundLoop for that camera),
// "BaseCamera" (overlay: the world) and "ScrollCamera" (overlay: the world one
// level-width over, for looping stages). Players 2-4 (and three players'
// star camera) each get a copy of the rig, and a copy of the backgrounds on
// a layer of their own (16-18) that
// only their base camera draws, so each part scrolls its own parallax. Every
// camera of a rig gets the part's viewport (URP overlays use the base
// camera's viewport but their own aspect for an orthographic projection, so
// theirs must match). A half is wider than the narrowest looping stage (the
// Brick level is 16 wide, a half about 25), so each rig has a second
// ScrollCamera for the stage's wrap on the other side too
// (SecondaryCameraPositioner.otherSide); both only draw near an edge.
//
// The HUD: player 1's stars, coins, lives, timer, item box and star track
// move into player 1's part, a copy of them (its own UIUpdater) into each
// other part: the full-screen HUD at 0.7 size in a half, 0.5 in a quarter, so
// nothing overlaps. Each part has its own death wipe; pause, the level's
// start and end fades and the win text stay full screen, in front of every
// part: the other players' HUDs are canvases of their own, sorted under
// player 1's (which has the pause menu and Settings), and every part's
// standings sit under the pause menu (MMIM has its scoreboard as the HUD's
// last child, over the pause menu: fine at the screen's left edge, not over
// a part in the middle of the screen).
//
// One camera per part: MMIM's rig draws the backgrounds with one URP camera
// and the world with another stacked on it; both are at the same place and
// size, and each URP camera costs the main thread a whole render set-up (four
// players: eight cameras, 35 fps on the console). In split play the rig's
// base camera draws both: the world camera's layers added to it, the world
// camera turned off. The backgrounds are far behind the world (z 20-40) and
// drawn first within their sorting layer (their orders moved 30000 down,
// kept in their own order), as when they were a camera of their own.
//
// Set up from GameManager.Awake, before any Start: BackgroundLoop tiles its
// backgrounds in its Start, so they are copied untouched.

using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering.Universal;
using UnityEngine.UI;

public class SplitScreen : MonoBehaviour
{
    const int LayerBackground = 1;
    const int LayerFirstCopy = 16; // players 2-4's backgrounds: 16, 17, 18 (unused in MMIM)
    const int CopyLayers = ((1 << (LocalPlay.MaxPlayers - 1)) - 1) << LayerFirstCopy;

    static SplitScreen s_instance;
    int players;
    readonly Camera[] rigs = new Camera[LocalPlay.MaxPlayers];
    readonly UIUpdater[] huds = new UIUpdater[LocalPlay.MaxPlayers];
    readonly FadeOutManager[] faders = new FadeOutManager[LocalPlay.MaxPlayers];
    readonly Transform[] partBoxes = new Transform[LocalPlay.MaxPlayers];
    readonly List<ScoreboardUpdater> boards = new List<ScoreboardUpdater>();
    const string ScoreboardName = "Scoreboard";

    public static bool On => s_instance;

    // three players: the fourth quarter (the star's camera) shows the standings
    public static bool Standings => s_instance && s_instance.players == 3;

    static int LayerOf(int slot)
    {
        return slot == 0 ? LayerBackground : LayerFirstCopy + slot - 1;
    }

    // a player's part of the screen (a viewport: 0..1 from the bottom left)
    public static Rect ViewOf(int slot, int players)
    {
        if (players <= 1)
            return new Rect(0f, 0f, 1f, 1f);
        if (players == 2)
            return new Rect(0f, slot == 0 ? 0.5f : 0f, 1f, 0.5f);
        return new Rect((slot % 2) * 0.5f, slot < 2 ? 0.5f : 0f, 0.5f, 0.5f);
    }

    public static float HudScale(int players)
    {
        return players == 2 ? 0.7f : 0.5f;
    }

    // the base camera ("Camera", with BackgroundLoop) of a player's part
    public static Camera CameraFor(int slot)
    {
        if (s_instance && slot >= 0 && slot < s_instance.rigs.Length && s_instance.rigs[slot])
            return s_instance.rigs[slot];
        return Camera.main;
    }

    // the box over a player's part on their own HUD copy ("Part (P2)"):
    // LocalNametags draws the other players' tags in it
    public static Transform PartBox(int slot)
    {
        if (s_instance && slot >= 0 && slot < s_instance.partBoxes.Length)
            return s_instance.partBoxes[slot];
        return null;
    }

    // a player's death wipe (the Bowser fade), over its own part only; null
    // outside split play (MMIM's full-screen one, tag FadeUI)
    public static FadeOutManager FaderFor(int slot)
    {
        if (s_instance && slot >= 0 && slot < s_instance.faders.Length)
            return s_instance.faders[slot];
        return null;
    }

    // every scoreboard (one per player's part with 2 or 4 players; MMIM's own otherwise)
    public static IEnumerable<ScoreboardUpdater> Scoreboards()
    {
        if (!s_instance || s_instance.boards.Count == 0)
        {
            if (ScoreboardUpdater.instance)
                yield return ScoreboardUpdater.instance;
            yield break;
        }
        foreach (ScoreboardUpdater b in s_instance.boards)
            if (b)
                yield return b;
    }

    // every HUD (one per player; MMIM's own alone outside split play)
    public static IEnumerable<UIUpdater> Huds()
    {
        if (!s_instance)
        {
            if (UIUpdater.Instance)
                yield return UIUpdater.Instance;
            yield break;
        }
        foreach (UIUpdater h in s_instance.huds)
            if (h)
                yield return h;
    }

    public static void Setup(GameManager gm)
    {
        if (!LocalPlay.Split || s_instance)
            return;
        s_instance = gm.gameObject.AddComponent<SplitScreen>();
        LocalPlay.Profile(true); // where the frame goes, for the console's debug.log
        try
        {
            s_instance.Build(gm);
            LocalNametags.Make(gm); // P1-P4 over each player in the others' parts
        }
        catch (System.Exception e)
        {
            Debug.LogWarning("[split] set-up failed: " + e);
        }
    }

    void OnDestroy()
    {
        if (s_instance == this)
        {
            s_instance = null;
            LocalPlay.Profile(false);
        }
    }

    // --- cameras ------------------------------------------------------------------------------

    void Build(GameManager gm)
    {
        players = Mathf.Clamp(LocalPlay.Players, 2, LocalPlay.MaxPlayers);
        BackgroundLoop loop = gm.GetComponentInChildren<BackgroundLoop>(true);
        Camera rig1 = loop ? loop.GetComponent<Camera>() : Camera.main;
        GameObject backgrounds = GameObject.FindGameObjectWithTag("Backgrounds");

        // the stage's wrap on both sides (before the rig is copied: the copies have theirs)
        if (gm.loopingLevel)
            OtherSideScroll(rig1);

        // layer-1 renderers outside the backgrounds (a moon, a background
        // tilemap): every other player's base camera needs its own copies
        var loose = new List<GameObject>();
        foreach (Renderer r in FindObjectsOfType<Renderer>())
            if (r.gameObject.layer == LayerBackground && (!backgrounds || !r.transform.IsChildOf(backgrounds.transform)) &&
                !loose.Contains(r.gameObject))
                loose.Add(r.gameObject);
        // a parent's copy brings its children along
        loose.RemoveAll(g => loose.Exists(p => p != g && g.transform.IsChildOf(p.transform)));

        if (backgrounds)
        {
            // renderers in the backgrounds on other layers are drawn by the
            // world cameras, in every part at player 1's parallax: to layer 1
            foreach (Renderer r in backgrounds.GetComponentsInChildren<Renderer>(true))
                if (r.gameObject.layer != LayerBackground)
                    r.gameObject.layer = LayerBackground;
        }
        // the backgrounds drawn before the world by one camera (copies below take it along)
        int behind = 0;
        foreach (Renderer r in FindObjectsOfType<Renderer>(true))
            if (r.gameObject.layer == LayerBackground)
            {
                r.sortingOrder = Mathf.Max(r.sortingOrder - BehindWorld, short.MinValue);
                behind++;
            }
        // three players: a fourth rig, the star's camera
        int views = players == 3 ? 4 : players;
        for (int slot = 1; slot < views; slot++)
        {
            string tag = slot < players ? " (P" + (slot + 1) + ")" : " (Star)";
            if (backgrounds && loop)
            {
                GameObject copy = Copy(backgrounds, backgrounds.transform.parent, "Backgrounds" + tag);
                copy.tag = "Untagged";
                SetLayer(copy, LayerOf(slot));
                copy.SetActive(true);
                GameObject rig = Copy(rig1.gameObject, rig1.transform.parent, "Camera" + tag);
                rig.tag = "Untagged"; // Camera.main stays player 1's
                foreach (AudioListener l in rig.GetComponentsInChildren<AudioListener>(true))
                    DestroyImmediate(l); // one listener: player 1's
                rig.GetComponent<BackgroundLoop>().root = copy.transform;
                rigs[slot] = rig.GetComponent<Camera>();
                rig.SetActive(true);
            }
            foreach (GameObject g in loose)
            {
                GameObject copy = Copy(g, g.transform.parent, g.name + tag);
                SetLayer(copy, LayerOf(slot));
                copy.SetActive(true);
            }
        }
        rigs[0] = rig1;
        if (views > players && rigs[players])
            rigs[players].gameObject.AddComponent<StarCamera>();

        for (int slot = 0; slot < views; slot++)
        {
            Camera rig = rigs[slot];
            if (!rig)
                continue;
            Rect rect = ViewOf(slot, players);
            foreach (Camera c in rig.GetComponentsInChildren<Camera>(true))
            {
                c.rect = rect;
                if (c == rig)
                    c.cullingMask = 1 << LayerOf(slot);
                else
                    c.cullingMask &= ~CopyLayers; // the world cameras: no copied backgrounds
                HorizontalCamera h = c.GetComponent<HorizontalCamera>();
                if (h)
                    h.viewSlot = slot;
            }
            if (!TwoCameras)
                OneCamera(rig);
        }
        int made = 0;
        foreach (Camera r in rigs)
            if (r)
                made++;
        Debug.LogWarning("[split] " + players + " players: " + made + " camera rig(s)" + (views > players ? " (one the star's)" : "") + ", " + loose.Count +
                         " loose background object(s) copied per player, " + (TwoCameras ? "MMIM's two cameras each" : "one camera each (" + behind + " background renderers first)"));

        BuildHud(gm);
    }

    const int BehindWorld = 30000;
    public static bool TwoCameras; // OfflineTest "cameras 2": MMIM's rig as it was, to compare

    // the rig's world camera's layers drawn by its base camera, the world camera off
    static void OneCamera(Camera rig)
    {
        UniversalAdditionalCameraData data = rig.GetUniversalAdditionalCameraData();
        if (!data)
            return;
        foreach (Camera c in rig.GetComponentsInChildren<Camera>(true))
        {
            if (c == rig || c.GetComponent<SecondaryCameraPositioner>())
                continue;
            UniversalAdditionalCameraData d = c.GetUniversalAdditionalCameraData();
            if (!d || d.renderType != CameraRenderType.Overlay)
                continue;
            rig.cullingMask |= c.cullingMask;
            data.cameraStack.Remove(c);
            c.enabled = false;
            HorizontalCamera h = c.GetComponent<HorizontalCamera>();
            if (h)
                h.enabled = false;
        }
    }

    // a second ScrollCamera: the stage's wrap on the side the first one is not drawing
    static void OtherSideScroll(Camera rig)
    {
        SecondaryCameraPositioner scroll = rig.GetComponentInChildren<SecondaryCameraPositioner>(true);
        Camera cam = scroll ? scroll.GetComponent<Camera>() : null;
        if (!cam)
            return;
        GameObject other = Instantiate(scroll.gameObject, scroll.transform.parent);
        other.name = scroll.name + " (other side)";
        other.GetComponent<SecondaryCameraPositioner>().otherSide = true;
        UniversalAdditionalCameraData data = rig.GetUniversalAdditionalCameraData();
        Camera otherCam = other.GetComponent<Camera>();
        if (data && otherCam && !data.cameraStack.Contains(otherCam))
            data.cameraStack.Insert(data.cameraStack.IndexOf(cam) + 1, otherCam);
    }

    // a copy of a scene object made while the original is inactive, so
    // nothing in the copy wakes before it is trimmed (returned inactive)
    static GameObject Copy(GameObject original, Transform parent, string name)
    {
        bool was = original.activeSelf;
        original.SetActive(false);
        GameObject copy = Instantiate(original, parent);
        original.SetActive(was);
        copy.name = name;
        // no second physics, network object or sound source (colliders before
        // the rigidbodies they need)
        foreach (Photon.Pun.PhotonView c in copy.GetComponentsInChildren<Photon.Pun.PhotonView>(true))
            DestroyImmediate(c);
        foreach (Collider2D c in copy.GetComponentsInChildren<Collider2D>(true))
            DestroyImmediate(c);
        foreach (Rigidbody2D c in copy.GetComponentsInChildren<Rigidbody2D>(true))
            DestroyImmediate(c);
        foreach (AudioSource c in copy.GetComponentsInChildren<AudioSource>(true))
            DestroyImmediate(c);
        return copy;
    }

    static void SetLayer(GameObject go, int layer)
    {
        foreach (Transform t in go.GetComponentsInChildren<Transform>(true))
            t.gameObject.layer = layer;
    }

    // --- the HUD ------------------------------------------------------------------------------

    static readonly string[] HudParts = { "LeftColumnParent", "RightColumnParent", "Track", "Track Templates", "StoredItem" };

    void BuildHud(GameManager gm)
    {
        UIUpdater hud1 = gm.GetComponentInChildren<UIUpdater>(true);
        if (!hud1)
            return;
        // no "Ping: N ms" (online only)
        Transform ping = hud1.transform.Find("Ping");
        if (ping)
            ping.gameObject.SetActive(false);
        huds[0] = hud1;
        // the other players' HUDs, copied before player 1's parts move
        for (int slot = 1; slot < players; slot++)
        {
            GameObject go = Copy(hud1.gameObject, hud1.transform.parent, "New HUD (P" + (slot + 1) + ")");
            // only the HUD parts (and with 2 or 4 players the scoreboard): no
            // second pause menu, event system, fade or win text
            var drop = new List<GameObject>();
            foreach (Transform child in go.transform)
                if (System.Array.IndexOf(HudParts, child.name) < 0 && !(players != 3 && child.name == ScoreboardName))
                    drop.Add(child.gameObject);
            foreach (GameObject g in drop)
                DestroyImmediate(g);
            UIUpdater hud = go.GetComponent<UIUpdater>();
            hud.viewSlot = slot;
            huds[slot] = hud;
        }
        // player 1's HUD (pause, Settings, the quit prompt, the win text, the
        // fades) in front of the other players' copies
        Canvas canvas1 = hud1.GetComponent<Canvas>();
        if (canvas1)
            canvas1.sortingOrder += 1;
        for (int slot = 0; slot < players; slot++)
            if (huds[slot])
                Part(huds[slot].transform, slot);
        Lines(hud1.transform);
        for (int slot = 1; slot < players; slot++)
            if (huds[slot])
                huds[slot].gameObject.SetActive(true);
        Faders(hud1.transform);
        if (players == 3)
            PlaceStandings(hud1.transform);
        // the scoreboards GameManager fills (each copy's Awake made itself
        // ScoreboardUpdater.instance: MMIM's own is that again)
        foreach (UIUpdater hud in huds)
            if (hud)
                boards.AddRange(hud.GetComponentsInChildren<ScoreboardUpdater>(true));
        ScoreboardUpdater mine = hud1.GetComponentInChildren<ScoreboardUpdater>(true);
        if (mine)
            ScoreboardUpdater.instance = mine;
    }

    static HudView Box(Transform canvas, string name, Rect view, float scale)
    {
        var go = new GameObject(name, typeof(RectTransform));
        var rt = (RectTransform)go.transform;
        rt.SetParent(canvas, false);
        go.layer = canvas.gameObject.layer;
        HudView box = go.AddComponent<HudView>();
        box.view = view;
        box.scale = scale;
        box.Fit();
        return box;
    }

    // the HUD parts into a box over the player's part, the full-screen layout smaller
    void Part(Transform canvas, int slot)
    {
        HudView box = Box(canvas, "Part (P" + (slot + 1) + ")", ViewOf(slot, players), HudScale(players));
        box.transform.SetSiblingIndex(0);
        partBoxes[slot] = box.transform;
        foreach (string name in HudParts)
        {
            Transform part = canvas.Find(name);
            if (part)
                part.SetParent(box.transform, false);
        }
        // 2 or 4 players: the player's own standings, in a box of their own
        // clipped to the part (the scoreboard slides out to the left)
        Transform board = players != 3 ? canvas.Find(ScoreboardName) : null;
        if (board)
        {
            HudView standings = Box(canvas, "Standings (P" + (slot + 1) + ")", ViewOf(slot, players), HudScale(players));
            standings.gameObject.AddComponent<RectMask2D>();
            standings.transform.SetSiblingIndex(box.transform.GetSiblingIndex() + 1); // with the part, under the pause menu
            board.SetParent(standings.transform, false);
        }
    }

    // the death wipe per player: a copy of MMIM's fader in a box clipped to
    // the player's part (RectMask2D), under the full-screen one (level start
    // and end stay full screen). Its animator starts black ("New State"):
    // opened at once, during the loading screen.
    void Faders(Transform canvas)
    {
        Transform fade = canvas.Find("FadeOutImage");
        if (!fade)
            return;
        int at = fade.GetSiblingIndex();
        for (int slot = 0; slot < players; slot++)
        {
            HudView box = Box(canvas, "Fade (P" + (slot + 1) + ")", ViewOf(slot, players), 1f);
            box.gameObject.AddComponent<RectMask2D>();
            box.transform.SetSiblingIndex(at++);
            GameObject copy = Instantiate(fade.gameObject, box.transform, false);
            copy.name = "FadeOutImage (P" + (slot + 1) + ")";
            copy.tag = "Untagged"; // FindGameObjectWithTag("FadeUI") stays MMIM's own
            copy.GetComponent<Animator>().SetTrigger("Normal");
            faders[slot] = copy.GetComponent<FadeOutManager>();
        }
    }

    // three players: MMIM's scoreboard in the star camera's quarter, where
    // MMIM has it (left, a quarter down) at the HUD's size, open for good
    // (GameManager) and clipped to it (it slides out to the left)
    void PlaceStandings(Transform canvas)
    {
        ScoreboardUpdater board = canvas.GetComponentInChildren<ScoreboardUpdater>(true);
        if (!board)
            return;
        Transform pause = canvas.Find("Pause");
        HudView box = Box(board.transform.parent, "Standings", ViewOf(3, 4), HudScale(players));
        box.gameObject.AddComponent<RectMask2D>();
        // under the pause menu (MMIM's scoreboard is the HUD's last child)
        box.transform.SetSiblingIndex(pause && pause.parent == box.transform.parent ? pause.GetSiblingIndex() : board.transform.GetSiblingIndex());
        board.transform.SetParent(box.transform, false);
    }

    // black lines where the parts meet: across for two players, a cross for three or four
    void Lines(Transform canvas)
    {
        Line(canvas, "Split Line", false);
        if (players > 2)
            Line(canvas, "Split Line (down)", true);
    }

    static void Line(Transform canvas, string name, bool vertical)
    {
        var go = new GameObject(name, typeof(RectTransform), typeof(Image));
        var rt = (RectTransform)go.transform;
        rt.SetParent(canvas, false);
        rt.SetSiblingIndex(0);
        go.layer = canvas.gameObject.layer;
        rt.anchorMin = vertical ? new Vector2(0.5f, 0f) : new Vector2(0f, 0.5f);
        rt.anchorMax = vertical ? new Vector2(0.5f, 1f) : new Vector2(1f, 0.5f);
        rt.pivot = new Vector2(0.5f, 0.5f);
        rt.anchoredPosition = Vector2.zero;
        rt.sizeDelta = vertical ? new Vector2(3f, 0f) : new Vector2(0f, 3f);
        Image img = go.GetComponent<Image>();
        img.color = Color.black;
        img.raycastTarget = false;
    }
}
