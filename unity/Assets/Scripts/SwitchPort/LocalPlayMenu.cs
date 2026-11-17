// LocalPlayMenu.cs -- Local Play in MMIM's own menus (Switch port, mmim_nx).
//
// Main Menu: MMIM's "Play Game" button becomes two of the same, "Online
// Play" (MMIM's lobby list, as before) and "Local Play". Local Play opens the
// console's controller screen for 2 to 4 players (closing it stays on the
// main menu), leaves the online server and makes a room in PUN's offline
// mode (LocalPlay), so MMIM's own room screen comes up with all of its rules
// (map, stars, coins, lives, time limit).
//
// In a local room that screen shows what local play needs: the player list
// lists Player 1 to 4 in their colours (PlayerListHandler) and every player
// has a Player Settings box (character and colours): with two players MMIM's
// own and a copy where the chat was, with three or four a smaller one each,
// two by two (MakeBoxes). Every player moves their own cursor in their own
// box; player 1's also sets the rules (LocalCursors). Nobody looks the same:
// players start as different characters, and a character somebody already
// is comes in the next colour, as in Smash (LocalPlay.MakeDistinct). The
// chosen map shows under the rules (its picture from the maps list, and its
// name). Holding L+R opens the controller screen again (the loader): players
// join or leave. No chat, spectating or server settings. Leaving the room
// (Back) ends local play and goes online again; a finished or quit match
// comes back to the local room.

using System.Collections;
using NSMB.Utils;
using UnityEngine.EventSystems;
using Photon.Pun;
using Photon.Realtime;
using TMPro;
using UnityEngine;
using UnityEngine.UI;
using Hashtable = ExitGames.Client.Photon.Hashtable;

public static class LocalPlayMenu
{
    static GameObject s_p1Box;            // MMIM's own Player Settings box (player 1)
    static GameObject s_box;              // player 2's box with two players (this menu scene's)
    static TMP_Dropdown s_boxCharacter;
    static ColorChooser s_boxChooser;
    static MainMenuManager s_boxOwner;
    static string s_p1Header;             // the Player Settings header as MMIM wrote it
    static bool s_starting, s_leaving, s_subscribed;

    // --- the main menu: "Online Play" and "Local Play" ----------------------------------------

    // MainMenuManager.Start, first thing
    public static void Attach(MainMenuManager mm)
    {
        if (!s_subscribed)
        {
            LocalPlay.PlayersChanged += OnPlayersChanged;
            s_subscribed = true;
        }
        try
        {
            PortNote(mm);
            // Local Play is the console's (its controller screen, a
            // controller per player): the phone and web builds play online
            if (PortPlatform.Switch || Application.isEditor)
                SplitPlayButton(mm);
        }
        catch (System.Exception e)
        {
            Debug.LogWarning("[local] main menu: " + e);
        }
    }

    // the green "!" note names the platform of this build (the scene's text
    // says "This Nintendo Switch port of MiiBumm's Mega Item Mod...")
    static void PortNote(MainMenuManager mm)
    {
        if (PortPlatform.Switch || !mm.remindBox1)
            return;
        foreach (TMP_Text t in mm.remindBox1.GetComponentsInChildren<TMP_Text>(true))
            if (t.text.Contains("Nintendo Switch port"))
                t.text = t.text.Replace("This Nintendo Switch port", PortPlatform.Phone ? "This Android port" : "This web port");
    }

    static void SplitPlayButton(MainMenuManager mm)
    {
        Transform play = Find(mm.mainMenu.transform, "BtnPlay");
        if (!play || play.parent.Find("BtnLocalPlay"))
            return;
        var rect = (RectTransform)play;
        float left = rect.anchorMin.x, right = rect.anchorMax.x, mid = (left + right) * 0.5f, gap = 0.01f;

        GameObject local = Object.Instantiate(play.gameObject, play.parent);
        local.name = "BtnLocalPlay";
        local.transform.SetSiblingIndex(play.GetSiblingIndex() + 1);
        var localRect = (RectTransform)local.transform;

        rect.anchorMax = new Vector2(mid - gap * 0.5f, rect.anchorMax.y);
        localRect.anchorMin = new Vector2(mid + gap * 0.5f, localRect.anchorMin.y);
        SetLabel(play, "Online Play");
        SetLabel(local.transform, "Local Play");
        // PipeButton keeps the anchor it saw at Start: again with the new ones
        play.GetComponent<PipeButton>()?.Start();
        local.GetComponent<PipeButton>()?.Start();

        Button button = local.GetComponent<Button>();
        button.onClick = new Button.ButtonClickedEvent(); // not "Play Game"'s lobby list
        button.onClick.AddListener(() =>
        {
            if (!s_starting)
                mm.StartCoroutine(StartLocal(mm));
        });
    }

    // the label over the whole (now half as wide) button at "Play Game"'s
    // size: MMIM's label box is 360 units narrower than the button, which
    // left a half button's label a sliver (auto-sized tiny, wrapped)
    static void SetLabel(Transform button, string text)
    {
        TMP_Text label = button.GetComponentInChildren<TMP_Text>(true);
        if (!label)
            return;
        float size = label.enableAutoSizing ? Mathf.Max(label.fontSize, 32f) : label.fontSize;
        var rt = label.rectTransform;
        rt.anchorMin = new Vector2(0f, rt.anchorMin.y);
        rt.anchorMax = new Vector2(1f, rt.anchorMax.y);
        rt.offsetMin = new Vector2(0f, rt.offsetMin.y);
        rt.offsetMax = new Vector2(0f, rt.offsetMax.y);
        label.margin = Vector4.zero;
        label.enableAutoSizing = false;
        label.enableWordWrapping = false;
        label.fontSize = size;
        label.text = text;
    }

