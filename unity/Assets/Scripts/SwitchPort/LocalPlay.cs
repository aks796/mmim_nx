// LocalPlay.cs -- 2 to 4 players on one console, split screen (Switch port, mmim_nx).
//
// Local play is MMIM's own room and match, made with PUN's OfflineMode (no
// server: the room, its events and RPCs stay on this console), with a second
// player character spawned on the same console. Everything that MMIM keys by
// Photon player (the one LocalPlayer here) is keyed by local slot instead:
//
//   slot        PhotonNetwork.Instantiate's data (object[] { slot }) of each
//               player object; 0 = player 1 (MMIM's own localPlayer)
//   input       one Controls per slot, bound to that player's gamepad only
//               (Gamepad.all[slot]: the loader announces them in the
//               console's player order); menus and pause take any gamepad
//   character   player 1: MMIM's Character dropdown (LocalPlayer property);
//   and colour  players 2-4: their Player Settings box (PlayerPrefs); nobody
//               looks the same: a character somebody has gives the next colour
//   camera      SplitScreen: one camera rig and HUD per player
//   rumble      SwitchRumble.Native(slot, ...)
//
// Who plays is the console's: Local Play opens its controller screen for 2
// to 4 players (NSMBW's colours: red, green, yellow, blue) and the players
// it confirms are the players; holding L+R in the local room opens it again
// to join or leave. The rules (stars, coins, lives, time, items, map) are the
// room's, set in MMIM's own room settings. Native() is an empty method the
// loader hooks (mvl_nx game_patch.c), Picked() the loader's answer.

using System.Collections;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using NSMB.Utils;
using Photon.Pun;
using UnityEngine;
using UnityEngine.InputSystem;

public class LocalPlay : MonoBehaviour
{
    public const int MaxPlayers = 4;

    // the loader's operations (game_patch.c on_local_play)
    const int OpGamepads = 1;   // announce `arg` gamepads (1: menus follow player 1)
    const int OpController = 2; // the controller screen for 2..`arg` players (answer: Picked)
    const int OpRoom = 3;       // the local room's screen is up (1) or not (0)
    const int OpProfile = 4;    // a split match runs (1) or ended (0): the loader's [prof] windows meanwhile

    public static bool Active { get; private set; }
    public static int Players { get; private set; } = 1;

    // a split match: more than one player on this console
    public static bool Split => Active && Players > 1;

    // the slot that won the match (-1 none: a draw), for EndGame's text
    public static int WinnerSlot = -1;

    // each player's camera zoom (HorizontalCamera: out while flying)
    public static readonly float[] ZoomTarget = new float[MaxPlayers];

    static LocalPlay s_runner;
    static readonly Controls[] s_controls = new Controls[MaxPlayers];
    static readonly InputDevice[] s_device = new InputDevice[MaxPlayers];
    static float s_native;
    static int s_picked = -1, s_joined = -1;
    static bool s_picking, s_inRoom;

    // players joined or left in the local room (L+R: the controller screen)
    public static event System.Action PlayersChanged;

    // --- starting and ending --------------------------------------------------------------

    // Local Play pressed: the console's controller screen for 2 to 4 players;
    // `done` gets how many it confirmed (0: closed with B)
    public static IEnumerator PickPlayers(System.Action<int> done)
    {
        s_picked = -1;
        s_picking = true;
        Native(OpController, MaxPlayers);
        // the loader answers at the end of this frame (the applet holds the
        // game until then); none in a while: no loader (the editor), two
        for (int frames = 0; s_picked < 0 && frames < 30; frames++)
            yield return null;
        s_picking = false;
        int n = s_picked < 0 ? 2 : s_picked;
        Debug.LogWarning("[local] controller screen: " + (n > 0 ? n + " players" : "closed"));
        done(n);
    }

    // the loader (game_patch.c local_picked): the players the controller
    // screen confirmed
    [UnityEngine.Scripting.Preserve]
    public static void Picked(int players)
    {
        if (s_picking)
            s_picked = players;
        else
            s_joined = players; // L+R in the local room: Update takes it (between frames now)
    }

    // L+R in the local room: players joined or left
    static void Joined(int players)
    {
        if (!Active || !s_inRoom || players < 2 || players == Players)
            return;
        Players = Mathf.Clamp(players, 2, MaxPlayers);
        AnnounceGamepads();
        AssignDefaults(); // whoever joined: a character nobody is
        Debug.LogWarning("[local] " + Players + " players now");
        PlayersChanged?.Invoke();
    }

