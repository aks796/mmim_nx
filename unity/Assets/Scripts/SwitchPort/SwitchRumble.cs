// SwitchRumble.cs -- the controller rumbles at a Mario game's moments (Switch port, mmim_nx).
//
// The moments are the ones NSMBW gives its Wii Remote (a ground pound's
// landing, getting hurt, losing a life, quakes: dQuake_c's HIP_ATTACK and
// PLAYER_DAMAGE shocks and its fading death pattern) plus picking up a
// power-up, as Mario games on Switch do. Each is played by the loader as a
// shaped HD rumble effect (mvl_nx gamepad.c: low-band knocks for impacts, a
// rising high-band sparkle on each run of a power-up's jingle), not a Wii
// Remote's on/off motor.
// Only this console's own players feel them, each on their own controller
// (local play: the player's slot, LocalPlay).
//
// Native() is an empty method the loader hooks (mvl_nx game_patch.c).

using System.Runtime.CompilerServices;
using TMPro;
using UnityEngine;
using UnityEngine.UI;

public class SwitchRumble : MonoBehaviour
{
    // the loader's effects (gamepad.c k_effects)
    public enum Effect
    {
        Thud = 1,        // a ground pound lands
        Hit = 2,         // hurt or powered down
        Death = 3,       // a life lost
        Quake = 4,       // the ground shakes (Thwomp, a Mega Mushroom's steps)
        BigThud = 5,     // a Mega Mushroom's ground pound
        Powerup = 6,     // a power-up
        SmallPowerup = 7, // the Mini Mushroom
        Mega = 8,        // the Mega Mushroom
        OneUp = 9,       // a 1-Up
        Leaf = 10,       // the Super Leaf (its own sound: a poof, then a run)
        Gold = 11,       // the Gold Flower (two glints, then a long run)
    }

    static SwitchRumble s_instance;
    static float s_lastShake;
    static float s_lastThud = -1f;
    static float s_native;

    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
    static void Create()
    {
        if (s_instance)
            return;
        var go = new GameObject("SwitchRumble");
        DontDestroyOnLoad(go);
        s_instance = go.AddComponent<SwitchRumble>();
    }

    // Settings > Rumble (PlayerPrefs; on unless turned off)
    public static bool Enabled
    {
        get => PlayerPrefs.GetInt("SwitchRumble", 1) != 0;
        set
        {
            PlayerPrefs.SetInt("SwitchRumble", value ? 1 : 0);
            PlayerPrefs.Save();
        }
    }

    // MMIM's Fullscreen toggle means nothing on a console (always full
    // screen): in both Settings boxes it is the Rumble toggle, same box,
    // same font, its label renamed (SettingButtonManager.OnFullscreenToggle
    // stores it)
    public static void UseFullscreenToggle(Toggle toggle)
    {
        if (!toggle)
            return;
        Transform label = toggle.transform.Find("Text (TMP)");
        var text = label ? label.GetComponent<TMP_Text>() : null;
        if (text)
            text.text = "Rumble";
        toggle.SetIsOnWithoutNotify(Enabled);
    }

    public static void Play(Effect effect, float strength = 1f, int player = 0)
    {
        if (!Enabled)
            return;
        if (PortPlatform.Phone)
        {
            PhoneVibrate(effect, strength);
            return;
        }
        Native(player, (int)effect, strength);
    }

    // a phone (the MMIM_PHONE build): the effect as one short buzz of the
    // phone's vibrator, as long as the console's effect feels (a thud a tap,
    // a lost life a long one)
    static AndroidJavaObject s_vibrator;
    static bool s_noVibrator, s_never;