    static Transform Find(Transform root, string name)
    {
        foreach (Transform t in root.GetComponentsInChildren<Transform>(true))
            if (t.name == name)
                return t;
        return null;
    }

    // --- into a local room ----------------------------------------------------------------------

    static IEnumerator StartLocal(MainMenuManager mm)
    {
        s_starting = true;
        mm.ConfirmSound();
        // who plays: the console's controller screen, 1 to 4 players
        int players = 0;
        yield return LocalPlay.PickPlayers(n => players = n);
        if (players < 1)
        {
            s_starting = false; // closed with B: still the main menu, still online
            yield break;
        }
        LocalPlay.Begin(players); // a gamepad each
        // off the online server first (offline mode cannot start while connected)
        if (PhotonNetwork.IsConnected && !PhotonNetwork.OfflineMode)
        {
            PhotonNetwork.Disconnect();
            float waited = 0f;
            while (PhotonNetwork.IsConnected && waited < 5f)
            {
                waited += Time.unscaledDeltaTime;
                yield return null;
            }
        }
        PhotonNetwork.OfflineMode = true;
        yield return null;
        try
        {
            Settings settings = Singleton<Settings>.Instance;
            PhotonNetwork.NickName = settings.nickname;
            Hashtable mine = PortSkins.Properties(settings.character); // C (a stand-in) and PortC
            mine[Enums.NetPlayerProperties.PlayerColor] = settings.skin;
            mine[Enums.NetPlayerProperties.Spectator] = false;
            mine[Enums.NetPlayerProperties.Ping] = 0;
            PhotonNetwork.LocalPlayer.SetCustomProperties(mine);
            Hashtable props = NetworkUtils.DefaultRoomProperties;
            props[Enums.NetRoomProperties.HostName] = settings.nickname;
            PhotonNetwork.CreateRoom("Local Play", new RoomOptions
            {
                MaxPlayers = (byte)LocalPlay.MaxPlayers,
                IsVisible = false,
                CustomRoomProperties = props,
            });
            Debug.LogWarning("[local] local room made: " + PhotonNetwork.InRoom);
        }
        catch (System.Exception e)
        {
            Debug.LogWarning("[local] the local room: " + e);
        }
        s_starting = false;
    }

    // --- the room screen ------------------------------------------------------------------------

    // MainMenuManager.EnterRoom, last thing: the screen for a local or an online room
    public static void OnEnterRoom(MainMenuManager mm)
    {
        try
        {
            Apply(mm, LocalPlay.Active);
        }
        catch (System.Exception e)
        {
            Debug.LogWarning("[local] room screen: " + e);
        }
    }

    static void Apply(MainMenuManager mm, bool local)
    {
        if (local)
            LocalPlay.AnnounceGamepads();
        LocalPlay.InRoom(local); // L+R there: players join or leave
        // the header
        if (local && mm.lobbyText)
        {
            TMP_Text header = mm.lobbyText.GetComponent<TMP_Text>();
            if (header)
                header.text = "Local Play";
        }
        CharacterOptions(mm, local);
        // player 1's palette swatches in their character's colours: a Local
        // Play starts player 1 as Mario (LocalPlay.StartAsDefaults) and ends
        // on their online pick, and the palette was made for the one before
        if (mm.colorManager)
            mm.colorManager.ChangeCharacter(Utils.GetCharacterData());
        // no chat: the other players' settings where it was
        Transform chat = mm.chatTextField ? mm.chatTextField.transform.parent : null;
        if (chat)
            chat.gameObject.SetActive(!local);
        // no spectating, no server settings
        if (mm.spectateToggle)
            mm.spectateToggle.transform.parent.gameObject.SetActive(!local);
        if (mm.changePlayersSlider)
        {
            Transform maxPlayers = mm.changePlayersSlider.transform.parent;
            maxPlayers.gameObject.SetActive(!local);
            Transform serverHeader = maxPlayers.parent.Find("LblServerSettings");
            if (serverHeader)
                serverHeader.gameObject.SetActive(!local);
        }
        if (mm.privateToggleRoom)
            mm.privateToggleRoom.transform.parent.gameObject.SetActive(!local);
        // player 1's box says so
        TMP_Text p1Header = PlayerSettingsHeader(mm.characterDropdown.transform);
        if (p1Header)
        {
            if (s_boxOwner != mm)
                s_p1Header = p1Header.text;
            p1Header.text = local ? "--- Player 1 Settings ---" : s_p1Header;
        }
        if (local && (s_boxOwner != mm || !s_box))
            MakeBoxes(mm, chat);
        s_boxOwner = mm;
        MapCard(mm, local);
        WinsBox(local);
        if (local)
        {
            LocalPlay.AssignDefaults(); // everyone a different character
            Layout();
            // player 1's cursor starts on their own character, as everyone's
            // does (MMIM selects an invisible 0x0 button there)
            GameObject first = Quarters && s_cards[0] != null && s_cards[0].character
                ? s_cards[0].character.gameObject : mm.characterDropdown.gameObject;
            if (EventSystem.current && first.activeInHierarchy)
                EventSystem.current.SetSelectedGameObject(first);
        }
        else
        {
            // an online room: MMIM's own box alone
            if (s_p1Box)
                s_p1Box.SetActive(true);
            if (s_box)
                s_box.SetActive(false);
            foreach (Card c in s_cards)
                if (c != null && c.box)
                    c.box.SetActive(false);
        }
    }

    static TMP_Text PlayerSettingsHeader(Transform characterDropdown)
    {
        // CharacterDropdown -> LblCharacter -> Content -> LblPlayerSettings
        Transform content = characterDropdown.parent ? characterDropdown.parent.parent : null;
        Transform header = content ? content.Find("LblPlayerSettings") : null;
        return header ? header.GetComponent<TMP_Text>() : null;
    }