    // the local room's screen up or not: L+R there opens the controller
    // screen for joining (elsewhere for the players there are)
    public static void InRoom(bool on)
    {
        s_inRoom = on && Active;
        Native(OpRoom, s_inRoom ? 1 : 0);
    }

    // --- the wins tally (the room's player tabs) -----------------------------------------
    // Each player's wins while the game runs, a party game's tally: shown on
    // the local room's player tabs (MMIM's crown x N), which are ordered by it.

    static readonly int[] s_wins = new int[MaxPlayers];

    public static int WinsOf(int slot)
    {
        return slot >= 0 && slot < MaxPlayers ? s_wins[slot] : 0;
    }

    // a split match ended with a winner (GameManager.EndGame)
    public static void CountWin(int slot)
    {
        if (!Split || slot < 0 || slot >= MaxPlayers)
            return;
        s_wins[slot]++;
        Debug.LogWarning("[local] " + NameOf(slot) + " wins: " + s_wins[slot] + " so far");
    }

    // the room's Add Player tab: the console's controller screen, as L+R
    // there (its answer comes back through Picked: players joined or left)
    public static void OpenControllerScreen()
    {
        if (Active && s_inRoom && !s_picking)
            Native(OpController, MaxPlayers);
    }

    // a split match began or ended: the loader samples where the main and
    // render threads are and logs 10 s windows of it meanwhile (debug.log
    // [prof]), whatever the config's profile line says
    public static void Profile(bool on)
    {
        Native(OpProfile, on ? 1 : 0);
    }

    // the local room's screen is up (LocalCursors: a cursor per player)
    public static bool InRoomScreen => s_inRoom;

    // the players start: one gamepad each
    public static void Begin(int players)
    {
        players = Mathf.Clamp(players, 1, MaxPlayers);
        if (!Active)
            StartAsDefaults();
        Active = true;
        Players = players;
        WinnerSlot = -1;
        for (int i = 0; i < MaxPlayers; i++)
            ZoomTarget[i] = 0f;
        if (!s_runner)
        {
            var go = new GameObject("LocalPlay");
            DontDestroyOnLoad(go);
            s_runner = go.AddComponent<LocalPlay>();
        }
        Native(OpGamepads, players);
        Debug.LogWarning("[local] local play for " + players + " player(s)");
    }

    // the local room left: online again, menus follow player 1
    public static void End()
    {
        if (!Active)
            return;
        InRoom(false);
        Active = false;
        Players = 1;
        RestoreOnlinePick();
        for (int i = 0; i < MaxPlayers; i++)
        {
            s_controls[i]?.Dispose();
            s_controls[i] = null;
            s_device[i] = null;
        }
        Native(OpGamepads, 1);
        Debug.LogWarning("[local] local play ended");
    }

    // one gamepad per player again (a level or the local room opening: the
    // loader may have gone back to menus-follow-player-1 in between)
    public static void AnnounceGamepads()
    {
        Native(OpGamepads, Active ? Players : 1);
    }

    // --- who is who ---------------------------------------------------------------------------

    // a player object's slot (0 outside local play, and for player 1)
    public static int SlotOf(PhotonView view)
    {
        if (!Active || !view)
            return 0;
        object[] data = view.InstantiationData;
        return data != null && data.Length > 0 && data[0] is int slot && slot >= 0 && slot < MaxPlayers ? slot : 0;
    }

    public static int SlotOf(PlayerController player)
    {
        return player ? SlotOf(player.photonView) : 0;
    }

    public static PlayerController PlayerAt(int slot)
    {
        GameManager gm = GameManager.Instance;
        if (!gm)
            return null;
        foreach (PlayerController p in gm.players)
            if (p && SlotOf(p) == slot)
                return p;
        return null;
    }

    public static string NameOf(int slot)
    {
        return "Player " + (Mathf.Max(slot, 0) + 1);
    }

    // player 1: MMIM's own choice (the LocalPlayer's properties); players
    // 2-4: their picks in the room. Every Local Play starts as Mario, Luigi,
    // Toad and Wario in their own colours (user: not the last played; Toad
    // over Toadette, 2026-11-16);
    // picks then hold for the session's matches (back in the room after one).
    // Player 1's online pick (MMIM's saved Settings) is kept aside and put
    // back when local play ends, so Online Play still opens as it was.
    static readonly string[] k_defaultCharacters = { "Mario", "Luigi", "Toad", "Wario" };
    static readonly int[] s_character = { -1, -1, -1, -1 }; // -1: not picked yet (the default)
    static readonly int[] s_skin = new int[MaxPlayers];
    static int s_onlineCharacter = -1, s_onlineSkin;

