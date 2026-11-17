// ControllerMenuSupport.cs -- the menus played with a controller (Switch port, mmim_nx).
//
// MMIM's menus were made for a mouse (MiiBumm's own note in the game says so):
// a controller loses its selection, some panels open with nothing selected,
// some buttons are dead ends, and the title only takes the button that happens
// to be selected. This keeps a console's behaviour on top of them, without
// changing the scenes:
//
//  * only the front-most panel is played: MMIM's popups (the note, the
//    error box, Settings) are children of the canvas after the menus they
//    open over, so the last active one with an item to select is in front,
//    and nothing under it can be selected (the note's box does not cover the
//    Main Menu's buttons, and the pointer could still reach them);
//  * there is always a selection while a controller is in use: when it is
//    lost (nothing selected, the selected object hidden or disabled, or
//    behind the front panel), the item it came from (a popup closed) or the
//    top-left item of the front panel is selected again;
//  * every visible item has a way out in every direction: designed links are
//    kept while they lead to a visible item, the rest go to the nearest
//    visible item that way (never to one under the panel that is open):
//    the nearest row first, then the item most in line, as a console menu
//    moves (Settings: up from Fullscreen is SFX, not Music);
//  * a panel of choices opens on the one in use (the maps: the room's map);
//  * the title's "Press any button..." takes any face, shoulder or +/-
//    button, as a Submit on the title;
//  * B goes back: the front panel's Back button (a button whose click plays
//    MainMenuManager.BackSound; the note's arrow closes the note); on the
//    Main Menu, the title; in a paused match, Settings, the quit prompt or
//    the pause menu closes;
//  * a selection inside a scroll list scrolls into view (smoothly: MMIM's
//    own KeepChildInFocus centres it where a list has one; the heading of a
//    section shows with its first row), and a scroll
//    list with nothing to select in it (the credits) scrolls with the stick
//    or the D-Pad held up or down (the R Stick too);
//  * a slider takes left/right (up/down when vertical) itself, a scrollbar
//    is never selected, and a dropdown's open list (its own canvas, sorted
//    over everything) is the front panel until it closes;
//  * moving onto a text field does not open the keyboard; A does;
//  * the selected box (text field, dropdown, number box) shows inverted, as
//    MMIM's own selected option box (SelectedBoxLook.cs);
//  * in the local room every player has a cursor (LocalCursors.cs): players
//    2-4 in their own box, player 1 everywhere else; each cursor is kept as
//    above within its own part (its links, its front panel, its Back: B
//    closes a player's open list or palette; leaving the room is player 1's).
//
// Touch keeps working: touching the screen leaves controller mode until a
// button is pressed again.

using System.Collections;
using System.Collections.Generic;
using System.Reflection;
using NSMB.Utils;
using Photon.Pun;
using UnityEngine;
using UnityEngine.Events;
using UnityEngine.EventSystems;
using UnityEngine.InputSystem;
using UnityEngine.UI;

public class ControllerMenuSupport : MonoBehaviour
{
    static ControllerMenuSupport s_instance;

    bool controllerMode;
    public static bool ControllerMode => s_instance && s_instance.controllerMode;
    static int s_logged;
    static bool s_checkedArgs;

    // a cursor: an event system's selection, kept. One, or in the local room
    // one per player (LocalCursors): zone 0 player 1's (everything but the
    // other players' boxes), 1-3 player 2-4's box.
    class Cursor
    {
        public int zone;
        public EventSystem es;
        public Gamepad pad;
        public GameObject lastSelected;
        public GameObject navSelected; // the selection FixNavigation last saw
        public bool restoreAfterBack;  // B closed a panel: back onto the item that opened it
        public bool bHeld;             // B down at the last look (one press acts once)
        public float lastBackTime = -1f;
        public bool inPalette;         // the selection in an open colour palette (last look)
        public readonly List<GameObject> history = new List<GameObject>(); // recent selections, newest last
        public float nextNavigationFix;
        public Transform front;
        public readonly SelectedBoxLook look = new SelectedBoxLook();
        public bool lookOn;            // the selected box shown selected (LateUpdate)

        public void Reset()
        {
            look.Restore();
            lookOn = false;
            es = null;
            lastSelected = null;
            navSelected = null;
            restoreAfterBack = false;
            inPalette = false;
            history.Clear();
            front = null;
            nextNavigationFix = 0f;
        }
    }

    readonly Cursor[] cursors = new Cursor[LocalPlay.MaxPlayers];
    static int s_zone;    // the cursor being kept
    static bool s_multi;  // a cursor per player (the local room)

    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
    static void Create()
    {
        if (s_instance)
            return;
        var go = new GameObject("ControllerMenuSupport");
        DontDestroyOnLoad(go);
        s_instance = go.AddComponent<ControllerMenuSupport>();
        go.AddComponent<TextFieldGuard>();
        // a new scene's menus: their own links again
        // MainMenu is the first scene: loaded before this exists
        if (UnityEngine.SceneManagement.SceneManager.GetActiveScene().name == "MainMenu")
        {
            s_checkedArgs = true;
            CheckPersistentInt();
        }
        UnityEngine.SceneManagement.SceneManager.sceneLoaded += (scene, mode) =>
        {
            s_original.Clear();
            if (!s_checkedArgs && scene.name == "MainMenu")
            {
                s_checkedArgs = true;
                CheckPersistentInt();
            }
        };
    }

    void Update()
    {
        LocalCursors.Update();
        s_multi = LocalCursors.Active;
        var anyPad = Gamepad.current;
        bool padUsed = anyPad != null && AnyPadInput(anyPad);
        if (padUsed)
            controllerMode = true;
        else if (Touchscreen.current != null && Touchscreen.current.primaryTouch.press.wasPressedThisFrame)
            controllerMode = false;

        int count = s_multi ? LocalCursors.Players : 1;
        for (int z = 0; z < cursors.Length; z++)
        {
            Cursor c = cursors[z] ?? (cursors[z] = new Cursor { zone = z });
            EventSystem es = z == 0 ? EventSystem.current : z < count ? LocalCursors.SystemOf(z) : null;
            if (!es)
            {
                if (c.es)
                    c.Reset();
                continue;
            }
            if (c.es != es)
            {
                c.Reset(); // a new scene's, or a player's new one
                c.es = es;
            }
            c.pad = s_multi ? LocalCursors.PadOf(z) : anyPad;
            s_zone = z;
            s_front = c.front;
            Keep(c);
            c.front = s_front;
        }
        s_zone = 0;
        s_front = cursors[0] != null ? cursors[0].front : null;
        if (controllerMode)
            EaseScroll();
    }