    static void PhoneVibrate(Effect effect, float strength)
    {
        long ms;
        switch (effect)
        {
            case Effect.Thud: ms = 20; break;
            case Effect.Hit: ms = 60; break;
            case Effect.Death: ms = 250; break;
            case Effect.Quake: ms = 40; break;
            case Effect.BigThud: ms = 80; break;
            case Effect.Mega: ms = 150; break;
            case Effect.SmallPowerup: ms = 15; break;
            default: ms = 30; break; // power-ups, 1-Up
        }
        ms = (long)Mathf.Max(10f, ms * Mathf.Clamp01(strength));
#if UNITY_ANDROID && !UNITY_EDITOR
        if (s_noVibrator)
            return;
        try
        {
            if (s_vibrator == null)
            {
                using (var unity = new AndroidJavaClass("com.unity3d.player.UnityPlayer"))
                using (var activity = unity.GetStatic<AndroidJavaObject>("currentActivity"))
                    s_vibrator = activity.Call<AndroidJavaObject>("getSystemService", "vibrator");
            }
            s_vibrator.Call("vibrate", ms);
        }
        catch (System.Exception e)
        {
            s_noVibrator = true;
            Debug.LogWarning("[rumble] no vibrator: " + e.Message);
        }
        // never runs: it puts the VIBRATE permission in the APK's manifest
        if (s_never)
            Handheld.Vibrate();
#endif
    }

    // what one of this console's own players hears (PlayerController.PlaySound)
    public static void ForSound(Enums.Sounds sound, int player = 0)
    {
        switch (sound)
        {
            case Enums.Sounds.Player_Sound_GroundpoundLanding:
                Play(Effect.Thud, 1f, player);
                s_lastThud = Time.unscaledTime; // its camera shake is this thud
                break;
            case Enums.Sounds.Player_Sound_Powerdown: // also the Poison Mushroom
                Play(Effect.Hit, 1f, player);
                break;
            case Enums.Sounds.Player_Sound_Death:
                Play(Effect.Death, 1f, player);
                break;
            // the power-ups' own collect sounds (Powerup.soundEffect)
            // (each rumble follows its sound: the power-up jingle's three runs)
            case Enums.Sounds.Player_Sound_PowerupCollect:
            case Enums.Sounds.Powerup_Propeller_Collect:
            case Enums.Sounds.Powerup_Penguin_Collect:
            case Enums.Sounds.Player_Sound_GummyCollect:
                Play(Effect.Powerup, 1f, player);
                break;
            case Enums.Sounds.Powerup_SMB3_Collect:
                Play(Effect.Leaf, 1f, player);
                break;
            case Enums.Sounds.Powerup_Gold_Collect:
                Play(Effect.Gold, 1f, player);
                break;
            case Enums.Sounds.Powerup_MiniMushroom_Collect:
                Play(Effect.SmallPowerup, 1f, player);
                break;
            case Enums.Sounds.Player_Sound_MegaMushroom_Collect:
                Play(Effect.Mega, 1f, player);
                break;
            case Enums.Sounds.Powerup_Sound_1UP:
                Play(Effect.OneUp, 1f, player);
                break;
        }
    }

    void Update()
    {
        // MMIM shakes the camera for quakes (CameraController: while the local
        // player stands); a new shake is a new quake, a big one a Mega stomp
        float shake = CameraController.ScreenShake;
        if (shake > s_lastShake + 0.01f && Time.unscaledTime - s_lastThud > 0.15f)
        {
            // every player of this console standing on the ground feels it
            for (int slot = 0; slot < (LocalPlay.Split ? LocalPlay.Players : 1); slot++)
            {
                if (!LocalPlayerOnGround(slot))
                    continue;
                if (shake >= 0.35f)
                    Play(Effect.BigThud, Mathf.Clamp01(shake * 2f), slot);
                else
                    Play(Effect.Quake, Mathf.Clamp01(0.3f + shake * 2f), slot);
            }
        }
        s_lastShake = shake;
    }

    static bool LocalPlayerOnGround(int slot)
    {
        var game = GameManager.Instance;
        if (!game)
            return false;
        PlayerController player = null;
        if (LocalPlay.Split)
            player = LocalPlay.PlayerAt(slot);
        else if (game.localPlayer)
            player = game.localPlayer.GetComponent<PlayerController>();
        return player && player.onGround;
    }

    // the loader's hook point: player (0 = player 1), effect, strength 0..1
    [MethodImpl(MethodImplOptions.NoInlining)]
    public static void Native(int player, int effect, float strength)
    {
        s_native = player + effect + strength; // a body, so the call is kept
    }
}
