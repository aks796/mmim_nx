// OfflineTest.cs -- debug (Switch port, mmim_nx): a match with no internet, for
// checks in an emulator (Ryubing has no network, and MMIM's rooms are online).
//
// Only when <Application.persistentDataPath>/offline_test.txt exists (the SD
// card's /switch/mmim_nx/ext/offline_test.txt). Its lines:
//
//     level 0                       the map (Enums.NetRoomProperties.Level)
//     character 2                   the player's character (0 Mario, 1 Luigi, 2 Wario...)
//     start 8                       seconds after the Main Menu loads
//     lobby 3                       seconds in the room's lobby before the match
//     players 2                     local play's split screen (LocalPlay), player 2
//     character2 1                  as player 2's character
//     spawn Mushroom,FireFlower     power-ups (Prefabs/Powerup/<name>) put above
//                                   the player 4 s into the match, one every
//                                   0.5 s; each one's Instantiate time is logged
//
// PUN OfflineMode makes the room locally (RaiseEvent and RPCs stay on this
// console), MMIM's own StartGame loads the level, and GameManager runs as with
// a host alone in a room. This is also the first piece of local play.

using System.Collections;
using System.Diagnostics;
using System.IO;
using NSMB.Utils;
using Photon.Pun;
using Photon.Realtime;
using UnityEngine;
using UnityEngine.SceneManagement;
using Debug = UnityEngine.Debug;
using Hashtable = ExitGames.Client.Photon.Hashtable;

public class OfflineTest : MonoBehaviour
{
    int level, character, players = 1, character2 = 1;
    float startAfter = 8f, lobbyFor = 3f;
    string[] spawns = new string[0];
    bool spawned;

    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
    static void Init()
    {
        string path = Path.Combine(Application.persistentDataPath, "offline_test.txt");
        if (!File.Exists(path))
            return;
        var go = new GameObject("OfflineTest");
        DontDestroyOnLoad(go);
        go.AddComponent<OfflineTest>().Parse(File.ReadAllLines(path));
        Debug.LogWarning("[offline] test from " + path);
    }

    void Parse(string[] lines)
    {
        foreach (string raw in lines)
        {
            string[] w = raw.Trim().Split(new[] { ' ' }, 2);
            if (w.Length < 2 || w[0].StartsWith("#"))
                continue;
            switch (w[0])
            {
                case "level": int.TryParse(w[1], out level); break;
                case "character": int.TryParse(w[1], out character); break;
                case "players": int.TryParse(w[1], out players); break;
                case "character2": int.TryParse(w[1], out character2); break;
                case "start": float.TryParse(w[1], out startAfter); break;
                case "lobby": float.TryParse(w[1], out lobbyFor); break;
                case "spawn": spawns = w[1].Split(','); break;
                case "power2": power2 = w[1].Trim(); break;
                case "hold2": hold2 = w[1].Trim(); break; // player 2 picks up this enemy in its shell (Prefabs/Enemy/Koopa)
                case "win2": int.TryParse(w[1], out win2); break;
                case "states": states = w[1].Split(','); break; // players 1 and 2 through these power-ups, 3 s each (how each character looks) // player 2 wins this many seconds in (the room's Wins tally)            // player 2 starts with it, beside player 1, throwing at them
                case "cameras": SplitScreen.TwoCameras = w[1].Trim() == "2"; break; // MMIM's two cameras per player (to compare)
            }
        }
    }

    IEnumerator Start()
    {
        SceneManager.sceneLoaded += (scene, mode) =>
        {
            if (scene.name != "MainMenu" && !spawned)
                StartCoroutine(Spawn());
        };
        yield return new WaitForSecondsRealtime(startAfter);
        var mm = MainMenuManager.Instance;
        if (!mm)
        {
            Debug.LogWarning("[offline] no Main Menu");
            yield break;
        }
        if (mm.errorBox)
            mm.errorBox.SetActive(false); // the failed online login's
        if (players > 1)
        {
            LocalPlay.Begin(players); // (every Local Play starts at the defaults: the test's pick after)
            LocalPlay.SetCharacter(1, character2);
        }
        if (PhotonNetwork.IsConnected)
            PhotonNetwork.Disconnect();
        while (PhotonNetwork.IsConnected)
            yield return null;
        PhotonNetwork.OfflineMode = true;
        yield return null;
        PhotonNetwork.NickName = "Test";
        try
        {
            PhotonNetwork.LocalPlayer.SetCustomProperties(new Hashtable
            {
                [Enums.NetPlayerProperties.Character] = character,
                [Enums.NetPlayerProperties.PlayerColor] = 0,
                [Enums.NetPlayerProperties.Spectator] = false,
            });
            Hashtable props = NetworkUtils.DefaultRoomProperties;
            props[Enums.NetRoomProperties.Level] = level;
            props[Enums.NetRoomProperties.HostName] = "Test";
            PhotonNetwork.CreateRoom("offline", new RoomOptions { MaxPlayers = 4, CustomRoomProperties = props });
        }
        catch (System.Exception e)
        {
            // MMIM's lobby reads online-only things as the room opens (the region)
            Debug.LogWarning("[offline] the room opened with " + e.GetType().Name + ": " + e.Message + "\n" + e.StackTrace);
        }
        yield return new WaitForSecondsRealtime(lobbyFor);
        Debug.LogWarning("[offline] in room: " + PhotonNetwork.InRoom + ", level " + level + ", character " + character);
        if (PhotonNetwork.InRoom)
        {
            try
            {
                MainMenuManager.Instance.StartGame();
            }
            catch (System.Exception e)
            {
                Debug.LogWarning("[offline] StartGame: " + e.GetType().Name + ": " + e.Message);
            }
        }
    }