    // The selected box's look, once every event system has moved its
    // selection this frame: players 2-4's (MultiplayerEventSystems made at
    // run time) update after this script, so a box they moved onto showed
    // its own white "selected" sprite (white text on white) for one frame
    // before the gray one: a flicker going from Colors up to Character.
    // In the local room each player's selection is drawn in their colour.
    void LateUpdate()
    {
        foreach (Cursor c in cursors)
        {
            if (c == null || !c.es)
                continue;
            c.look.tint = s_multi ? LocalPlay.GlowColor(c.zone) : Color.white;
            c.look.Show(c.lookOn ? c.es.currentSelectedGameObject : null);
        }
    }

    void Keep(Cursor c)
    {
        EventSystem es = c.es;
        bool controller = controllerMode || c.zone > 0; // players 2-4 only have a controller
        // B: a press acts once -- this cursor's own edge (not down at the last
        // look), and never twice within 80 ms, whatever the Input System
        // reports for the frame (a guard; the palette's double back below
        // was its own Cancel)
        bool bPressed = c.pad != null && c.pad.buttonEast.wasPressedThisFrame && !c.bHeld &&
                        Time.unscaledTime - c.lastBackTime > 0.08f;
        c.bHeld = c.pad != null && c.pad.buttonEast.isPressed;
        // the palette's own Cancel: MMIM's colour buttons close their palette
        // on Cancel (an EventTrigger, Cancel -> ColorChooser.Close), and the
        // event system runs before this, so on B the selection is already
        // back on Colors: that B is used (it went on to leave the room)
        bool wasInPalette = c.inPalette;
        c.inPalette = OpenChooser(es.currentSelectedGameObject, out _) != null;
        if (c.zone > 0)
        {
            // a player's box under a panel player 1 opened (the maps,
            // Settings): that player waits until it closes
            GameObject box = LocalPlayMenu.BoxOf(c.zone);
            bool covered = !box || !box.activeInHierarchy || Covered(es, box, true);
            if (es.sendNavigationEvents == covered)
                es.sendNavigationEvents = !covered;
            if (covered)
            {
                c.lookOn = false;
                return;
            }
        }
        c.lookOn = controller;

        Transform front = FrontPanel();
        if (front != c.front)
        {
            Canvas.ForceUpdateCanvases(); // a popup just opened: its lists laid out before links are made
            s_covered.Clear();           // and what covers what is asked again
        }
        if (front != c.front || es.currentSelectedGameObject != c.navSelected)
            c.nextNavigationFix = 0f; // a popup opened or closed, a menu changed
        s_front = c.front = front;
        c.navSelected = es.currentSelectedGameObject;
        if (Time.unscaledTime >= c.nextNavigationFix)
        {
            c.nextNavigationFix = Time.unscaledTime + 0.5f; // (and at once when a panel or the selection changes)
            FixNavigation(es);
        }

        // + in a room starts the game, as Start Game (the user, 2026-11-16:
        // the Local Play room, then the online lobby too; - and ZL+ZR in the
        // Local Play room open the controller screen: the loader, gamepad.c):
        // any player with nothing open over the room (the panel in front is
        // the one with Start Game: not the maps, Settings or a list) and no
        // palette open; online only the host (Start Game is not interactable
        // for the others: "not available" in the log)
        Button startButton = MainMenuManager.Instance ? MainMenuManager.Instance.startGameBtn : null;
        if (startButton && startButton.gameObject.activeInHierarchy && c.pad != null && c.pad.startButton.wasPressedThisFrame)
        {
            bool roomInFront = front && startButton && startButton.transform.IsChildOf(front);
            GameObject plusOn = es.currentSelectedGameObject;
            bool typing = plusOn && plusOn.GetComponent<TMPro.TMP_InputField>(); // the lobby's chat box
            if (roomInFront && !c.inPalette && !InOpenList(plusOn) && !typing)
                StartFromPlus(c.zone);
            else
                Debug.LogWarning("[menu] + from player " + (c.zone + 1) + " ignored: " +
                                 (!roomInFront ? "a panel is open (" + (front ? front.name : "none") + ")" : typing ? "the chat box is selected" : "a list or palette is open"));
        }

        Gamepad pad = c.pad;
        if (c.zone == 0 && pad != null && TitleShown() && AnyButtonPressed(pad) && !Covered(es, MainMenuManager.Instance.title))
        {
            Submit(es, MainMenuManager.Instance.title);
            return;
        }

        if (bPressed)
        {
            c.lastBackTime = Time.unscaledTime;
            Back(c, wasInPalette);
            if (c.restoreAfterBack)
                return; // the menu's own default selection stays out of the history
        }

        if (!controller)
            return;

        if (pad != null && c.zone == 0)
            ScrollText(pad);

        GameObject selected = es.currentSelectedGameObject;
        if (c.restoreAfterBack)
        {
            // MMIM's menus select their own default when one opens (the Main
            // Menu its top button); after B the cursor goes back to the item
            // last used in the menu now in front, as a console game does
            // (Credits -> "About", the note -> "Guide", Controls -> "Controls")
            c.restoreAfterBack = false;
            for (int i = c.history.Count - 1; i >= 0; i--)
                if (Usable(c.history[i]) && InFront(c.history[i]) && InZone(c.history[i].transform) && !Covered(es, c.history[i]))
                {
                    if (c.history[i] != selected)
                        es.SetSelectedGameObject(c.history[i]);
                    break;
                }
            selected = es.currentSelectedGameObject;
        }
        if (!Usable(selected) || selected.GetComponent<Scrollbar>() || !InFront(selected) || !InZone(selected.transform) ||
            Covered(es, selected))
        {
            // the choice in use (a panel of maps just opened), else back where
            // it was before (a popup closed), else the first item
            GameObject pick = CurrentChoice(es);
            for (int i = c.history.Count - 1; i >= 0 && !pick; i--)
                if (Usable(c.history[i]) && !c.history[i].GetComponent<Scrollbar>() && InFront(c.history[i]) &&
                    InZone(c.history[i].transform) && !Covered(es, c.history[i]))
                    pick = c.history[i];
            if (!pick)
                pick = FirstItem(es, c.lastSelected ? c.lastSelected : selected);
            if (pick)
                es.SetSelectedGameObject(pick);
            selected = es.currentSelectedGameObject;
        }
        if (selected && selected != c.lastSelected)
        {
            c.lastSelected = selected;
            c.history.Remove(selected);
            c.history.Add(selected);
            if (c.history.Count > 16)
                c.history.RemoveAt(0);
            ScrollIntoView(selected);
            if (s_logged++ < 300) // the path through the menus, in the Switch's debug.log
            {
                Selectable sel = selected.GetComponent<Selectable>();
                Navigation n = sel ? sel.navigation : default;
                Debug.LogWarning("[menu] " + (c.zone > 0 ? "P" + (c.zone + 1) + " " : "") + "selected " + PathOf(selected.transform) +
                                 (sel ? " (down " + LinkName(n.selectOnDown) + ", up " + LinkName(n.selectOnUp) + ")" : ""));
            }
            DumpPanel(es, selected);
        }
    }

