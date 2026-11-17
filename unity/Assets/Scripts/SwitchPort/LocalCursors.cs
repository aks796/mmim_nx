// LocalCursors.cs -- a cursor for every player in the local room (Switch port, mmim_nx).
//
// In the local room each player has a cursor of their own, as a console's
// character select has: players 2-4 move in their own Player Settings box
// (character and colours) with their own controller, and player 1 has the
// rest -- their own box and the match's rules (map, stars, coins, lives,
// time, Start Game, Back). Nobody's cursor can go into another player's box.
//
// Each of players 2-4 gets an event system of their own (the Input System's
// MultiplayerEventSystem, kept to their box) with a copy of the menus' UI
// actions bound to their gamepad alone; MMIM's own event system becomes
// player 1's (their gamepad, and touch). ControllerMenuSupport keeps every
// cursor's selection, links and Back for its zone: a player's zone is their
// box (and what it opens: a dropdown's list, the colour palette and the
// click-catching blockers those put over the screen); player 1's is
// everything else. A box that a panel player 1 opened covers (the maps,
// Settings) waits until it closes.

using System.Collections.Generic;
using System.Reflection;
using TMPro;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.InputSystem;
using UnityEngine.InputSystem.UI;
using UnityEngine.UI;

public static class LocalCursors
{
    static readonly MultiplayerEventSystem[] s_systems = new MultiplayerEventSystem[LocalPlay.MaxPlayers];
    static readonly InputDevice[] s_pads = new InputDevice[LocalPlay.MaxPlayers];
    static InputActionAsset s_mainAsset;   // MMIM's UI actions, player 1's while the room is up
    static InputDevice s_mainPad;
    static int s_mainDeviceCount = -1;
    static bool s_active;
    static int s_players = 1;
    static readonly Dictionary<GameObject, int> s_claims = new Dictionary<GameObject, int>();
    static readonly FieldInfo s_dropdownBlocker =
        typeof(TMP_Dropdown).GetField("m_Blocker", BindingFlags.Instance | BindingFlags.NonPublic);

    // the local room's screen, with a cursor per player
    public static bool Active => s_active;
    public static int Players => s_active ? s_players : 1;

    public static EventSystem SystemOf(int slot)
    {
        return slot > 0 && slot < s_systems.Length ? s_systems[slot] : null;
    }

    public static Gamepad PadOf(int slot)
    {
        var pads = Gamepad.all;
        return slot >= 0 && slot < pads.Count ? pads[slot] : null;
    }

    // every frame (ControllerMenuSupport), before the cursors are kept
    public static void Update()
    {
        bool want = LocalPlay.Active && LocalPlay.InRoomScreen && MainMenuManager.Instance &&
                    LocalPlayMenu.HasBoxes && EventSystem.current;
        if (!want)
        {
            if (s_active)
                Stop();
            return;
        }
        if (!s_active)
        {
            Debug.LogWarning("[local] a cursor per player in the room");
            ControllerMenuSupport.RedumpPanels();
        }
        s_active = true;
        s_players = Mathf.Clamp(LocalPlay.Players, 1, LocalPlay.MaxPlayers);

        // player 1: MMIM's event system, their gamepad and touch
        if (!s_mainAsset)
        {
            var module = EventSystem.current.GetComponent<InputSystemUIInputModule>();
            s_mainAsset = module ? module.actionsAsset : null;
        }
        Gamepad pad0 = PadOf(0);
        if (s_mainAsset && (pad0 != s_mainPad || UnityEngine.InputSystem.InputSystem.devices.Count != s_mainDeviceCount))
        {
            s_mainPad = pad0;
            s_mainDeviceCount = UnityEngine.InputSystem.InputSystem.devices.Count;
            var devices = new List<InputDevice>();
            foreach (InputDevice d in UnityEngine.InputSystem.InputSystem.devices)
                if (!(d is Gamepad) || d == pad0)
                    devices.Add(d);
            s_mainAsset.devices = devices.ToArray();
        }

        // players 2-4: an event system each, kept to their box
        for (int slot = 1; slot < s_systems.Length; slot++)
        {
            GameObject box = slot < s_players ? LocalPlayMenu.BoxOf(slot) : null;
            if (!box || !s_mainAsset)
            {
                Drop(slot);
                continue;
            }
            MultiplayerEventSystem es = s_systems[slot] ? s_systems[slot] : Make(slot, box);
            if (es.playerRoot != box)
            {
                es.playerRoot = box;
                if (es.currentSelectedGameObject && !es.currentSelectedGameObject.transform.IsChildOf(box.transform))
                    es.SetSelectedGameObject(null);
            }
            Gamepad pad = PadOf(slot);
            if (pad != s_pads[slot])
            {
                s_pads[slot] = pad;
                var module = es.GetComponent<InputSystemUIInputModule>();
                if (module && module.actionsAsset)
                    module.actionsAsset.devices = pad != null ? new InputDevice[] { pad } : new InputDevice[0];
            }
        }

        // what the open dropdowns' and palettes' blockers belong to
        s_claims.Clear();
        foreach (Selectable s in ControllerMenuSupport.Selectables())
            if (s is TMP_Dropdown d && d.IsExpanded)
            {
                int zone = ZoneOf(d.transform);
                if (s_dropdownBlocker != null && s_dropdownBlocker.GetValue(d) is GameObject b && b)
                    s_claims[b] = zone;
                TintList(d, zone);
            }
        if (s_tintedPlayers != s_players)
        {
            s_tintedPlayers = s_players; // the room opened, or players joined: their buttons in their colours
            TintButtons();
        }
        foreach (ColorChooser c in ColorChooser.All)
            if (c && c.Blocker)
                s_claims[c.Blocker] = ZoneOf(c.transform);
    }