    // The players' settings, in copies of MMIM's Player Settings box:
    //   two players     player 1's box (MMIM's own) and player 2's under it
    //                   where the chat was, the same size
    //   three or four   a smaller box per player, two by two over the same
    //                   space ("--- Player 3 ---", the character dropdown
    //                   across the box, Colors); player 1's sets MMIM's own
    //                   choice (its dropdown and colour, hidden meanwhile)
    class Card
    {
        public GameObject box;
        public TMP_Dropdown character;
        public ColorChooser chooser;
    }

    static readonly Card[] s_cards = new Card[LocalPlay.MaxPlayers];

    public static bool HasBoxes => s_box;

    static bool Quarters => LocalPlay.Players > 2 && s_cards[0] != null;

    // player 2-4's box in the local room now (null for player 1, whose
    // settings are part of the rest of the screen)
    public static GameObject BoxOf(int slot)
    {
        if (slot <= 0 || slot >= LocalPlay.Players || !LocalPlay.Active || !s_box)
            return null;
        if (!Quarters)
            return slot == 1 ? s_box : null;
        Card card = s_cards[slot];
        return card != null ? card.box : null;
    }

    static void MakeBoxes(MainMenuManager mm, Transform chat)
    {
        // the Player Settings box: CharacterDropdown -> LblCharacter -> Content -> Viewport
        s_p1Box = mm.characterDropdown.transform.parent.parent.parent.gameObject;
        s_p1Box.SetActive(true);
        Transform panel = chat ? chat.parent : s_p1Box.transform.parent;

        // player 2's box (two players): under player 1's, at the top of where the chat was
        Card p2 = CopyBox(mm, "Player2Settings", panel);
        s_box = p2.box;
        s_boxCharacter = p2.character;
        s_boxChooser = p2.chooser;
        if (chat)
        {
            var c1 = new Vector3[4];
            var c2 = new Vector3[4];
            ((RectTransform)s_p1Box.transform).GetWorldCorners(c1);
            ((RectTransform)chat).GetWorldCorners(c2);
            s_box.transform.position += new Vector3(c2[1].x - c1[1].x, c2[1].y - c1[1].y, 0f);
        }
        TMP_Text header = HeaderOf(s_box);
        if (header)
            header.text = "--- Player 2 Settings ---";
        if (s_boxChooser)
            s_boxChooser.localSlot = 1; // before its Start
        if (s_boxCharacter)
            s_boxCharacter.onValueChanged.AddListener(option => CharacterPicked(mm, 1, CharacterAt(option)));

        // three or four players: a box each, two by two over both
        Rect region = WorldRect(s_p1Box.transform);
        Rect r2 = WorldRect(s_box.transform);
        region = Rect.MinMaxRect(Mathf.Min(region.xMin, r2.xMin), Mathf.Min(region.yMin, r2.yMin),
                                 Mathf.Max(region.xMax, r2.xMax), Mathf.Max(region.yMax, r2.yMax));
        for (int slot = 0; slot < LocalPlay.MaxPlayers; slot++)
            s_cards[slot] = MakeCard(mm, panel, slot, region);
        Debug.LogWarning("[local] players' settings boxes made");
    }

    // a copy of MMIM's Player Settings box, for another player
    static Card CopyBox(MainMenuManager mm, string name, Transform parent)
    {
        GameObject box = Object.Instantiate(s_p1Box, s_p1Box.transform.parent);
        box.name = name;
        box.transform.SetParent(parent, true);
        // its controls' ScrollRectRedirection passes touch drags to the
        // settings' scroll view, which the copy is not in (Start threw
        // IndexOutOfRangeException looking for it)
        foreach (ScrollRectRedirection r in box.GetComponentsInChildren<ScrollRectRedirection>(true))
            Object.DestroyImmediate(r);
        Transform spectate = Find(box.transform, "LblSpectate");
        if (spectate)
            Object.DestroyImmediate(spectate.gameObject);
        // the copy carried player 1's colour buttons (made by its chooser's
        // Start; not in its list, so picking one gave index -1): only the
        // template and blocker stay, its own Start makes the player's
        Transform palette = Find(box.transform, "Palette");
        if (palette)
        {
            var made = new System.Collections.Generic.List<GameObject>();
            foreach (Transform b in palette)
                if (b.name != "ColorTemplate" && b.name != "Blocker")
                    made.Add(b.gameObject);
            foreach (GameObject g in made)
                Object.DestroyImmediate(g);
        }
        var card = new Card { box = box, chooser = box.GetComponentInChildren<ColorChooser>(true) };
        Transform ddT = Find(box.transform, "CharacterDropdown");
        card.character = ddT ? ddT.GetComponent<TMP_Dropdown>() : null;
        if (card.character)
        {
            card.character.onValueChanged = new TMP_Dropdown.DropdownEvent(); // not player 1's SwapCharacter
            card.character.ClearOptions();
            card.character.AddOptions(mm.characterDropdown.options);
        }
        box.SetActive(false);
        return card;
    }

