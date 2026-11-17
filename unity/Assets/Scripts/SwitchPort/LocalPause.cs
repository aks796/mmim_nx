// LocalPause.cs -- the pause menu stops a Local Play match.
//
// MMIM's pause (GameManager.Pause) only opens the menu and stops the pausing
// player's input: online the match has to go on for everyone else. In Local
// Play every player is on this console, so the match carried on with
// nobody playing (user, 2026-11-17: "does pause stop the game in local
// play it should"). Now, while it is paused:
//   - Time.timeScale 0: physics, animation, enemies, coroutines stop
//     (input is read in the dynamic update, so the menu still works);
//   - PUN's offline clock is held (PhotonNetwork.PortOfflineClock): the
//     match timer and the moving platforms (GenericMover) run on it, and
//     would otherwise jump ahead by the time spent paused;
//   - the sound stops (AudioListener.pause) but the menu's own sounds,
//     played on GameManager.sfx, which ignores it while paused.
// Online, and outside a match, nothing changes.

using Photon.Pun;
using UnityEngine;

public class LocalPause : MonoBehaviour
{
    static int s_lastRaw;       // the offline clock as PUN read it last
    static int s_held;          // ms spent paused, taken off the clock
    static int s_heldSince = -1; // the raw clock when the pause began

    bool holding;
    AudioSource menuSound;
    bool menuSoundIgnored;
    float heldAt;

    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSceneLoad)]
    static void Install()
    {
        PhotonNetwork.PortOfflineClock = Clock;
        var go = new GameObject("LocalPause");
        DontDestroyOnLoad(go);
        go.AddComponent<LocalPause>();
    }

    // PUN's offline clock: stands still while paused, then goes on from there
    static int Clock(int raw)
    {
        s_lastRaw = raw;
        return (s_heldSince >= 0 ? s_heldSince : raw) - s_held;
    }

    void Update()
    {
        GameManager game = GameManager.Instance;
        bool hold = LocalPlay.Active && PhotonNetwork.OfflineMode && game && game.paused && !game.gameover;
        if (hold == holding)
            return;
        holding = hold;
        _ = PhotonNetwork.ServerTimestamp; // reads the raw clock into s_lastRaw
        if (hold)
        {
            s_heldSince = s_lastRaw;
            heldAt = Time.unscaledTime;
            Time.timeScale = 0f;
            menuSound = game.sfx;
            if (menuSound)
            {
                menuSoundIgnored = menuSound.ignoreListenerPause;
                menuSound.ignoreListenerPause = true;
            }
            AudioListener.pause = true;
            Debug.LogWarning("[pause] Local Play match stopped at " + Clock(s_lastRaw) + " ms");
        }
        else
        {
            if (s_heldSince >= 0)
                s_held += s_lastRaw - s_heldSince;
            s_heldSince = -1;
            Time.timeScale = 1f;
            AudioListener.pause = false;
            if (menuSound)
                menuSound.ignoreListenerPause = menuSoundIgnored;
            menuSound = null;
            Debug.LogWarning("[pause] Local Play match goes on at " + Clock(s_lastRaw) + " ms, after " +
                             (Time.unscaledTime - heldAt).ToString("0.0") + " s" + (game ? "" : " (match left)"));
        }
    }
}