    static void StartAsDefaults()
    {
        for (int i = 0; i < MaxPlayers; i++)
        {
            s_character[i] = -1;
            s_skin[i] = 0;
        }
        Settings settings = Singleton<Settings>.Instance;
        if (!settings)
            return;
        s_onlineCharacter = settings.character;
        s_onlineSkin = settings.skin;
        // the local room reads player 1 from these (LocalPlayMenu.StartLocal)
        settings.character = DefaultCharacter(0);
        settings.skin = 0;
    }

    static void RestoreOnlinePick()
    {
        Settings settings = Singleton<Settings>.Instance;
        if (s_onlineCharacter < 0 || !settings)
            return;
        settings.character = s_onlineCharacter;
        settings.skin = s_onlineSkin;
        settings.SaveSettingsToPreferences(); // the room's player 1 box saved the local picks
        s_onlineCharacter = -1;
    }

    public static int DefaultCharacter(int slot)
    {
        PlayerData[] chars = GlobalController.Instance ? GlobalController.Instance.characters : null;
        if (chars == null || chars.Length == 0)
            return 0;
        string want = k_defaultCharacters[Mathf.Clamp(slot, 0, k_defaultCharacters.Length - 1)];
        for (int i = 0; i < chars.Length; i++)
            if (chars[i] && string.Equals(chars[i].name, want, System.StringComparison.OrdinalIgnoreCase))
                return i;
        return slot % chars.Length;
    }

    public static int CharacterOf(int slot)
    {
        if (slot == 0)
            return Utils.GetCharacterIndex();
        slot = Mathf.Clamp(slot, 1, MaxPlayers - 1);
        int count = CharacterCount;
        return Mathf.Clamp(s_character[slot] >= 0 ? s_character[slot] : DefaultCharacter(slot), 0, count - 1);
    }

    public static void SetCharacter(int slot, int index)
    {
        if (slot > 0 && slot < MaxPlayers)
            s_character[slot] = index;
    }

    public static int SkinOf(int slot)
    {
        if (slot == 0)
        {
            Utils.GetCustomProperty<int>(Enums.NetPlayerProperties.PlayerColor, out int skin, PhotonNetwork.LocalPlayer.CustomProperties);
            return skin;
        }
        return Mathf.Clamp(s_skin[Mathf.Clamp(slot, 1, MaxPlayers - 1)], 0, SkinCount - 1);
    }

    public static void SetSkin(int slot, int index)
    {
        if (slot > 0 && slot < MaxPlayers)
            s_skin[slot] = index;
    }

    // --- nobody looks the same (Smash's rule) -------------------------------------------------

    static int CharacterCount => GlobalController.Instance ? GlobalController.Instance.characters.Length : 2;
    static int SkinCount => GlobalController.Instance ? GlobalController.Instance.skins.Length : 1;

    // the room opened or players joined: players 2-4 start as characters
    // nobody else is (their last pick when it is free, else the first free one
    // from their own place in MvL's list: player 2 Luigi, ...), and anyone
    // left the same as another (more players than characters) takes a colour
    // of their own. Player 1's choice is MMIM's own and stays.
    public static void AssignDefaults()
    {
        if (!Active)
            return;
        int count = CharacterCount;
        // everyone's character first (a free one: the defaults in order,
        // then the rest of MvL's list), then the colours: the colours were
        // made distinct against a player 4 still on a stale pick, which gave
        // player 3 another colour for nothing
        for (int slot = 1; slot < Players; slot++)
        {
            int ch = CharacterOf(slot);
            if (!CharacterUsedBelow(slot, ch))
                continue;
            int pick = -1;
            for (int d = 0; d < k_defaultCharacters.Length && pick < 0; d++)
            {
                int c = DefaultCharacter(d);
                if (!CharacterUsedBelow(slot, c))
                    pick = c;
            }
            for (int i = 0; i < count && pick < 0; i++)
                if (!CharacterUsedBelow(slot, (slot + i) % count))
                    pick = (slot + i) % count;
            if (pick >= 0)
                SetCharacter(slot, pick);
        }
        for (int slot = 1; slot < Players; slot++)
            MakeDistinct(slot);
    }

    static bool CharacterUsedBelow(int slot, int character)
    {
        for (int j = 0; j < slot; j++)
            if (CharacterOf(j) == character)
                return true;
        return false;
    }