    static Card MakeCard(MainMenuManager mm, Transform panel, int slot, Rect region)
    {
        Card card = CopyBox(mm, "Player" + (slot + 1) + "Box", panel);
        if (card.chooser)
            card.chooser.localSlot = slot; // before its Start; 0: MMIM's own colour
        // a quarter of the space, a little apart
        float gap = region.width * 0.02f;
        float w = (region.width - gap) * 0.5f, h = (region.height - gap) * 0.5f;
        var rt = (RectTransform)card.box.transform;
        Vector3 scale = rt.parent ? rt.parent.lossyScale : Vector3.one;
        rt.anchorMin = rt.anchorMax = new Vector2(0.5f, 0.5f);
        rt.pivot = new Vector2(0f, 1f);
        rt.sizeDelta = new Vector2(w / scale.x, h / scale.y);
        rt.position = new Vector3(region.xMin + (slot % 2) * (w + gap), region.yMax - (slot / 2) * (h + gap), rt.position.z);
        // its rows as wide as the box now
        Transform content = card.box.transform.Find("Content");
        HorizontalOrVerticalLayoutGroup group = content ? content.GetComponent<HorizontalOrVerticalLayoutGroup>() : null;
        if (group)
        {
            group.childControlWidth = true;
            group.childForceExpandWidth = true;
        }
        TMP_Text header = HeaderOf(card.box);
        if (header)
        {
            header.text = "--- " + LocalPlay.NameOf(slot) + " ---";
            ShrinkToFit(header);
        }
        // the character dropdown across the box (no room for "Character:" beside it)
        if (card.character)
        {
            TMP_Text label = card.character.transform.parent.GetComponent<TMP_Text>();
            if (label)
                label.text = "";
            // (MMIM's right margin, 2.5%, on both sides)
            var drt = (RectTransform)card.character.transform;
            float margin = 1f - drt.anchorMax.x;
            drt.anchorMin = new Vector2(margin, drt.anchorMin.y);
            drt.offsetMin = new Vector2(0f, drt.offsetMin.y);
            drt.offsetMax = new Vector2(0f, drt.offsetMax.y);
            if (slot == 0)
                card.character.onValueChanged.AddListener(index =>
                {
                    // MMIM's own dropdown: its SwapCharacter sets the choice, the voice and its colours
                    mm.characterDropdown.value = index; // (SwapCharacter: CharacterChanged)
                    Layout();
                });
            else
                card.character.onValueChanged.AddListener(option => CharacterPicked(mm, slot, CharacterAt(option)));
        }
        return card;
    }

    static TMP_Text HeaderOf(GameObject box)
    {
        Transform content = box.transform.Find("Content");
        Transform header = content ? content.Find("LblPlayerSettings") : null;
        return header ? header.GetComponent<TMP_Text>() : null;
    }

    // the header's size, smaller only if it must
    static void ShrinkToFit(TMP_Text text)
    {
        if (text.enableAutoSizing)
            return;
        text.fontSizeMax = text.fontSize;
        text.fontSizeMin = text.fontSize * 0.6f;
        text.enableAutoSizing = true;
        text.enableWordWrapping = false;
    }

    static Rect WorldRect(Transform t)
    {
        var c = new Vector3[4];
        ((RectTransform)t).GetWorldCorners(c);
        return Rect.MinMaxRect(c[0].x, c[0].y, c[2].x, c[2].y);
    }

    static void CharacterPicked(MainMenuManager mm, int slot, int index)
    {
        LocalPlay.SetCharacter(slot, index);
        PlayerData data = LocalPlay.CharacterData(slot);
        mm.sfx.PlayOneShot(Enums.Sounds.Player_Voice_Selected.GetClip(data));
        LocalPlay.MakeDistinct(slot); // somebody is that character already: the next colour
        Layout();
    }

    // the boxes for the players there are now
    static void Layout()
    {
        if (!s_box)
            return;
        int players = Mathf.Clamp(LocalPlay.Players, 1, LocalPlay.MaxPlayers);
        bool quarters = players > 2 && s_cards[0] != null;
        if (s_p1Box)
            s_p1Box.SetActive(!quarters);
        s_box.SetActive(!quarters && players > 1); // alone: player 1's box only
        if (s_boxCharacter)
            s_boxCharacter.SetValueWithoutNotify(OptionOf(LocalPlay.CharacterOf(1)));
        if (s_boxChooser)
            s_boxChooser.ShowSlot(1);
        if (s_boxOwner && s_boxOwner.colorManager)
            s_boxOwner.colorManager.ShowSlot(0); // its palette opens on player 1's colour now
        for (int slot = 0; slot < s_cards.Length; slot++)
        {
            Card card = s_cards[slot];
            if (card == null)
                continue;
            card.box.SetActive(quarters && slot < players);
            FitCaption(card.character);
            if (card.character)
                card.character.SetValueWithoutNotify(OptionOf(LocalPlay.CharacterOf(slot)));
            if (card.chooser)
                card.chooser.ShowSlot(slot);
        }
        RefreshBox();
    }

    // a character box's name kept left of its arrow: in a quarter's narrower
    // box MMIM's label ran under the "v" ("Toadettev"); it shrinks to fit
    static void FitCaption(TMP_Dropdown d)
    {
        TMP_Text label = d ? d.captionText : null;
        var arrow = d ? d.transform.Find("Text (TMP)") as RectTransform : null;
        if (!label || !arrow)
            return;
        RectTransform lr = label.rectTransform;
        float labelRight = lr.localPosition.x + lr.rect.xMax, arrowLeft = arrow.localPosition.x + arrow.rect.xMin;
        float over = labelRight - (arrowLeft - 6f);
        if (over > 0f)
        {
            lr.offsetMax -= new Vector2(over, 0f);
            lr.offsetMin -= new Vector2(over * 0.5f, 0f); // (still centred in the box's text area)
        }
        label.enableWordWrapping = false;
        label.enableAutoSizing = true;
        label.fontSizeMin = Mathf.Min(label.fontSizeMin, 8f);
    }