    static MultiplayerEventSystem Make(int slot, GameObject box)
    {
        var go = new GameObject("EventSystem (P" + (slot + 1) + ")");
        go.SetActive(false);
        var es = go.AddComponent<MultiplayerEventSystem>();
        var module = go.AddComponent<InputSystemUIInputModule>();
        var main = EventSystem.current.GetComponent<InputSystemUIInputModule>();
        // MMIM's UI actions, a copy of this player's own (move, A, B; no pointer)
        InputActionAsset asset = Object.Instantiate(s_mainAsset);
        asset.name = s_mainAsset.name + " (P" + (slot + 1) + ")";
        module.actionsAsset = asset;
        module.move = Ref(asset, main && main.move ? main.move.action : null, "UI/Navigate");
        module.submit = Ref(asset, main && main.submit ? main.submit.action : null, "UI/Submit");
        module.cancel = Ref(asset, main && main.cancel ? main.cancel.action : null, "UI/Cancel");
        if (main)
        {
            module.moveRepeatDelay = main.moveRepeatDelay;
            module.moveRepeatRate = main.moveRepeatRate;
        }
        Gamepad pad = PadOf(slot);
        s_pads[slot] = pad;
        asset.devices = pad != null ? new InputDevice[] { pad } : new InputDevice[0];
        es.sendNavigationEvents = true;
        es.playerRoot = box;
        s_systems[slot] = es;
        go.SetActive(true);
        Debug.LogWarning("[local] player " + (slot + 1) + "'s cursor: " + (pad != null ? pad.displayName + " (" + pad.deviceId + ")" : "no gamepad"));
        return es;
    }

    // the same action in a copy of its asset
    static InputActionReference Ref(InputActionAsset copy, InputAction original, string fallback)
    {
        InputAction action = null;
        if (original != null && original.actionMap != null)
            action = copy.FindAction(original.actionMap.name + "/" + original.name);
        if (action == null)
            action = copy.FindAction(fallback);
        return action != null ? InputActionReference.Create(action) : null;
    }

    static void Drop(int slot)
    {
        MultiplayerEventSystem es = s_systems[slot];
        s_systems[slot] = null;
        s_pads[slot] = null;
        if (!es)
            return;
        es.SetSelectedGameObject(null); // its selection drawn as selected no more
        var module = es.GetComponent<InputSystemUIInputModule>();
        InputActionAsset asset = module ? module.actionsAsset : null;
        Object.Destroy(es.gameObject);
        if (asset)
            Object.Destroy(asset);
    }

    static void Stop()
    {
        s_active = false;
        for (int slot = 1; slot < s_systems.Length; slot++)
            Drop(slot);
        if (s_mainAsset)
            s_mainAsset.devices = null; // menus and pause: any controller again
        s_mainAsset = null;
        s_mainPad = null;
        s_mainDeviceCount = -1;
        s_claims.Clear();
        UntintButtons(); // MMIM's blue again (the online room)
        s_tintedPlayers = -1;
        Debug.LogWarning("[local] one cursor again");
    }

    // whose an object is: 1-3 inside player 2-4's box (or a blocker their
    // dropdown or palette put up), else 0 (player 1's)
    public static int ZoneOf(Transform t)
    {
        return ZoneOf(t, out _);
    }