    // in the local room: whether an object is this cursor's to use
    static bool InZone(Transform t)
    {
        return !s_multi || LocalCursors.ZoneOf(t) == s_zone;
    }

    // a player's box is covered only by the room's own panels (the maps,
    // Settings player 1 opened), not by any player's box, list, palette or
    // the blockers those put over the screen (its own palette's blocker
    // froze player 2 once the palette opened)
    static bool NotRoom(Transform t)
    {
        if (!s_multi)
            return false;
        return LocalCursors.ZoneOf(t, out bool claimed) != 0 || claimed;
    }

    // another player's box or what it opened (their list, their palette and
    // its blocker): never in front of anything of this cursor's
    static bool OtherZone(Transform t)
    {
        if (!s_multi)
            return false;
        int z = LocalCursors.ZoneOf(t, out bool claimed);
        return claimed ? z != s_zone : z != 0 && z != s_zone;
    }

    // --- the selectables, once a frame -----------------------------------------------------
    // Selectable.allSelectablesArray copies every enabled selectable into a
    // new array each call; the cursors scanned it several times a frame each
    // (4 in the local room: it ran at 43-53 fps on the console). One buffer a
    // frame, again when the count changes (a popup opened in this frame).
    static Selectable[] s_sel = new Selectable[256];
    static int s_selCount, s_selFrame = -1, s_selTotal = -1;
    static readonly List<Selectable> s_selList = new List<Selectable>(256);

    public static List<Selectable> Selectables()
    {
        int total = Selectable.allSelectableCount;
        if (s_selFrame != Time.frameCount || total != s_selTotal)
        {
            s_selFrame = Time.frameCount;
            s_selTotal = total;
            if (s_sel.Length < total)
                s_sel = new Selectable[total * 2];
            s_selCount = Selectable.AllSelectablesNoAlloc(s_sel);
            s_selList.Clear();
            for (int i = 0; i < s_selCount; i++)
                if (s_sel[i])
                    s_selList.Add(s_sel[i]);
        }
        return s_selList;
    }

    static readonly HashSet<long> s_dumped = new HashSet<long>(); // a panel, as one cursor saw it

    // the local room's cursors began: each panel's links in debug.log again,
    // as each cursor sees them
    public static void RedumpPanels()
    {
        s_dumped.Clear();
    }

    // the first time a panel gets the selection: its visible items and where
    // each leads (debug.log), to see how a menu is wired
    static void DumpPanel(EventSystem es, GameObject selected)
    {
        Canvas canvas = selected.GetComponentInParent<Canvas>();
        Transform panel = canvas ? Panel(selected.transform, canvas) : null;
        if (!panel || s_dumped.Count > 24 || !s_dumped.Add((long)panel.GetInstanceID() * 8 + s_zone))
            return;
        FixNavigation(es);
        var sb = new System.Text.StringBuilder("[menu] panel " + panel.name + (s_multi ? " (P" + (s_zone + 1) + "'s)" : "") + ":");
        foreach (Selectable s in s_visible)
        {
            if (!s.transform.IsChildOf(panel))
                continue;
            Navigation n = s.navigation;
            sb.Append("\n  ").Append(LinkName(s)).Append(" (").Append(s.GetType().Name).Append(") up ")
              .Append(LinkName(n.selectOnUp)).Append(" down ")
              .Append(LinkName(n.selectOnDown)).Append(" left ")
              .Append(LinkName(n.selectOnLeft)).Append(" right ")
              .Append(LinkName(n.selectOnRight));
        }
        int hidden = 0;
        foreach (Selectable s in Selectables())
            if (s.transform.IsChildOf(panel) && !s_visible.Contains(s))
                hidden++;
        sb.Append("\n  (").Append(hidden).Append(" more not visible)");
        Debug.LogWarning(sb.ToString());
    }

    static string PathOf(Transform t)
    {
        string path = NameOf(t);
        for (Transform p = t.parent; p && path.Length < 120; p = p.parent)
            path = NameOf(p) + "/" + path;
        return path;
    }

    // a name, with its place among its siblings when another has the same
    // (the maps list has a Row1 per section)
    static string NameOf(Transform t)
    {
        if (!t)
            return "-";
        Transform parent = t.parent;
        if (parent)
            for (int i = 0; i < parent.childCount; i++)
            {
                Transform other = parent.GetChild(i);
                if (other != t && other.name == t.name)
                    return t.name + "#" + t.GetSiblingIndex();
            }
        return t.name;
    }

    static string LinkName(Selectable s)
    {
        return s ? (s.transform.parent ? NameOf(s.transform.parent) + "/" : "") + NameOf(s.transform) : "-";
    }

    // --- + in the Local Play room ------------------------------------------------------

    // a dropdown's open list (TMP_Dropdown's "Dropdown List")
    static bool InOpenList(GameObject selected)
    {
        for (Transform t = selected ? selected.transform : null; t; t = t.parent)
            if (t.name == "Dropdown List")
                return true;
        return false;
    }

    static float s_plusStartedAt = -10f;

    static void StartFromPlus(int zone)
    {
        if (Time.unscaledTime - s_plusStartedAt < 1f)
            return; // two players' + in the same moment: one start
        MainMenuManager mm = MainMenuManager.Instance;
        Button start = mm ? mm.startGameBtn : null;
        if (!start || !start.gameObject.activeInHierarchy || !start.interactable)
        {
            Debug.LogWarning("[menu] + from player " + (zone + 1) + ": Start Game is not available");
            return;
        }
        Debug.LogWarning("[menu] + from player " + (zone + 1) + ": Start Game");
        s_plusStartedAt = Time.unscaledTime;
        start.onClick.Invoke();
    }

    // --- input ------------------------------------------------------------------------

    static bool AnyButtonPressed(Gamepad pad)
    {
        return pad.buttonSouth.wasPressedThisFrame || pad.buttonEast.wasPressedThisFrame ||
               pad.buttonWest.wasPressedThisFrame || pad.buttonNorth.wasPressedThisFrame ||
               pad.leftShoulder.wasPressedThisFrame || pad.rightShoulder.wasPressedThisFrame ||
               pad.leftTrigger.wasPressedThisFrame || pad.rightTrigger.wasPressedThisFrame ||
               pad.startButton.wasPressedThisFrame || pad.selectButton.wasPressedThisFrame;
    }

    static bool AnyPadInput(Gamepad pad)
    {
        return AnyButtonPressed(pad) || pad.dpad.ReadValue().sqrMagnitude > 0.25f ||
               pad.leftStick.ReadValue().sqrMagnitude > 0.25f;
    }

    static bool TitleShown()
    {
        var menu = MainMenuManager.Instance;
        return menu && menu.title && menu.title.activeInHierarchy;
    }

    static void Submit(EventSystem es, GameObject target)
    {
        es.SetSelectedGameObject(target);
        ExecuteEvents.Execute(target, new BaseEventData(es), ExecuteEvents.submitHandler);
    }