    // L+R in the local room: players joined or left
    static void OnPlayersChanged()
    {
        if (!LocalPlay.Active || !s_boxOwner)
            return;
        Layout();
        PlayerListHandler list = Object.FindObjectOfType<PlayerListHandler>();
        if (list)
            list.PopulatePlayerEntries(false);
    }

    // ColorChooser (a local player's box): a colour picked (one that
    // somebody with the same character has: the next one)
    public static void SetLocalColor(int slot, int index)
    {
        LocalPlay.SetSkin(slot, index);
        LocalPlay.MakeDistinct(slot);
        Layout();
    }

    // ColorChooser: player 1's colour set (MMIM's own), its box shows it too
    public static void Refresh()
    {
        if (!LocalPlay.Active || !s_boxOwner)
            return;
        LocalPlay.MakeDistinct(0);
        Layout();
    }

    // MainMenuManager.SwapCharacter: player 1's character picked
    public static void CharacterChanged()
    {
        if (!LocalPlay.Active || !s_boxOwner || !PhotonNetwork.InRoom)
            return;
        LocalPlay.MakeDistinct(0);
        Layout();
    }

    // each colour button shows its player's colours (or the X for none), as MMIM's does
    static void RefreshBox()
    {
        if (s_boxChooser)
            RefreshColors(s_boxChooser.transform, 1);
        for (int slot = 0; slot < s_cards.Length; slot++)
            if (s_cards[slot] != null && s_cards[slot].chooser)
                RefreshColors(s_cards[slot].chooser.transform, slot);
    }

    static void RefreshColors(Transform primary, int slot)
    {
        Transform image = primary.Find("Image");
        Transform disabled = primary.Find("Disabled");
        int skin = LocalPlay.SkinOf(slot);
        if (image)
            image.gameObject.SetActive(skin != 0);
        if (disabled)
            disabled.gameObject.SetActive(skin == 0);
        if (skin != 0 && image)
        {
            PlayerColors colors = Singleton<GlobalController>.Instance.skins[skin].GetPlayerColors(LocalPlay.CharacterData(slot));
            Image overall = image.Find("Overall") ? image.Find("Overall").GetComponent<Image>() : null;
            Image shirt = image.Find("Shirt") ? image.Find("Shirt").GetComponent<Image>() : null;
            if (overall)
                overall.color = colors.overallsColor;
            if (shirt)
                shirt.color = colors.hatColor;
        }
    }

    // --- the player tabs ----------------------------------------------------------------------------

    // MMIM's player list is made for an online room of up to ten; local play
    // lists two to four, small in a tall empty column. There the list reaches
    // over to the players' boxes and its tabs are drawn as large as that
    // width allows (the list's own layout kept: its content scaled up from
    // its top left corner). The online room gets MMIM's list back.
    static RectTransform s_tabList;
    static Vector2 s_tabListOffsetMax;

    public static void SizePlayerTabs(GameObject contentPane)
    {
        var content = contentPane ? contentPane.transform as RectTransform : null;
        RectTransform view = content ? content.parent as RectTransform : null;
        RectTransform list = view ? view.parent as RectTransform : null;
        if (!list)
            return;
        if (s_tabList != list)
        {
            s_tabList = list;
            s_tabListOffsetMax = list.offsetMax;
        }
        list.offsetMax = s_tabListOffsetMax;
        content.localScale = Vector3.one;
        content.sizeDelta = Vector2.zero;
        if (!LocalPlay.Active)
            return;
        GameObject first = Quarters && s_cards[0] != null ? s_cards[0].box : s_p1Box;
        if (!first || !first.activeInHierarchy || !list.parent)
            return;
        float unit = Mathf.Max(list.parent.lossyScale.x, 1e-6f); // world size of one of the list's parent's units
        float widen = (WorldRect(first.transform).xMin - 8f * unit - WorldRect(list).xMax) / unit;
        if (widen > 0f)
            list.offsetMax = s_tabListOffsetMax + new Vector2(widen, 0f);
        var grid = content.GetComponent<GridLayoutGroup>();
        float cell = grid ? grid.cellSize.x : 135f, pad = grid ? grid.padding.left : 10f;
        float w = view.rect.width, h = view.rect.height;
        float k = Mathf.Clamp((w - 2f) / (cell + 2f * pad), 1f, 1.5f);
        content.localScale = new Vector3(k, k, 1f);
        content.sizeDelta = new Vector2(w / k - w, h / k - h); // laid out at the list's width once scaled
    }

    // --- a player's colour palette, inside their box ---------------------------------------------

    // MMIM's palette opens under the Colors button at full size, six
    // colours a row: in the local room it covered the next player's box (3-4
    // players) and ran off the bottom of the screen (the lower boxes). There
    // it is laid out to the box's shape (as many colours a row as fill it)
    // and fitted inside the box, so every player's popup stays in their own
    // part of the screen. Online rooms keep MMIM's.
    static readonly System.Collections.Generic.Dictionary<RectTransform, (Vector3 scale, Vector2 position, int columns)>
        s_paletteOriginal = new System.Collections.Generic.Dictionary<RectTransform, (Vector3, Vector2, int)>();