    string power2, hold2;
    int win2;
    string[] states;

    IEnumerator CycleStates()
    {
        yield return new WaitForSeconds(2f);
        foreach (string name in states)
        {
            if (name.Trim() == "Star")
            {
                for (int slot = 0; slot < 2; slot++)
                {
                    PlayerController p = LocalPlay.PlayerAt(slot);
                    if (p)
                        p.invincible = 3f;
                }
                Debug.LogWarning("[offline] players 1 and 2 are Star (t=" + Time.realtimeSinceStartup.ToString("F1") + ")");
                yield return new WaitForSeconds(3f);
                continue;
            }
            if (!System.Enum.TryParse(name.Trim(), out Enums.PowerupState state))
                continue;
            for (int slot = 0; slot < 2; slot++)
            {
                PlayerController p = LocalPlay.PlayerAt(slot);
                if (p)
                    p.state = state;
            }
            Debug.LogWarning("[offline] players 1 and 2 are " + state + " (t=" + Time.realtimeSinceStartup.ToString("F1") + ")");
            yield return new WaitForSeconds(3f);
        }
    }

    IEnumerator Player2Wins()
    {
        yield return new WaitForSeconds(win2);
        PlayerController p2 = LocalPlay.PlayerAt(1);
        if (!p2)
            yield break;
        p2.stars = GameManager.Instance.starRequirement;
        Debug.LogWarning("[offline] player 2 has " + p2.stars + " stars");
        GameManager.Instance.CheckForWinner();
    }

    // player 2 picks up a shell the way KoopaWalk's pickup does (how they hold it)
    IEnumerator Player2Holds()
    {
        yield return new WaitForSeconds(2f);
        PlayerController p2 = LocalPlay.PlayerAt(1);
        for (float t = 0f; t < 10f && p2 && (p2.dead || p2.Frozen || !p2.onGround); t += 0.25f)
            yield return new WaitForSeconds(0.25f);
        if (!p2)
            yield break;
        GameObject go = PhotonNetwork.InstantiateRoomObject(hold2, p2.body.position + new Vector2(0.6f, 0.3f), Quaternion.identity);
        KoopaWalk koopa = go ? go.GetComponent<KoopaWalk>() : null;
        if (!koopa)
        {
            Debug.LogWarning("[offline] hold2 " + hold2 + ": no shell");
            yield break;
        }
        yield return null;
        koopa.photonView.RPC("EnterShell", RpcTarget.All);
        koopa.photonView.RPC("Pickup", RpcTarget.All, p2.photonView.ViewID);
        p2.photonView.RPC("SetHolding", RpcTarget.All, koopa.photonView.ViewID);
        Debug.LogWarning("[offline] player 2 holds " + hold2 + " at " + koopa.holderOffset.ToString("F3"));
    }