    // --- B: back ----------------------------------------------------------------------

    void Back(Cursor c, bool wasInPalette)
    {
        EventSystem es = c.es;
        // a colour palette open: it closes, back on its colour button
        GameObject on = es.currentSelectedGameObject;
        ColorChooser palette = OpenChooser(on, out string how);
        if (s_logged++ < 300) // what B did, for HW logs
            Debug.LogWarning("[menu] " + (c.zone > 0 ? "P" + (c.zone + 1) + " " : "") + "B at frame " + Time.frameCount + " on " +
                             (on ? PathOf(on.transform) : "-") + (palette ? ": closes " + PathOf(palette.transform) + "'s palette (" + how + ")" : ""));
        if (palette)
        {
            LocalCursors.Run(es, palette.Close);
            return;
        }
        if (wasInPalette)
        {
            if (s_logged++ < 300)
                Debug.LogWarning("[menu] B: the palette closed itself (its own Cancel)");
            return;
        }
        if (on && on.transform.parent && on.transform.parent.name == "Palette" && on.transform.parent.parent)
            foreach (ColorChooser ch in on.transform.parent.parent.GetComponentsInChildren<ColorChooser>(true))
                Debug.LogWarning("[menu] B in a palette, not closed: " + PathOf(ch.transform) + " enabled " + ch.enabled +
                                 ", open " + ch.IsOpen + ", palette " + (ch.Palette ? PathOf(ch.Palette) : "-") +
                                 ", listed " + ColorChooser.All.Contains(ch) + " of " + ColorChooser.All.Count);
        // players 2-4: an open list closes itself (its own Cancel); leaving
        // the room is player 1's
        if (c.zone > 0)
            return;

        // a paused match: the quit prompt, else the pause menu itself
        var game = GameManager.Instance;
        if (game)
        {
            if (game.paused)
            {
                if (game.optionsBox && game.optionsBox.activeInHierarchy)
                    game.OptionsCancel(); // Settings: back to the pause menu
                else if (game.hostExitUI && game.hostExitUI.activeInHierarchy)
                    game.HostQuitCancel();
                else
                    game.Pause();
            }
            return; // in a match B is the players'
        }

        var mm = MainMenuManager.Instance;
        // rebinding a control: B may be the button being chosen
        if (mm && mm.rebindPrompt && mm.rebindPrompt.activeInHierarchy)
            return;
        // an error box closes first (its one button is "ok.")
        if (mm && mm.errorBox && mm.errorBox.activeInHierarchy && mm.errorButton)
        {
            Submit(es, mm.errorButton);
            return;
        }

        Button back = BackButton(es.currentSelectedGameObject);
        if (back)
        {
            if (s_logged++ < 300)
                Debug.LogWarning("[menu] B: " + PathOf(back.transform));
            back.onClick.Invoke();
            c.restoreAfterBack = true; // next frame, once the panels have changed
            return;
        }
        if (mm && mm.mainMenu && mm.mainMenu.activeInHierarchy)
            mm.OpenTitleScreen();
    }

    // the open colour palette the selection is in, or null; how: which way
    // it was found (for the log)
    static ColorChooser OpenChooser(GameObject selected, out string how)
    {
        how = "";
        if (!selected)
            return null;
        foreach (ColorChooser chooser in ColorChooser.All)
            if (chooser && chooser.IsOpen && chooser.Palette && selected.transform.IsChildOf(chooser.Palette))
            {
                how = "listed";
                return chooser;
            }
        // the chooser beside the palette the selection is in, in case it is
        // not in ColorChooser.All (a colour button is the palette's child; this
        // runs every frame, so only for an item of something named Palette)
        Transform palette = selected.transform.parent;
        if (palette && palette.name == "Palette" && palette.parent)
            foreach (ColorChooser chooser in palette.parent.GetComponentsInChildren<ColorChooser>(true))
                if (chooser && chooser.Palette == palette && chooser.IsOpen)
                {
                    how = "found beside it, not listed (" + ColorChooser.All.Count + " listed)";
                    return chooser;
                }
        return null;
    }

    // the front panel's Back button, nearest the selection when it has more
    // than one (a prompt's own before the panel's under it)
    static Button BackButton(GameObject near)
    {
        Button best = null;
        int bestScore = int.MaxValue;
        foreach (Selectable s in Selectables())
        {
            if (!(s is Button b) || !b.IsInteractable() || !b.gameObject.activeInHierarchy || !PlaysBackSound(b) ||
                !InFront(b.gameObject) || !InZone(b.transform))
                continue;
            int score = Distance(b.transform, near ? near.transform : null);
            if (score < bestScore)
            {
                bestScore = score;
                best = b;
            }
        }
        return best;
    }

    static bool PlaysBackSound(Button b)
    {
        var click = b.onClick;
        for (int i = 0; i < click.GetPersistentEventCount(); i++)
            if (click.GetPersistentMethodName(i) == "BackSound")
                return true;
        return false;
    }

    // steps from a up to an ancestor of b, plus that ancestor's depth under b
    // (a large number when unrelated)
    static int Distance(Transform a, Transform b)
    {
        if (!b)
            return 1000;
        var chain = new List<Transform>();
        for (Transform t = b; t; t = t.parent)
            chain.Add(t);
        int steps = 0;
        for (Transform t = a; t; t = t.parent, steps++)
        {
            int k = chain.IndexOf(t);
            if (k >= 0)
                return steps + k;
        }
        return 1000;
    }

    // --- keeping a selection ------------------------------------------------------------

    static bool Usable(GameObject go)
    {
        if (!go || !go.activeInHierarchy)
            return false;
        var s = go.GetComponent<Selectable>();
        return !s || s.IsInteractable();
    }

    static readonly List<RaycastResult> s_hits = new List<RaycastResult>();
    static readonly Vector3[] s_cornersA = new Vector3[4], s_cornersB = new Vector3[4], s_cornersC = new Vector3[4];
    static PointerEventData s_ped;
    static EventSystem s_pedSystem;

    // whether something else (a panel opened over it) is in front of go at
    // its centre, as the pointer would find it
    // what covers what, for a few frames: the cursors asked every frame (the
    // selection, a player's box) and the links every half second (each item),
    // each a raycast through every graphic of the canvas
    static readonly Dictionary<long, KeyValuePair<int, bool>> s_covered = new Dictionary<long, KeyValuePair<int, bool>>();
    const int CoveredFrames = 3;

    static bool Covered(EventSystem es, GameObject go, bool byRoomOnly = false)
    {
        if (!go)
            return false;
        long key = ((long)go.GetInstanceID() << 4) | ((long)s_zone << 1) | (byRoomOnly ? 1L : 0L);
        if (s_covered.TryGetValue(key, out KeyValuePair<int, bool> kept) && Time.frameCount - kept.Key < CoveredFrames)
            return kept.Value;
        if (s_covered.Count > 512)
            s_covered.Clear();
        bool covered = CoveredNow(es, go, byRoomOnly);
        s_covered[key] = new KeyValuePair<int, bool>(Time.frameCount, covered);
        return covered;
    }