    public static void FitPalette(ColorChooser chooser)
    {
        var palette = chooser ? chooser.Palette as RectTransform : null;
        if (!palette)
            return;
        var grid = palette.GetComponent<GridLayoutGroup>();
        if (!s_paletteOriginal.TryGetValue(palette, out var original))
            s_paletteOriginal[palette] = original = (palette.localScale, palette.anchoredPosition, grid ? grid.constraintCount : 0);
        palette.localScale = original.scale;
        palette.anchoredPosition = original.position;
        if (grid && original.columns > 0)
            grid.constraintCount = original.columns;
        RectTransform box = LocalPlay.Active ? BoxRectOf(chooser.transform) : null;
        if (!box)
        {
            LayoutRebuilder.ForceRebuildLayoutImmediate(palette);
            return;
        }
        Rect b = WorldRect(box);
        // under the box's "--- Player N ---" header: whose box it is stays shown
        TMP_Text header = HeaderOf(box.gameObject);
        if (header)
        {
            Rect h = WorldRect(header.transform);
            if (h.yMin > b.yMin && h.yMin < b.yMax)
                b = Rect.MinMaxRect(b.xMin, b.yMin, b.xMax, h.yMin);
        }
        int colours = 0;
        foreach (Transform t in palette)
            if (t.gameObject.activeSelf && t.GetComponent<Button>())
                colours++;
        if (grid && colours > 0 && grid.constraint == GridLayoutGroup.Constraint.FixedColumnCount)
            grid.constraintCount = Mathf.Clamp(Mathf.CeilToInt(Mathf.Sqrt(colours * b.width / Mathf.Max(b.height, 1e-4f))), 1, colours);
        LayoutRebuilder.ForceRebuildLayoutImmediate(palette);
        Rect p = WorldRect(palette);
        float k = Mathf.Min(1f, b.width * 0.94f / Mathf.Max(p.width, 1e-4f), b.height * 0.94f / Mathf.Max(p.height, 1e-4f));
        palette.localScale = original.scale * k;
        p = WorldRect(palette);
        palette.position += (Vector3)(b.center - p.center);
    }

    // the local player's box something is in (null: not a player's box)
    static RectTransform BoxRectOf(Transform t)
    {
        foreach (Card c in s_cards)
            if (c != null && c.box && c.box.activeInHierarchy && t.IsChildOf(c.box.transform))
                return (RectTransform)c.box.transform;
        if (s_box && s_box.activeInHierarchy && t.IsChildOf(s_box.transform))
            return (RectTransform)s_box.transform;
        if (s_p1Box && s_p1Box.activeInHierarchy && t.IsChildOf(s_p1Box.transform))
            return (RectTransform)s_p1Box.transform;
        return null;
    }

    // --- the map, under the rules ----------------------------------------------------------------

    // A local room has no server settings, which left the Game Settings box
    // half empty: the chosen map shows there, as its picture in the maps list
    // (the same framed square) with its name under it in the box's lettering.
    // Touching it opens the maps as "Map Selection" does.
    static GameObject s_mapCard;
    static Image s_mapImage;    // the picture's place (touch opens the maps)
    static GameObject s_mapShown; // a copy of the map's button in the list: its frame and picture
    static TMP_Text s_mapName;
    const float MapCardHeight = 92f, MapPictureShare = 0.76f;

    // --- the characters on offer ----------------------------------------------------------------
    // MMIM's character menu lists five (Mario to Toadette). Its list of
    // characters also has MiiBumm (a model, voices, icons; kept out of the
    // menu) and Waluigi (the same, left out of the list: added at the end,
    // GlobalController.prefab), so the five keep their numbers online, where
    // other players' MMIM knows six. Every character is on offer, local and
    // online (each one's name and its "ready" picture, as MMIM's entries):
    // online, Waluigi and MiiBumm are tied to stand-ins PC MMIM knows,
    // Luigi and Mario, and other port players see them as they are
    // (PortSkins).

    static System.Collections.Generic.List<TMP_Dropdown.OptionData> s_onlineOptions;

    // The menus' order: MMIM's five, Waluigi, then MiiBumm, the mod's own
    // character, last as the guest of honour (user, 2026-11-16). Only the
    // menus change: each character keeps its number (MiiBumm's 5 is PC
    // MMIM's too), so an entry's position and its character differ for the
    // last two. CharacterAt: menu entry -> character; OptionOf: back.
    static int[] s_order;

    static int[] Order()
    {
        PlayerData[] chars = GlobalController.Instance ? GlobalController.Instance.characters : null;
        int n = chars != null ? chars.Length : 0;
        if (s_order != null && s_order.Length == n)
            return s_order;
        var order = new System.Collections.Generic.List<int>();
        int miibumm = -1;
        for (int i = 0; i < n; i++)
        {
            if (chars[i] && chars[i].name == "MiiBumm")
                miibumm = i;
            else
                order.Add(i);
        }
        if (miibumm >= 0)
            order.Add(miibumm);
        s_order = order.ToArray();
        return s_order;
    }

    public static int CharacterAt(int option)
    {
        int[] order = Order();
        return option >= 0 && option < order.Length ? order[option] : option;
    }

    public static int OptionOf(int character)
    {
        int at = System.Array.IndexOf(Order(), character);
        return at >= 0 ? at : character;
    }

    static void CharacterOptions(MainMenuManager mm, bool local)
    {
        TMP_Dropdown menu = mm.characterDropdown;
        PlayerData[] chars = GlobalController.Instance ? GlobalController.Instance.characters : null;
        if (!menu || chars == null)
            return;
        if (s_onlineOptions == null || s_boxOwner != mm)
            s_onlineOptions = new System.Collections.Generic.List<TMP_Dropdown.OptionData>(menu.options);
        int online = s_onlineOptions.Count;
        var options = new System.Collections.Generic.List<TMP_Dropdown.OptionData>(s_onlineOptions);
        // every character, online too: Waluigi and MiiBumm go online behind
        // their stand-ins (PortSkins), so PC MMIM sees Luigi and Mario
        for (int at = online; at < chars.Length; at++)
        {
            int i = CharacterAt(at);
            options.Add(chars[i] ? new TMP_Dropdown.OptionData(chars[i].name, chars[i].readySprite)
                                 : new TMP_Dropdown.OptionData("?"));
        }
        if (menu.options.Count != options.Count)
        {
            menu.ClearOptions();
            menu.AddOptions(options);
        }
        foreach (TMP_Dropdown other in OtherCharacterMenus())
            if (other && other.options.Count != options.Count)
            {
                other.ClearOptions();
                other.AddOptions(options);
            }
        menu.SetValueWithoutNotify(OptionOf(Utils.GetCharacterIndex()));
        menu.RefreshShownValue();
    }

