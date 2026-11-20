// PortSkins.cs -- Waluigi and MiiBumm online, each tied to a stand-in.
//
// PC MMIM's character list ends at MiiBumm (5, hidden from its menu) and
// has no Waluigi: a port player sending character 6 would break PC
// players' games, and spawning PlayerWaluigi would show them MMIM's old
// unfinished model. So online, a port-only character is tied to a
// stand-in that PC MMIM knows (user, 2026-11-16: "tied to a specific player
// character so that other players on our builds can read waluigi and
// miibumm"):
//   Waluigi -> Luigi (his suits and colour sets are Luigi's, refitted)
//   MiiBumm -> Mario (his suits are Mario's, with his head)
// The player properties carry both: "C" (MMIM's own key) = the stand-in,
// which is all PC MMIM and older port builds read, and PortC = the real
// character, which this port reads (Utils.GetCharacterIndex -> Resolve).
// The player object is spawned under the stand-in's prefab name, so every
// game in the room can make it; on port builds PhotonNetwork's spawn
// (PortPrefabHook -> PrefabFor) makes the real character's prefab instead.
// The four prefabs share one network layout (one PhotonView observing
// CustomNetworkSerializer), and a player object's PlayerData comes from
// its prefab, so a Waluigi here and a Luigi on PC stay in step.

using ExitGames.Client.Photon;
using NSMB.Utils;
using Photon.Pun;
using Photon.Realtime;
using UnityEngine;

public static class PortSkins
{
    public const string Key = "PortC";

    // Waluigi and MiiBumm are the Switch build's only (user, 2026-11-18:
    // MiiBumm does not want them in the phone and browser builds). Those
    // play like PC MMIM: neither is on offer, a saved pick of either goes
    // online as its stand-in, and a Switch player's shows as the stand-in.
    public static bool PortCharacters => PortPlatform.Switch;

    static readonly string[,] k_standIns = { { "Waluigi", "Luigi" }, { "MiiBumm", "Mario" } };

    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSceneLoad)]
    static void Install()
    {
        PhotonNetwork.PortPrefabHook = PrefabFor;
    }

    static PlayerData[] Characters()
    {
        return GlobalController.Instance ? GlobalController.Instance.characters : null;
    }

    static int IndexOf(string name)
    {
        PlayerData[] chars = Characters();
        if (chars != null)
            for (int i = 0; i < chars.Length; i++)
                if (chars[i] && chars[i].name == name)
                    return i;
        return -1;
    }

    // the character PC MMIM is shown for this one (itself if it has no stand-in)
    public static int StandIn(int character)
    {
        PlayerData[] chars = Characters();
        if (chars == null || character < 0 || character >= chars.Length || !chars[character])
            return character;
        for (int i = 0; i < k_standIns.GetLength(0); i++)
            if (chars[character].name == k_standIns[i, 0])
            {
                int standIn = IndexOf(k_standIns[i, 1]);
                return standIn >= 0 ? standIn : character;
            }
        return character;
    }

    // the properties that make the local player `character`: C the
    // stand-in, PortC the real one (null removes it when there is none)
    public static Hashtable Properties(int character)
    {
        if (!PortCharacters)
            character = StandIn(character);
        int standIn = StandIn(character);
        return new Hashtable
        {
            [Enums.NetPlayerProperties.Character] = standIn,
            [Key] = standIn != character ? (object)character : null,
        };
    }

    // Utils.GetCharacterIndex: a player's real character when their PortC
    // is a character whose stand-in is the C they report, else C
    public static int Resolve(Player player, int reported)
    {
        if (!PortCharacters)
            return reported;
        if (player == null || player.CustomProperties == null || !player.CustomProperties.ContainsKey(Key))
            return reported;
        if (!(player.CustomProperties[Key] is int real))
            return reported;
        PlayerData[] chars = Characters();
        if (chars == null || real < 0 || real >= chars.Length || !chars[real] || real == reported)
            return reported;
        return StandIn(real) == reported ? real : reported;
    }

    // the prefab this player's own object is spawned under: online the
    // stand-in's (every game in the room has it), offline the real one
    public static string SpawnPrefab()
    {
        PlayerData[] chars = Characters();
        int real = Utils.GetCharacterIndex();
        int spawn = PhotonNetwork.OfflineMode ? real : StandIn(real);
        return "Prefabs/" + chars[spawn].prefab;
    }

    // PhotonNetwork's spawn of any networked object: a stand-in's player
    // prefab spawned by a player whose PortC is set becomes the real
    // character's prefab here (online only: in local play every player is
    // this console's and spawns its real prefab by name)
    static string PrefabFor(string prefabName, Player creator)
    {
        if (PhotonNetwork.OfflineMode || creator == null || string.IsNullOrEmpty(prefabName))
            return prefabName;
        PlayerData[] chars = Characters();
        if (chars == null)
            return prefabName;
        int reported = 0;
        Utils.GetCustomProperty<int>(Enums.NetPlayerProperties.Character, out reported, creator.CustomProperties);
        int real = Resolve(creator, reported);
        if (real == reported || reported < 0 || reported >= chars.Length || !chars[reported] || !chars[real])
            return prefabName;
        if (prefabName != "Prefabs/" + chars[reported].prefab)
            return prefabName; // not the player object (a fireball, a frozen cube...)
        Debug.LogWarning("[skins] " + creator.NickName + ": " + chars[reported].name + " here is " + chars[real].name);
        return "Prefabs/" + chars[real].prefab;
    }
}