    static bool CoveredNow(EventSystem es, GameObject go, bool byRoomOnly)
    {
        var rt = go.transform as RectTransform;
        Canvas canvas = go.GetComponentInParent<Canvas>();
        if (!rt || !canvas)
            return false;
        Vector3[] c = s_cornersA;
        rt.GetWorldCorners(c);
        // an item of a scroll list outside the list's view now: whatever is
        // over that spot (the maps' "Current Map" bar) says nothing; it is
        // scrolled into view when selected (the second row of each section
        // of maps was skipped while under that bar)
        ScrollRect list = go.GetComponentInParent<ScrollRect>();
        if (list && list.viewport && list.content && go.transform.IsChildOf(list.content))
        {
            Vector3 mid = (c[0] + c[2]) * 0.5f;
            Vector3[] v = s_cornersB;
            list.viewport.GetWorldCorners(v);
            if (mid.x < v[0].x || mid.x > v[2].x || mid.y < v[0].y || mid.y > v[2].y)
                return false;
        }
        Camera cam = canvas.renderMode == RenderMode.ScreenSpaceOverlay ? null : canvas.worldCamera;
        Vector2 centre = RectTransformUtility.WorldToScreenPoint(cam, (c[0] + c[2]) * 0.5f);
        if (s_ped == null || s_pedSystem != es)
        {
            s_ped = new PointerEventData(es);
            s_pedSystem = es;
        }
        s_ped.Reset();
        s_ped.position = centre;
        var ped = s_ped;
        s_hits.Clear();
        es.RaycastAll(ped, s_hits);
        int at = 0;
        while (at < s_hits.Count && (byRoomOnly ? NotRoom(s_hits[at].gameObject.transform) : OtherZone(s_hits[at].gameObject.transform)))
            at++; // another player's box (or their open list) covers nothing of this cursor's
        if (at >= s_hits.Count)
            return false; // nothing that takes the pointer there: not covered
        Transform first = s_hits[at].gameObject.transform;
        Transform panel = Panel(go.transform, canvas);
        // its own parts, or anything of its own panel (a label over a slider)
        // do not cover it; another panel opened over it (the error box, the
        // Settings box over the Main Menu) does
        if (first == go.transform || first.IsChildOf(go.transform) || go.transform.IsChildOf(first) ||
            (panel && first.IsChildOf(panel)))
            return false;
        // ...but only one drawn in front of it: a popup still zooming in
        // (its Animator scales it up from nothing) lets the pointer through
        // to the menu behind, which covers nothing
        Canvas hitCanvas = first.GetComponentInParent<Canvas>();
        if (!hitCanvas || !panel)
            return true;
        Canvas root = canvas.rootCanvas, hitRoot = hitCanvas.rootCanvas;
        if (hitRoot != root)
            return hitRoot.sortingOrder >= root.sortingOrder;
        Transform hitPanel = Panel(first, hitCanvas);
        return !hitPanel || hitPanel.GetSiblingIndex() > panel.GetSiblingIndex();
    }

    // the panel an item belongs to: its ancestor right under the root canvas
    // (MMIM's menus and popups are each a child of the canvas)
    static Transform Panel(Transform t, Canvas canvas)
    {
        Transform root = canvas.rootCanvas.transform;
        Transform panel = null;
        for (Transform p = t; p && p != root; p = p.parent)
            panel = p;
        return panel;
    }

    // The panel in front: of the canvas drawn last (highest sorting order) that
    // has an item to select, its last active child that has one -- MMIM opens
    // each popup as a later sibling of the menus under it (StoneBG, then
    // RemindBox1, ErrorBox, OptionsBox, ...; the pause menu, then Settings in
    // a match). Null when nothing can be selected (a match being played).
    static Transform s_front;

    // (every cursor's at once, once a frame: each cursor scanned every
    // selectable for its own every frame)
    static int s_frontFrame = -1, s_frontTotal = -1;
    static bool s_frontMulti;
    static readonly Transform[] s_frontOf = new Transform[LocalPlay.MaxPlayers];
    static readonly Canvas[] s_frontRoot = new Canvas[LocalPlay.MaxPlayers];
    static readonly int[] s_frontOrder = new int[LocalPlay.MaxPlayers], s_frontIndex = new int[LocalPlay.MaxPlayers];

    static Transform FrontPanel()
    {
        if (s_frontFrame != Time.frameCount || s_frontTotal != Selectable.allSelectableCount || s_frontMulti != s_multi)
        {
            s_frontFrame = Time.frameCount;
            s_frontTotal = Selectable.allSelectableCount;
            s_frontMulti = s_multi;
            FindFrontPanels();
        }
        return s_frontOf[s_multi ? Mathf.Clamp(s_zone, 0, LocalPlay.MaxPlayers - 1) : 0];
    }

    static void FindFrontPanels()
    {
        for (int z = 0; z < s_frontOf.Length; z++)
        {
            s_frontOf[z] = null;
            s_frontRoot[z] = null;
            s_frontOrder[z] = int.MinValue;
            s_frontIndex[z] = -1;
        }
        foreach (Selectable s in Selectables())
        {
            if (!s || !s.IsInteractable() || !s.gameObject.activeInHierarchy || s is Scrollbar)
                continue;
            int z = s_multi ? Mathf.Clamp(LocalCursors.ZoneOf(s.transform), 0, LocalPlay.MaxPlayers - 1) : 0;
            if (ClippedOut(s))
                continue;
            Canvas canvas = s.GetComponentInParent<Canvas>();
            if (!canvas || !canvas.isActiveAndEnabled)
                continue;
            Canvas root = canvas.rootCanvas;
            Transform layer = Layer(s.transform, root, out int order);
            if (!layer)
                continue;
            int index = order == int.MinValue ? layer.GetSiblingIndex() : 0;
            Canvas bestRoot = s_frontRoot[z];
            bool better = !s_frontOf[z] || root.sortingOrder > bestRoot.sortingOrder ||
                          (root.sortingOrder == bestRoot.sortingOrder &&
                           (order > s_frontOrder[z] || (order == s_frontOrder[z] && root == bestRoot && index > s_frontIndex[z])));
            if (better)
            {
                s_frontRoot[z] = root;
                s_frontOf[z] = layer;
                s_frontOrder[z] = order;
                s_frontIndex[z] = index;
            }
        }
    }