    static System.Collections.Generic.IEnumerable<TMP_Dropdown> OtherCharacterMenus()
    {
        if (s_boxCharacter)
            yield return s_boxCharacter;
        foreach (Card c in s_cards)
            if (c != null && c.character)
                yield return c.character;
    }

    // --- the wins table, under the player tabs (WinsBoard) -----------------------------------

    static GameObject s_wins;

    // the lower player box (two players: player 2's; three or four: player 3's quarter)
    public static bool LowerBoxRect(out Rect r)
    {
        r = default;
        GameObject box = Quarters && s_cards[2] != null ? s_cards[2].box : s_box;
        if (!box || !box.activeInHierarchy)
            return false;
        r = WorldRect(box.transform);
        return true;
    }

    static void WinsBox(bool local)
    {
        if (!local)
        {
            if (s_wins)
                s_wins.SetActive(false);
            return;
        }
        if (!s_wins)
            MakeWinsBox();
        if (s_wins)
            s_wins.SetActive(true);
    }

    static void MakeWinsBox()
    {
        PlayerListHandler list = Object.FindObjectOfType<PlayerListHandler>(true);
        if (!s_p1Box || !list)
            return;
        Transform content = s_p1Box.transform.Find("Content");
        Transform label = content ? Find(content, "LblCharacter") : null;
        TMP_Text header = HeaderOf(s_p1Box);
        if (!label || !header)
        {
            Debug.LogWarning("[local] no wins box: the settings box's header or labels are not where expected");
            return;
        }
        // the room's own box: MMIM's Player Settings panel, emptied
        GameObject box = Object.Instantiate(s_p1Box, s_p1Box.transform.parent);
        box.name = "WinsBox";
        box.transform.SetParent(list.transform.parent, true);
        Transform boxContent = box.transform.Find("Content");
        TMP_Text boxHeader = HeaderOf(box);
        // its rows: a "Character:" label without its dropdown
        GameObject rowsGo = Object.Instantiate(label.gameObject, boxContent);
        rowsGo.name = "Rows";
        for (int i = rowsGo.transform.childCount - 1; i >= 0; i--)
            Object.DestroyImmediate(rowsGo.transform.GetChild(i).gameObject);
        var drop = new System.Collections.Generic.List<GameObject>();
        foreach (Transform c in boxContent)
            if (c != boxHeader.transform && c != rowsGo.transform)
                drop.Add(c.gameObject);
        foreach (GameObject g in drop)
            Object.DestroyImmediate(g);
        foreach (Component c in boxContent.GetComponents<Component>())
            if (c is LayoutGroup || c is ContentSizeFitter)
                Object.DestroyImmediate(c);
        foreach (Component c in rowsGo.GetComponents<Component>())
            if (c is LayoutElement)
                Object.DestroyImmediate(c);
        var crt = (RectTransform)boxContent;
        crt.anchorMin = Vector2.zero;
        crt.anchorMax = Vector2.one;
        crt.offsetMin = crt.offsetMax = Vector2.zero;
        crt.pivot = new Vector2(0.5f, 0.5f);
        boxHeader.text = "--- Wins ---";
        // the header at the size of the boxes' headers (it was sizing itself to its rect)
        boxHeader.enableAutoSizing = false;
        boxHeader.fontSize = header.fontSize; // (its size as drawn, autosized or not)
        boxHeader.enableWordWrapping = false;
        boxHeader.alignment = TextAlignmentOptions.Center;
        TMP_Text rows = rowsGo.GetComponent<TMP_Text>();
        // MMIM's UI symbols (heads, crown, its HUD digits), as the tabs' and HUD's
        // text has them; the label's own default is the emoji set
        TMP_SpriteAsset symbols = Resources.Load<TMP_SpriteAsset>("fonts/UISymbols");
        if (symbols)
            rows.spriteAsset = symbols;
        rows.alignment = TextAlignmentOptions.TopLeft;
        rows.enableWordWrapping = false;
        rows.fontSizeMax = rows.fontSize;
        rows.fontSizeMin = rows.fontSize * 0.6f;
        rows.enableAutoSizing = true;
        rows.lineSpacing = 30f;
        var rt = (RectTransform)box.transform;
        rt.anchorMin = rt.anchorMax = new Vector2(0.5f, 0.5f);
        rt.pivot = new Vector2(0f, 1f);
        var board = box.AddComponent<WinsBoard>();
        board.header = boxHeader;
        board.rows = rows;
        Transform tabs = Find(list.transform, "Content");
        board.tabs = tabs as RectTransform;
        s_wins = box;
        Debug.LogWarning("[local] wins box made");
    }

    static void MapCard(MainMenuManager mm, bool local)
    {
        if (!local)
        {
            if (s_mapCard)
                s_mapCard.SetActive(false);
            return;
        }
        if (!s_mapCard)
            MakeMapCard(mm);
        if (!s_mapCard)
            return;
        s_mapCard.SetActive(true);
        if (Utils.GetCustomProperty(Enums.NetRoomProperties.Level, out int level))
            MapChanged(mm, level);
    }