    // a player just picked a character or colour that another player already
    // has (both the same): they take the next colour nobody with that
    // character has, as Smash gives the next costume. True when it changed.
    public static bool MakeDistinct(int slot)
    {
        if (!Active || slot < 0 || slot >= Players)
            return false;
        int ch = CharacterOf(slot), skin = SkinOf(slot), count = SkinCount;
        if (!LooksLikeAnother(slot, ch, skin))
            return false;
        for (int i = 1; i < count; i++)
        {
            int s = (skin + i) % count;
            if (LooksLikeAnother(slot, ch, s))
                continue;
            if (slot == 0)
            {
                // player 1's colour is MMIM's own (its box shows it)
                if (MainMenuManager.Instance)
                    MainMenuManager.Instance.SetPlayerColor(s);
            }
            else
            {
                SetSkin(slot, s);
            }
            Debug.LogWarning("[local] " + NameOf(slot) + " is the same character as another: colour " + s);
            return true;
        }
        return false;
    }

    static bool LooksLikeAnother(int slot, int character, int skin)
    {
        for (int j = 0; j < Players; j++)
            if (j != slot && CharacterOf(j) == character && SkinOf(j) == skin)
                return true;
        return false;
    }

    public static PlayerData CharacterData(int slot)
    {
        return GlobalController.Instance.characters[CharacterOf(slot)];
    }

    // the outline / track / scoreboard colour, NSMBW's (and the controller
    // screen's, game_patch.c s_colors): red, green, yellow, blue
    static readonly Color32[] k_colors =
    {
        new Color32(0xE6, 0x00, 0x12, 0xFF), new Color32(0x00, 0xA8, 0x3C, 0xFF),
        new Color32(0xFF, 0xC8, 0x00, 0xFF), new Color32(0x00, 0x78, 0xF0, 0xFF),
    };

    public static Color GlowColor(int slot)
    {
        return k_colors[Mathf.Clamp(slot, 0, k_colors.Length - 1)];
    }

    // the local player nearest a point (sounds' volume by distance)
    public static bool NearestPlayer(Vector3 to, out Vector3 position)
    {
        position = Vector3.zero;
        GameManager gm = GameManager.Instance;
        if (!Split || !gm)
            return false;
        float best = float.MaxValue;
        foreach (PlayerController p in gm.players)
        {
            if (!p)
                continue;
            float d = Utils.WrappedDistance(p.transform.position, to);
            if (d < best)
            {
                best = d;
                position = p.transform.position;
            }
        }
        return best < float.MaxValue;
    }

    // --- input: one Controls per player, on that player's gamepad -----------------------------

    // what PlayerController binds to (MMIM's global InputSystem.controls outside split play)
    public static Controls ControlsFor(int slot)
    {
        if (!Split || slot < 0 || slot >= MaxPlayers)
            return InputSystem.controls;
        if (s_controls[slot] == null)
        {
            var c = new Controls();
            string json = GlobalController.Instance ? GlobalController.Instance.controlsJson : null;
            if (!string.IsNullOrEmpty(json))
                c.LoadBindingOverridesFromJson(json);
            c.devices = new InputDevice[0]; // none until its gamepad is known
            c.Player.Enable();
            s_controls[slot] = c;
            s_device[slot] = null;
            AssignDevices();
        }
        return s_controls[slot];
    }

    // Gamepad.all[slot]: after the controller screen the loader announces the
    // gamepads again in player order, so this follows every change
    static void AssignDevices()
    {
        var pads = Gamepad.all;
        for (int i = 0; i < MaxPlayers; i++)
        {
            if (s_controls[i] == null)
                continue;
            InputDevice want = i < pads.Count ? pads[i] : null;
            if (want == s_device[i])
                continue;
            s_device[i] = want;
            s_controls[i].devices = want != null ? new[] { want } : new InputDevice[0];
            Debug.LogWarning("[local] player " + (i + 1) + ": " + (want != null ? want.displayName + " (" + want.deviceId + ")" : "no gamepad"));
        }
    }

    void Update()
    {
        if (s_joined >= 0)
        {
            int n = s_joined;
            s_joined = -1;
            Joined(n);
        }
        if (Split)
            AssignDevices();
    }

    // the loader's hook point: op, arg
    [MethodImpl(MethodImplOptions.NoInlining)]
    public static void Native(int op, int arg)
    {
        s_native = op + arg; // a body, so the call is kept
    }
}