    // player 2 with a power-up, beside player 1 and facing them, using it
    // every 0.7 s for 8 s: does it hit player 1 ([local] ... hit by ...)
    IEnumerator ThrowAtPlayer1()
    {
        yield return new WaitForSeconds(2f);
        PlayerController p1 = LocalPlay.PlayerAt(0), p2 = LocalPlay.PlayerAt(1);
        // after the start's drop-in (players frozen until they land)
        for (float t = 0f; t < 10f && p2 && (p2.dead || p2.Frozen || !p2.onGround); t += 0.25f)
            yield return new WaitForSeconds(0.25f);
        if (!p1 || !p2 || !System.Enum.TryParse(power2, out Enums.PowerupState state))
        {
            Debug.LogWarning("[offline] power2 " + power2 + ": no players or no such power-up");
            yield break;
        }
        p2.state = state;
        p2.body.position = p1.body.position + new Vector2(1.6f, 0.2f);
        p2.facingRight = false;
        Debug.LogWarning("[offline] player 2 is " + state + ", beside player 1");
        yield return new WaitForSeconds(3.5f); // MMIM's prefabs: no power-up use for 3.5 s after spawning (goldTimer)
        for (int i = 0; i < 12; i++)
        {
            yield return new WaitForSeconds(0.9f);
            if (!p1 || !p2)
                yield break;
            // a little further each time: a hammer arcs up and over, a fireball bounces along
            p2.body.position = p1.body.position + new Vector2(1.5f + 0.5f * i, 0.2f);
            p2.facingRight = false;
            int before = FindObjectsOfType<FireballMover>().Length;
            p2.ActivatePowerupAction();
            if (i == 0)
            {
                // what stops a throw (ActivatePowerupAction's conditions): every bool on, every timer running
                var flags = new System.Text.StringBuilder();
                foreach (var f in typeof(PlayerController).GetFields(System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.NonPublic))
                {
                    object v = f.GetValue(p2);
                    if (v is bool b && b)
                        flags.Append(' ').Append(f.Name);
                    else if (v is float x && x > 0f && f.Name.ToLower().Contains("timer"))
                        flags.Append(' ').Append(f.Name).Append('=').Append(x.ToString("F2"));
                }
                Debug.LogWarning("[offline] player 2's flags:" + flags + " Frozen=" + p2.Frozen + " pipe=" + (bool)p2.pipeEntering);
            }
            if (i < 12)
                Debug.LogWarning("[offline] player 2 (" + p2.state + ", dead " + p2.dead + ", knockback " + p2.knockback + ", on ground " + p2.onGround +
                                 ") at " + p2.body.position.ToString("F1") + ", player 1 at " + p1.body.position.ToString("F1") +
                                 ": projectiles " + before + " -> " + FindObjectsOfType<FireballMover>().Length);
        }
    }

    IEnumerator Spawn()
    {
        spawned = true;
        while (!GameManager.Instance || !GameManager.Instance.localPlayer || !GameManager.Instance.started)
            yield return null;
        Debug.LogWarning("[offline] match started");
        if (!string.IsNullOrEmpty(power2))
            StartCoroutine(ThrowAtPlayer1());
        if (win2 > 0)
            StartCoroutine(Player2Wins());
        if (states != null)
            StartCoroutine(CycleStates());
        if (!string.IsNullOrEmpty(hold2))
            StartCoroutine(Player2Holds());
        yield return new WaitForSeconds(4f);
        // to the player's right, a row above the ground, out of reach
        Vector3 origin = GameManager.Instance.localPlayer.transform.position + new Vector3(2.5f, 2.5f, 0f);
        var items = new System.Collections.Generic.List<GameObject>();
        float dx = 0f;
        foreach (string name in spawns)
        {
            Vector3 at = origin + new Vector3(dx, 0f, 0f);
            var sw = Stopwatch.StartNew();
            GameObject item = PhotonNetwork.InstantiateRoomObject("Prefabs/Powerup/" + name.Trim(), at, Quaternion.identity);
            sw.Stop();
            Debug.LogWarning("[offline] spawned " + name + " at " + at + " in " + sw.Elapsed.TotalMilliseconds.ToString("F1") +
                             " ms" + (item ? "" : " (none)"));
            items.Add(item);
            dx += 1.2f;
            yield return new WaitForSeconds(0.5f);
        }
        // what each one is doing: is it there, drawn, where, with what
        for (int round = 0; round < 8; round++)
        {
            yield return new WaitForSeconds(1.5f);
            for (int i = 0; i < items.Count; i++)
            {
                GameObject item = items[i];
                if (!item)
                {
                    Debug.LogWarning("[offline] t+" + (round + 1) * 1.5f + " " + spawns[i] + ": gone");
                    continue;
                }
                var line = new System.Text.StringBuilder();
                line.Append("[offline] t+").Append((round + 1) * 1.5f).Append(' ').Append(spawns[i]).Append(" at ")
                    .Append(item.transform.position.ToString("F2")).Append(" layer ").Append(LayerMask.LayerToName(item.layer));
                foreach (Renderer r in item.GetComponentsInChildren<Renderer>(true))
                {
                    Material m = r.sharedMaterial;
                    line.Append(" | ").Append(r.GetType().Name).Append(' ').Append(r.name)
                        .Append(r.gameObject.activeInHierarchy ? "" : " INACTIVE").Append(r.enabled ? "" : " DISABLED")
                        .Append(r.isVisible ? " visible" : " not-visible")
                        .Append(" sort ").Append(r.sortingLayerName).Append('/').Append(r.sortingOrder)
                        .Append(" bounds ").Append(r.bounds.center.ToString("F2")).Append(r.bounds.size.ToString("F2"))
                        .Append(" mat ").Append(m ? m.name : "none")
                        .Append(m && m.shader ? " shader " + m.shader.name + (m.shader.isSupported ? "" : " UNSUPPORTED") : "");
                    if (r is SpriteRenderer sr)
                        line.Append(" sprite ").Append(sr.sprite ? sr.sprite.name : "none").Append(" a ").Append(sr.color.a.ToString("F2"));
                }
                Debug.LogWarning(line.ToString());
            }
        }
    }
}