    // What an item draws in: a nested canvas with its own sorting order (a
    // dropdown's open list at 30000, its full-screen blocker at 29999, the
    // player list's Mute/Kick menu) -- that canvas, with its order -- or else
    // its panel under the root canvas (order int.MinValue).
    static Transform Layer(Transform t, Canvas root, out int order)
    {
        for (Transform p = t; p && p != root.transform; p = p.parent)
        {
            Canvas c = p.GetComponent<Canvas>();
            if (c && c.enabled && c.overrideSorting)
            {
                order = c.sortingOrder;
                return p;
            }
        }
        order = int.MinValue;
        return Panel(t, root);
    }

    static readonly Vector3[] s_corners = new Vector3[4];

    // whether a mask that is not a scroll list's viewport hides the item (the
    // player list's Mute/Kick menu stays active, slid out of its mask); in a
    // scroll list only a mask inside its content counts
    static bool ClippedOut(Selectable s)
    {
        Vector3 c = Centre(s);
        for (Transform t = s.transform.parent; t; t = t.parent)
        {
            // an item of a scroll list is scrolled to when selected, whatever
            // hides it now: the maps below the second row were outside the
            // maps box's mask (its frame), so the cursor stopped there
            ScrollRect list = t.GetComponent<ScrollRect>();
            if (list && list.content && s.transform.IsChildOf(list.content))
                return false;
            if (!t.GetComponent<RectMask2D>() && !t.GetComponent<Mask>())
                continue;
            if (list)
                continue;
            ScrollRect owner = t.parent ? t.parent.GetComponent<ScrollRect>() : null;
            if (owner && owner.viewport == t)
                continue; // a scroll list: off-screen items are scrolled to
            var rt = t as RectTransform;
            if (!rt)
                continue;
            rt.GetWorldCorners(s_corners);
            if (c.x < s_corners[0].x || c.x > s_corners[2].x || c.y < s_corners[0].y || c.y > s_corners[2].y)
                return true;
        }
        return false;
    }

    static bool InFront(GameObject go)
    {
        return !s_front || (go && go.transform.IsChildOf(s_front));
    }

    // the front panel's button for the choice in use, or null: the maps
    // (buttons calling MainMenuManager.SetLevelIndex(n)) open on the room's
    // map, as a console game's list opens on what is chosen
    static GameObject CurrentChoice(EventSystem es)
    {
        if (!PhotonNetwork.InRoom || !Utils.GetCustomProperty(Enums.NetRoomProperties.Level, out int level))
            return null;
        foreach (Selectable s in Selectables())
        {
            if (!(s is Button b) || !b.IsInteractable() || !b.gameObject.activeInHierarchy || !InFront(b.gameObject) ||
                !InZone(b.transform) || Covered(es, b.gameObject))
                continue;
            if (PersistentInt(b.onClick, "SetLevelIndex", out int arg) && arg == level)
                return b.gameObject;
        }
        return null;
    }

    // the int a button's click passes to a method set in the scene (Unity
    // keeps it in UnityEventBase.m_PersistentCalls, not public)
    static readonly FieldInfo s_persistentCalls =
        typeof(UnityEventBase).GetField("m_PersistentCalls", BindingFlags.Instance | BindingFlags.NonPublic);

