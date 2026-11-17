// LocalNametags.cs -- "P1" to "P4" over each local player in split play.
//
// MMIM makes nametags only for other people's characters
// (PlayerAnimationController: !photonView.IsMine). In local play every
// player is this console's, so split play had none, and two players on the
// same character were hard to tell apart. Each player now has a tag in
// their own colour (red, green, yellow, blue: NSMBW's, as the outlines and
// the scoreboard rows) in the other players' parts of the screen, not in
// their own (they know which one they are; user, 2026-11-16).
//
// The tags are MMIM's own nametag (PlayerNametag: username + arrow), its
// UserNametag taken off (it follows one networked player and destroys
// itself without one), drawn in the viewer's own HUD box ("Part (P2)",
// SplitScreen.PartBox: the HUD's scale). On MMIM's NametagCanvas the tags
// in player 2's half of a 2-player split drew as a sliver a few pixels tall
// (Ryubing; Unity's scale and rect were the same as the visible ones), while
// that player's HUD drew whole: so the tags go where the HUD is. A tag is
// placed where its player's head is in the viewer's camera, as UserNametag
// places it, and stays inside the viewer's part.

using System.Collections.Generic;
using TMPro;
using UnityEngine;
using UnityEngine.UI;

public class LocalNametags : MonoBehaviour
{
    class Tag
    {
        public GameObject go;
        public RectTransform rt;
        public int target, viewer;
    }

    readonly List<Tag> tags = new List<Tag>();

    public static void Make(GameManager gm)
    {
        if (!LocalPlay.Split || !gm || !gm.nametagPrefab || gm.GetComponent<LocalNametags>())
            return;
        gm.gameObject.AddComponent<LocalNametags>().Build(gm);
    }

    void Build(GameManager gm)
    {
        int players = Mathf.Clamp(LocalPlay.Players, 2, LocalPlay.MaxPlayers);
        float scale = SplitScreen.HudScale(players);
        GameObject template = gm.nametagPrefab;
        for (int viewer = 0; viewer < players; viewer++)
            for (int target = 0; target < players; target++)
            {
                if (target == viewer)
                    continue;
                Transform box = SplitScreen.PartBox(viewer);
                GameObject go = Instantiate(template, box ? box : template.transform.parent);
                go.SetActive(false);
                go.name = "LocalNametag P" + (target + 1) + " in P" + (viewer + 1);
                UserNametag own = go.GetComponent<UserNametag>();
                if (own)
                    DestroyImmediate(own);
                Color colour = LocalPlay.GlowColor(target);
                foreach (TMP_Text text in go.GetComponentsInChildren<TMP_Text>(true))
                {
                    text.text = "P" + (target + 1);
                    text.color = colour;
                }
                foreach (Image image in go.GetComponentsInChildren<Image>(true))
                    if (image.name == "arrow")
                        image.color = colour;
                // in the HUD box its scale is the box's (the split HUD's); on
                // MMIM's canvas (no box) the same scale by hand
                go.transform.localScale = template.transform.localScale * (box ? 1f : scale);
                go.transform.SetAsLastSibling(); // over that player's HUD parts
                tags.Add(new Tag { go = go, rt = (RectTransform)go.transform, target = target, viewer = viewer });
            }
        Debug.LogWarning("[local] nametags: " + tags.Count + " (P1-P" + players + ", each in the other players' views)");
    }

    void LateUpdate()
    {
        GameManager gm = GameManager.Instance;
        foreach (Tag tag in tags)
        {
            if (!tag.go)
                continue;
            PlayerController p = LocalPlay.PlayerAt(tag.target);
            Camera cam = SplitScreen.CameraFor(tag.viewer);
            bool show = gm && p && p.spawned && cam;
            if (show)
            {
                // over the head, as UserNametag (and across a looping level's seam)
                Vector3 world = p.transform.position;
                world.y += p.WorldHitboxSize.y * 1.2f + 0.5f;
                if (gm.loopingLevel && Mathf.Abs(cam.transform.position.x - world.x) > gm.levelWidthTile * 0.25f)
                    world.x += Mathf.Sign(cam.transform.position.x) * gm.levelWidthTile / 2f;
                Vector3 screen = cam.WorldToScreenPoint(world);
                Rect part = cam.pixelRect;
                show = screen.z > 0f && part.Contains(screen);
                if (show)
                {
                    // the tag's height above its point stays in this player's part
                    float height = tag.rt.rect.height * tag.rt.lossyScale.y;
                    screen.y = Mathf.Min(screen.y, part.yMax - height);
                    screen.z = 0f;
                    tag.rt.position = screen;
                }
            }
            if (tag.go.activeSelf != show)
                tag.go.SetActive(show);
            if (Time.unscaledTime >= nextLog)
                Debug.LogWarning("[local] tag P" + (tag.target + 1) + " in P" + (tag.viewer + 1) + "'s part: " +
                                 (show ? "shown" : "hidden") + (p ? (p.spawned ? "" : ", not spawned") : ", no player") +
                                 (cam ? ", camera " + cam.name + " " + cam.pixelRect + ", head at " +
                                  (p ? cam.WorldToScreenPoint(p.transform.position).ToString() : "-") : ", no camera") +
                                 ", tag at " + tag.rt.position + TextState(tag.go));
        }
        if (Time.unscaledTime >= nextLog)
            nextLog = Time.unscaledTime + (logged++ < 5 ? 2f : 60f);
    }

    static string TextState(GameObject go)
    {
        TMP_Text text = go.GetComponentInChildren<TMP_Text>(true);
        if (!text)
            return ", no text";
        Canvas canvas = go.GetComponentInParent<Canvas>();
        RectTransform root = (RectTransform)go.transform, tr = text.rectTransform;
        return ", text \"" + text.text + "\" at " + text.transform.position + (text.isActiveAndEnabled ? "" : " (off)") +
               " alpha " + text.color.a.ToString("0.0") + " scale " + text.transform.lossyScale.ToString("F3") +
               " root scale " + root.localScale.ToString("F3") + " root rect " + root.rect + " text rect " + tr.rect +
               " size " + text.fontSize.ToString("0.0") + " bounds " + text.textBounds.size.ToString("F1") +
               " parent " + (tr.parent ? tr.parent.name + " " + tr.parent.localScale.ToString("F3") : "-") +
               (canvas ? ", canvas " + canvas.name + " mode " + canvas.renderMode + " order " + canvas.sortingOrder : "");
    }

    float nextLog;
    int logged;

    void OnDestroy()
    {
        foreach (Tag tag in tags)
            if (tag.go)
                Destroy(tag.go);
    }
}