    static void MakeMapCard(MainMenuManager mm)
    {
        Transform mapRow = mm.levelDropdown ? mm.levelDropdown.transform.parent : null; // LblMap
        Transform content = mapRow ? mapRow.parent : null;
        Transform starsRow = content ? content.Find("LblStars") : null; // "Stars to Win:": the box's lettering
        TMP_Text style = starsRow ? starsRow.GetComponent<TMP_Text>() : null;
        if (!content || !style)
            return;
        // a row of its own at the end of the rules (the server settings, after
        // it, are hidden in a local room)
        var card = new GameObject("MapCard", typeof(RectTransform));
        s_mapCard = card;
        card.layer = content.gameObject.layer;
        var rt = (RectTransform)card.transform;
        rt.SetParent(content, false);
        rt.anchorMin = rt.anchorMax = new Vector2(0f, 1f);
        rt.pivot = new Vector2(0.5f, 1f);
        rt.sizeDelta = new Vector2(((RectTransform)mapRow).sizeDelta.x, MapCardHeight);

        var picture = new GameObject("Picture", typeof(RectTransform), typeof(Image));
        picture.layer = card.layer;
        var prt = (RectTransform)picture.transform;
        prt.SetParent(rt, false);
        prt.anchorMin = new Vector2(0f, 1f - MapPictureShare);
        prt.anchorMax = new Vector2(1f, 1f);
        prt.offsetMin = new Vector2(0f, 2f);
        prt.offsetMax = Vector2.zero;
        s_mapImage = picture.GetComponent<Image>();
        s_mapImage.color = Color.clear; // only takes the touch: the copy draws
        s_mapImage.raycastTarget = true;
        picture.AddComponent<MapCardTap>().mapButton = mapRow.Find("Mapbtn") ? mapRow.Find("Mapbtn").GetComponent<Button>() : null;

        var label = new GameObject("Name", typeof(RectTransform));
        label.layer = card.layer;
        var lrt = (RectTransform)label.transform;
        lrt.SetParent(rt, false);
        lrt.anchorMin = Vector2.zero;
        lrt.anchorMax = new Vector2(1f, 1f - MapPictureShare);
        lrt.offsetMin = lrt.offsetMax = Vector2.zero;
        TextMeshProUGUI name = label.AddComponent<TextMeshProUGUI>();
        name.font = style.font;
        name.fontSharedMaterial = style.fontSharedMaterial;
        name.color = style.color;
        name.fontSize = style.fontSize;
        name.fontSizeMax = style.fontSize;
        name.fontSizeMin = style.fontSize * 0.5f;
        name.enableAutoSizing = true;
        name.enableWordWrapping = false;
        name.alignment = TextAlignmentOptions.Center;
        name.raycastTarget = false;
        s_mapName = name;
        rt.SetAsLastSibling();
        Debug.LogWarning("[local] map card made");
    }

    // MainMenuManager.ChangeLevel: the room's map changed
    public static void MapChanged(MainMenuManager mm, int level)
    {
        if (!s_mapCard || !s_mapImage)
            return;
        Button map = ControllerMenuSupport.MapButton(mm.mapsPrompt, level);
        if (s_mapShown)
            Object.Destroy(s_mapShown);
        s_mapShown = null;
        if (map)
        {
            // the list's own button (its frame image and the map's picture in
            // it), as a picture: at the same proportions, the card's height
            GameObject copy = Object.Instantiate(map.gameObject, s_mapImage.transform, false);
            copy.name = "Map";
            foreach (MonoBehaviour b in copy.GetComponentsInChildren<MonoBehaviour>(true))
                if (!(b is Graphic))
                    Object.DestroyImmediate(b); // its Button and EventTrigger
            foreach (Graphic g in copy.GetComponentsInChildren<Graphic>(true))
                g.raycastTarget = false;
            var crt = (RectTransform)copy.transform;
            crt.anchorMin = crt.anchorMax = crt.pivot = new Vector2(0.5f, 0.5f);
            crt.anchoredPosition = Vector2.zero;
            float side = MapCardHeight * MapPictureShare - 2f;
            float scale = side / Mathf.Max(1f, Mathf.Min(crt.sizeDelta.x, crt.sizeDelta.y));
            crt.localScale = new Vector3(scale, scale, 1f);
            copy.SetActive(true);
            s_mapShown = copy;
        }
        if (s_mapName && mm.levelDropdown && level >= 0 && level < mm.levelDropdown.options.Count)
            s_mapName.text = mm.levelDropdown.options[level].text;
    }

    // --- out of local play ----------------------------------------------------------------------

    // MainMenuManager.QuitRoom (Back in the room): local play over, online again
    public static bool Leave(MainMenuManager mm)
    {
        if (!LocalPlay.Active)
            return false;
        s_leaving = true;
        LocalPlay.End();
        PhotonNetwork.OfflineMode = false; // leaves the offline room: OnLeftRoom below
        if (s_leaving)
        {
            // not in a room after all: still go back
            s_leaving = false;
            BackToMainMenu(mm);
        }
        return true;
    }

    // MainMenuManager.OnLeftRoom: true when it was local play's room
    public static bool OnLeftRoom(MainMenuManager mm)
    {
        if (!s_leaving)
            return false;
        s_leaving = false;
        BackToMainMenu(mm);
        return true;
    }

    static void BackToMainMenu(MainMenuManager mm)
    {
        Apply(mm, false);
        mm.ClearChat();
        mm.OpenMainMenu();
        mm.ReconnectOnline();
        Debug.LogWarning("[local] back online");
    }
}

// the map card's picture (LocalPlayMenu): touched, it opens the maps as the
// Map Selection button does
public class MapCardTap : MonoBehaviour, IPointerClickHandler
{
    public Button mapButton;

    public void OnPointerClick(PointerEventData eventData)
    {
        if (mapButton && mapButton.IsInteractable())
            mapButton.onClick.Invoke();
    }
}