    static object Field(object o, string name)
    {
        return o?.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public)?.GetValue(o);
    }

    static bool PersistentInt(UnityEventBase ev, string method, out int value)
    {
        value = 0;
        for (int i = 0; i < ev.GetPersistentEventCount(); i++)
        {
            if (ev.GetPersistentMethodName(i) != method)
                continue;
            try
            {
                var calls = Field(s_persistentCalls?.GetValue(ev), "m_Calls") as IList;
                if (calls != null && i < calls.Count && Field(Field(calls[i], "m_Arguments"), "m_IntArgument") is int v)
                {
                    value = v;
                    return true;
                }
            }
            catch (System.Exception e)
            {
                Debug.LogWarning("[menu] a button's argument: " + e.Message);
            }
            return false;
        }
        return false;
    }

    // the maps box's button for a map (its click: SetLevelIndex(level)), or null
    public static Button MapButton(GameObject mapsBox, int level)
    {
        if (!mapsBox)
            return null;
        foreach (Button b in mapsBox.GetComponentsInChildren<Button>(true))
            if (PersistentInt(b.onClick, "SetLevelIndex", out int arg) && arg == level)
                return b;
        return null;
    }

    // once, at start (debug.log): whether the maps' level numbers can be read
    static void CheckPersistentInt()
    {
        foreach (Button b in Resources.FindObjectsOfTypeAll<Button>())
            if (PersistentInt(b.onClick, "SetLevelIndex", out int arg))
            {
                Debug.LogWarning("[menu] " + PathOf(b.transform) + " sets map " + arg);
                return;
            }
        Debug.LogWarning("[menu] no map button's level read");
    }

    // the top-left interactable item of the front panel, nearest the
    // previous selection when it is in that panel too
    static GameObject FirstItem(EventSystem es, GameObject near)
    {
        Selectable best = null;
        int bestOrder = int.MinValue, bestDistance = int.MaxValue;
        Vector2 bestPos = default;
        foreach (Selectable s in Selectables())
        {
            if (!s.IsInteractable() || !s.gameObject.activeInHierarchy || s is Scrollbar)
                continue;
            if (s is Button b && PlaysBackSound(b))
                continue; // a panel opens on its first item, not on Back
            if (!InFront(s.gameObject) || !InZone(s.transform) || !Sized(s) || ClippedOut(s) || Covered(es, s.gameObject))
                continue; // under a panel opened over it, or hidden by a mask (or another player's, or nothing to see)
            Canvas canvas = s.GetComponentInParent<Canvas>();
            if (!canvas || !canvas.isActiveAndEnabled)
                continue;
            int order = canvas.rootCanvas.sortingOrder;
            // nearest the previous selection only within its own panel (from
            // another popup, the hierarchy says nothing about the screen)
            int distance = near && InFront(near) ? Distance(s.transform, near.transform) : 0;
            Vector2 pos = ScreenPos(s.transform, canvas);
            bool better = best == null || order > bestOrder ||
                          (order == bestOrder && distance < bestDistance) ||
                          (order == bestOrder && distance == bestDistance &&
                           (pos.y > bestPos.y + 4f || (Mathf.Abs(pos.y - bestPos.y) <= 4f && pos.x < bestPos.x)));
            if (better)
            {
                best = s;
                bestOrder = order;
                bestDistance = distance;
                bestPos = pos;
            }
        }
        if (best)
            return best.gameObject;
        // items of the front panel not uncovered yet: wait for them
        foreach (Selectable s in Selectables())
            if (s.IsInteractable() && s.gameObject.activeInHierarchy && !(s is Scrollbar) && InFront(s.gameObject) &&
                InZone(s.transform) && !(s is Button b2 && PlaysBackSound(b2)))
                return null;
        // nothing else in the panel (the note): its Back button
        Button back = BackButton(near);
        return back ? back.gameObject : null;
    }

    static Vector2 ScreenPos(Transform t, Canvas canvas)
    {
        Camera cam = canvas.renderMode == RenderMode.ScreenSpaceOverlay ? null : canvas.worldCamera;
        return RectTransformUtility.WorldToScreenPoint(cam, t.position);
    }

    // Every visible item (interactable, not covered by a panel opened over it)
    // gets a way out in every direction: a designed (Explicit) link is kept
    // while it points at a visible item; an empty or hidden one -- and an
    // automatic one, which would also find items under the open panel --
    // becomes the nearest visible item that way, scored as Unity's automatic
    // navigation scores it (Selectable.FindSelectable).
    static readonly List<Selectable> s_visible = new List<Selectable>();
    static readonly Dictionary<Selectable, Navigation> s_original = new Dictionary<Selectable, Navigation>();

    static void FixNavigation(EventSystem es)
    {
        s_visible.Clear();
        foreach (Selectable s in Selectables())
            if (s.IsInteractable() && s.gameObject.activeInHierarchy && !(s is Scrollbar) && InFront(s.gameObject) &&
                InZone(s.transform) && Sized(s) && !ClippedOut(s) && !Covered(es, s.gameObject))
                s_visible.Add(s); // (in the local room: this cursor's own, linked among themselves)
        // up and down scroll a text list instead (the credits), when the
        // panel's items are all on one row and could not use them anyway
        s_textScroll = OneRow(s_visible) ? TextScroll() : null;
        bool textScroll = s_textScroll != null;
        foreach (Selectable s in s_visible)
        {
            // the scene's own links, not the ones made here last time (made
            // while a list was still being laid out they would stick)
            if (!s_original.TryGetValue(s, out Navigation nav))
                nav = s.navigation;
            bool designed = nav.mode == Navigation.Mode.Explicit;
            Selectable up = textScroll ? null :
                designed && s_visible.Contains(nav.selectOnUp) ? nav.selectOnUp : Nearest(s, Vector3.up);
            Selectable down = textScroll ? null :
                designed && s_visible.Contains(nav.selectOnDown) ? nav.selectOnDown : Nearest(s, Vector3.down);
            Selectable left = designed && s_visible.Contains(nav.selectOnLeft) ? nav.selectOnLeft : Nearest(s, Vector3.left);
            Selectable right = designed && s_visible.Contains(nav.selectOnRight) ? nav.selectOnRight : Nearest(s, Vector3.right);
            // a slider moves its own handle along its axis (Slider.OnMove only
            // does so while there is nothing to go to that way)
            if (s is Slider slider)
            {
                if (slider.direction == Slider.Direction.LeftToRight || slider.direction == Slider.Direction.RightToLeft)
                    left = right = null;
                else
                    up = down = null;
            }
            Navigation now = s.navigation;
            if (now.mode == Navigation.Mode.Explicit && up == now.selectOnUp && down == now.selectOnDown &&
                left == now.selectOnLeft && right == now.selectOnRight)
                continue;
            if (!s_original.ContainsKey(s))
                s_original[s] = nav;
            nav.mode = Navigation.Mode.Explicit;
            nav.selectOnUp = up;
            nav.selectOnDown = down;
            nav.selectOnLeft = left;
            nav.selectOnRight = right;
            s.navigation = nav;
        }
    }

    // an item with a size: the room's "ChooseDirection" is a 0x0 button MMIM
    // selects when the room opens (nothing shown selected); moving onto it
    // hid the cursor
    static bool Sized(Selectable s)
    {
        Rect r = WorldRect(s);
        return r.width > 1e-4f && r.height > 1e-4f;
    }

    static Vector3 Centre(Selectable s)
    {
        var rt = s.transform as RectTransform;
        if (!rt)
            return s.transform.position;
        Vector3[] c = s_cornersC;
        rt.GetWorldCorners(c);
        return (c[0] + c[2]) * 0.5f;
    }

    static Rect WorldRect(Selectable s)
    {
        var rt = s.transform as RectTransform;
        if (!rt)
            return new Rect(s.transform.position, Vector2.zero);
        Vector3[] c = s_cornersC;
        rt.GetWorldCorners(c);
        return Rect.MinMaxRect(Mathf.Min(c[0].x, c[2].x), Mathf.Min(c[0].y, c[2].y), Mathf.Max(c[0].x, c[2].x),
                               Mathf.Max(c[0].y, c[2].y));
    }

    // The nearest visible item that way, as a console menu moves: the nearest
    // row (column) first -- the gap between the two items' edges that way --
    // then the one most in line (overlapping across, else the smallest gap
    // across, else aligned with this one's left / top edge). Unity's own score
    // (distance that way / distance squared) jumps a row of narrow items
    // under a wide one: up from Fullscreen went to Music, past SFX.
    static Selectable Nearest(Selectable from, Vector3 dir)
    {
        Rect a = WorldRect(from);
        bool vertical = dir.y != 0f;
        Selectable best = null;
        float bestScore = float.MaxValue;
        foreach (Selectable s in s_visible)
        {
            if (s == from)
                continue;
            Rect b = WorldRect(s);
            // beyond this one that way by half an item at least: an item of
            // the same row a pixel higher is not "up" (Scoreboard Always On
            // went up to Fullscreen, beside it)
            float minSize = vertical ? Mathf.Min(a.height, b.height) : Mathf.Min(a.width, b.width);
            if (Vector2.Dot(dir, b.center - a.center) <= minSize * 0.5f)
                continue;
            float along, across, line;
            if (vertical)
            {
                along = dir.y > 0f ? b.yMin - a.yMax : a.yMin - b.yMax;
                across = Mathf.Max(0f, Mathf.Max(b.xMin - a.xMax, a.xMin - b.xMax));
                line = Mathf.Abs(b.xMin - a.xMin);
            }
            else
            {
                along = dir.x > 0f ? b.xMin - a.xMax : a.xMin - b.xMax;
                across = Mathf.Max(0f, Mathf.Max(b.yMin - a.yMax, a.yMin - b.yMax));
                line = Mathf.Abs(b.yMax - a.yMax);
            }
            float size = vertical ? Mathf.Max(a.height, 1e-4f) : Mathf.Max(a.width, 1e-4f);
            // items in one row (their gaps within a fifth of this item's size)
            // are told apart by how in line they are, not by a few pixels
            float score = Mathf.Round(Mathf.Max(0f, along) / (size * 0.2f)) * (size * 0.2f) + 2f * across + 0.01f * line;
            // one whose centre is not beyond this one's edge that way (under
            // or over it, not beside it) only when nothing else is: right
            // from the room's Character box went to the Colors button under
            // its right end, not to Map Selection beside it. (By the centre:
            // the maps' buttons are taller than their pictures, so the boxes
            // of two rows overlap; an edge rule skipped every second row.)
            float beyond = vertical ? (dir.y > 0f ? b.center.y - a.yMax : a.yMin - b.center.y)
                                    : (dir.x > 0f ? b.center.x - a.xMax : a.xMin - b.center.x);
            if (beyond < 0f)
                score += size + 2f * -beyond;
            if (score < bestScore)
            {
                bestScore = score;
                best = s;
            }
        }
        return best;
    }

    // --- scroll lists -------------------------------------------------------------------

    static void ScrollIntoView(GameObject selected)
    {
        var scroll = selected.GetComponentInParent<ScrollRect>();
        if (!scroll || !scroll.content || !scroll.viewport || !scroll.vertical)
            return;
        if (scroll.GetComponent<KeepChildInFocus>())
            return; // MMIM's own: eases the selection to the list's middle
        var item = selected.transform as RectTransform;
        if (!item || !item.IsChildOf(scroll.content))
            return;
        Vector3[] v = new Vector3[4], c = new Vector3[4];
        scroll.viewport.GetWorldCorners(v);
        item.GetWorldCorners(c);
        float viewBottom = v[0].y, viewTop = v[1].y, itemBottom = c[0].y, itemTop = c[1].y;
        // the first row of a section shows with the section's heading above
        // it (the maps: "--- Vanilla Maps ---") when both fit
        float top = itemTop;
        Transform row = item;
        while (row.parent && row.parent != scroll.content)
            row = row.parent;
        for (int i = row.GetSiblingIndex() - 1; row.parent == scroll.content && i >= 0; i--)
        {
            var prev = scroll.content.GetChild(i) as RectTransform;
            if (!prev || !prev.gameObject.activeInHierarchy)
                continue;
            if (!prev.GetComponentInChildren<Selectable>())
            {
                prev.GetWorldCorners(c);
                if (c[1].y - itemBottom <= viewTop - viewBottom)
                    top = Mathf.Max(top, c[1].y);
            }
            break;
        }
        float shift = 0f;
        if (top > viewTop)
            shift = top - viewTop;
        else if (itemBottom < viewBottom)
            shift = itemBottom - viewBottom;
        if (shift == 0f)
            return;
        Vector3 local = scroll.content.parent.InverseTransformVector(new Vector3(0f, shift, 0f));
        s_scroll = scroll;
        s_scrollTarget = scroll.content.anchoredPosition - new Vector2(0f, local.y);
    }

    // the list being eased to its target (a few frames, not a jump)
    static ScrollRect s_scroll;
    static Vector2 s_scrollTarget;

    static void EaseScroll()
    {
        if (!s_scroll)
            return;
        Vector2 now = s_scroll.content.anchoredPosition;
        Vector2 next = Vector2.Lerp(now, s_scrollTarget, 1f - Mathf.Exp(-18f * Time.unscaledDeltaTime));
        if ((next - s_scrollTarget).sqrMagnitude < 0.25f)
        {
            next = s_scrollTarget;
            s_scroll.content.anchoredPosition = next;
            s_scroll = null;
            return;
        }
        s_scroll.velocity = Vector2.zero;
        s_scroll.content.anchoredPosition = next;
    }

    // the front panel's scroll list with nothing to select in it and more
    // text than it shows (the credits), or null
    static ScrollRect TextScroll()
    {
        if (!s_front)
            return null;
        foreach (ScrollRect scroll in s_front.GetComponentsInChildren<ScrollRect>())
        {
            if (!scroll.isActiveAndEnabled || !scroll.vertical || !scroll.content || !scroll.viewport)
                continue;
            if (scroll.content.rect.height <= scroll.viewport.rect.height + 1f)
                continue;
            bool items = false;
            foreach (Selectable s in scroll.content.GetComponentsInChildren<Selectable>())
                if (s.IsInteractable())
                {
                    items = true;
                    break;
                }
            if (!items)
                return scroll;
        }
        return null;
    }

    static ScrollRect s_textScroll; // FixNavigation's pick

    // whether the items' centres are all on one row (none above another)
    static bool OneRow(List<Selectable> items)
    {
        float lo = float.MaxValue, hi = float.MinValue, height = float.MaxValue;
        foreach (Selectable s in items)
        {
            var rt = s.transform as RectTransform;
            if (!rt)
                continue;
            Vector3[] c = new Vector3[4];
            rt.GetWorldCorners(c);
            float y = (c[0].y + c[1].y) * 0.5f;
            lo = Mathf.Min(lo, y);
            hi = Mathf.Max(hi, y);
            height = Mathf.Min(height, c[1].y - c[0].y);
        }
        return lo > hi || hi - lo <= height * 0.5f;
    }

    // held up or down (L Stick, D-Pad or R Stick): the text moves, about a
    // screenful of the list a second at full tilt
    static void ScrollText(Gamepad pad)
    {
        float v = pad.leftStick.ReadValue().y + pad.dpad.ReadValue().y + pad.rightStick.ReadValue().y;
        v = Mathf.Clamp(v, -1f, 1f);
        if (Mathf.Abs(v) < 0.3f)
            return;
        ScrollRect scroll = s_textScroll;
        if (!scroll || !scroll.isActiveAndEnabled || !s_front || !scroll.transform.IsChildOf(s_front))
            return;
        float range = scroll.content.rect.height - scroll.viewport.rect.height;
        float speed = scroll.viewport.rect.height * 1.2f;
        scroll.velocity = Vector2.zero;
        scroll.verticalNormalizedPosition =
            Mathf.Clamp01(scroll.verticalNormalizedPosition + v * speed * Time.unscaledDeltaTime / range);
    }
}