    public static int ZoneOf(Transform t, out bool claimed)
    {
        claimed = false;
        if (!s_active || !t)
            return 0;
        for (int slot = 1; slot < s_players; slot++)
        {
            GameObject box = LocalPlayMenu.BoxOf(slot);
            if (box && t.IsChildOf(box.transform))
                return slot;
        }
        foreach (KeyValuePair<GameObject, int> kv in s_claims)
            if (kv.Key && t.IsChildOf(kv.Key.transform))
            {
                claimed = true;
                return kv.Value;
            }
        return 0;
    }

    // the event system whose cursor an object is for (MMIM's outside the room)
    public static EventSystem SystemFor(Transform t)
    {
        if (!s_active)
            return EventSystem.current;
        int zone = ZoneOf(t);
        return zone > 0 ? SystemOf(zone) : EventSystem.current;
    }

    // the colour a player's cursor is drawn in (their own: red, green, yellow, blue)
    public static Color TintOf(Transform t)
    {
        return s_active ? LocalPlay.GlowColor(ZoneOf(t)) : Color.white;
    }

    // --- each player's cursor in their colour -------------------------------------------
    // MMIM's blue square buttons (Colors, the rules' toggles) light up in
    // their selected sprite; its sheet has the same in red, green and yellow
    // (Resources/SwitchPort/option: a copy of it): each player's lights up in
    // theirs. Their open character list's bar is drawn in their colour too.
    static readonly string[] k_optionColours = { "red", "green", "yellow", "blue" };
    static readonly Dictionary<Selectable, SpriteState> s_tinted = new Dictionary<Selectable, SpriteState>();
    static Dictionary<string, Sprite> s_option;
    static int s_tintedPlayers = -1;

    static Sprite Option(string name)
    {
        if (s_option == null)
        {
            s_option = new Dictionary<string, Sprite>();
            foreach (Sprite sp in Resources.LoadAll<Sprite>("SwitchPort/option"))
                s_option[sp.name] = sp;
        }
        return s_option.TryGetValue(name, out Sprite s) ? s : null;
    }

    static void TintButtons()
    {
        UntintButtons();
        foreach (Selectable s in ControllerMenuSupport.Selectables())
        {
            if (!s || s.transition != Selectable.Transition.SpriteSwap)
                continue;
            SpriteState st = s.spriteState;
            if (!st.selectedSprite || st.selectedSprite.name != "option_blue_selected")
                continue;
            string colour = k_optionColours[Mathf.Clamp(ZoneOf(s.transform), 0, k_optionColours.Length - 1)];
            Sprite selected = Option("option_" + colour + "_selected"), pressed = Option("option_" + colour + "_pressed");
            if (!selected)
                continue;
            s_tinted[s] = st;
            SpriteState now = st;
            now.selectedSprite = selected;
            if (st.highlightedSprite && st.highlightedSprite.name == "option_blue_selected")
                now.highlightedSprite = selected;
            if (pressed && st.pressedSprite && st.pressedSprite.name == "option_blue_pressed")
                now.pressedSprite = pressed;
            s.spriteState = now;
        }
    }

    static void UntintButtons()
    {
        foreach (KeyValuePair<Selectable, SpriteState> kv in s_tinted)
            if (kv.Key)
                kv.Key.spriteState = kv.Value;
        s_tinted.Clear();
    }

    // an open character list: its rows' bar in the player's colour (MMIM's
    // gray: a 0.61 gray toggle tint over a half-transparent white)
    static void TintList(TMP_Dropdown d, int zone)
    {
        Transform list = d.transform.Find("Dropdown List");
        if (!list)
            return;
        Color c = LocalPlay.GlowColor(zone);
        foreach (Toggle t in list.GetComponentsInChildren<Toggle>())
        {
            ColorBlock cb = t.colors;
            if (cb.selectedColor == c)
                continue;
            cb.selectedColor = c;
            cb.highlightedColor = c;
            cb.pressedColor = c * 0.8f;
            t.colors = cb;
        }
    }

    // MMIM code that selects through EventSystem.current (a palette closing)
    // run for one player's event system
    public static void Run(EventSystem es, System.Action action)
    {
        EventSystem was = EventSystem.current;
        if (es && es != was)
            EventSystem.current = es;
        try
        {
            action();
        }
        finally
        {
            if (was && EventSystem.current != was)
                EventSystem.current = was;
        }
    }
}
