// WinsBoard.cs -- the local room's wins table (Switch port, mmim_nx).
//
// Under the player tabs, in the room's empty left column: a box in the
// room's own look (a copy of MMIM's Player Settings box: its panel, its
// "--- ... ---" header, its label lettering) headed "--- Wins ---", one row
// per player, the most wins first: rank, the character's head, the player,
// MMIM's crown x wins, numbered down the order (ties: player 1 first). The
// tally is LocalPlay's (this boot's matches). The box stays where it is
// whoever joins: beside the lower player box(es), its top and bottom theirs
// (four tabs end just above it), as wide as the tabs (LocalPlayMenu.WinsBox).

using System.Collections.Generic;
using System.Text;
using NSMB.Utils;
using TMPro;
using UnityEngine;

public class WinsBoard : MonoBehaviour
{
    public TMP_Text header, rows;
    public RectTransform tabs; // the player list's content: its last active tab is the box's top

    string shown;
    readonly List<int> order = new List<int>();
    readonly StringBuilder text = new StringBuilder(256);
    static readonly Vector3[] s_c = new Vector3[4];

    void LateUpdate()
    {
        if (!LocalPlay.Active || !rows)
            return;
        Fill();
        Place();
    }

    void Fill()
    {
        int players = LocalPlay.Players;
        order.Clear();
        for (int slot = 0; slot < players; slot++)
            order.Add(slot);
        order.Sort((a, b) => LocalPlay.WinsOf(b) != LocalPlay.WinsOf(a) ? LocalPlay.WinsOf(b) - LocalPlay.WinsOf(a) : a - b);
        text.Length = 0;
        int rank = 0;
        foreach (int slot in order)
        {
            int wins = LocalPlay.WinsOf(slot);
            rank++;
            if (text.Length > 0)
                text.Append('\n');
            // columns at fixed places (the heads are not all as wide)
            // (the heads sat on the letters' baseline: down to the middle of the line)
            text.Append(rank).Append(".<pos=12%><voffset=-0.3em>").Append(LocalPlay.CharacterData(slot).uistring)
                .Append("</voffset><pos=27%>").Append(LocalPlay.NameOf(slot)).Append("<pos=74%><sprite=5>").Append(Utils.GetSymbolString("x" + wins));
        }
        string now = text.ToString();
        if (now == shown)
            return;
        shown = now;
        rows.text = now;
        // the box as tall as its rows
        var box = (RectTransform)transform;
        float width = box.rect.width;
        float headerHeight = header ? header.GetPreferredValues(header.text, width, 0f).y : 0f;
        float rowsHeight = rows.GetPreferredValues(now, width * 0.92f, 0f).y;
        var hr = header ? header.rectTransform : null;
        if (hr)
        {
            hr.anchorMin = new Vector2(0f, 1f);
            hr.anchorMax = new Vector2(1f, 1f);
            hr.pivot = new Vector2(0.5f, 1f);
            hr.offsetMin = new Vector2(0f, -headerHeight - 4f);
            hr.offsetMax = new Vector2(0f, -4f);
        }
        var rr = rows.rectTransform;
        rr.anchorMin = new Vector2(0f, 1f);
        rr.anchorMax = new Vector2(1f, 1f);
        rr.pivot = new Vector2(0.5f, 1f);
        rr.offsetMin = new Vector2(width * 0.05f, -headerHeight - 8f - rowsHeight);
        rr.offsetMax = new Vector2(-width * 0.03f, -headerHeight - 8f);
    }

    // beside the lower player box(es): their top and bottom, the tabs' width
    void Place()
    {
        if (!tabs || !tabs.parent || !LocalPlayMenu.LowerBoxRect(out Rect lower))
            return;
        var view = (RectTransform)tabs.parent;
        view.GetWorldCorners(s_c);
        float left = s_c[0].x, right = s_c[2].x;
        var box = (RectTransform)transform;
        Vector3 scale = box.parent ? box.parent.lossyScale : Vector3.one;
        Vector2 size = new Vector2((right - left) / Mathf.Max(scale.x, 1e-6f) - 4f, lower.height / Mathf.Max(scale.y, 1e-6f));
        if ((box.sizeDelta - size).sqrMagnitude > 0.25f)
        {
            box.sizeDelta = size;
            shown = null; // its rows laid out again at this size
        }
        Vector3 at = new Vector3(left + 2f * scale.x, lower.yMax, box.position.z);
        if ((box.position - at).sqrMagnitude > 1e-6f)
            box.position = at;
    }
}