// Unity's text fields open the keyboard as soon as they are selected
// (TMP_InputField.OnSelect -> ActivateInputField, applied in its LateUpdate):
// with a controller that is every time the cursor passes over one, and on
// the Switch the keyboard covers the screen. When the selection has just
// moved onto a text field, the pending activation is dropped before the
// field's LateUpdate runs; A on a selected field (OnSubmit) and a touch still
// open it.
[DefaultExecutionOrder(-30000)]
public class TextFieldGuard : MonoBehaviour
{
    static readonly System.Reflection.FieldInfo s_tmpFlag = typeof(TMPro.TMP_InputField).GetField(
        "m_ShouldActivateNextUpdate", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
    static readonly System.Reflection.FieldInfo s_uiFlag = typeof(InputField).GetField(
        "m_ShouldActivateNextUpdate", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);

    GameObject last;

    void LateUpdate()
    {
        EventSystem es = EventSystem.current;
        GameObject selected = es ? es.currentSelectedGameObject : null;
        if (selected == last)
            return;
        last = selected;
        if (!selected || !ControllerMenuSupport.ControllerMode)
            return;
        var tmp = selected.GetComponent<TMPro.TMP_InputField>();
        if (tmp && !tmp.isFocused && s_tmpFlag != null)
            s_tmpFlag.SetValue(tmp, false);
        var ui = selected.GetComponent<InputField>();
        if (ui && !ui.isFocused && s_uiFlag != null)
            s_uiFlag.SetValue(ui, false);
    }
}
