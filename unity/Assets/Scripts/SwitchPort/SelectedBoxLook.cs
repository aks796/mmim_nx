// SelectedBoxLook.cs -- the selected box shows, with a controller (Switch port, mmim_nx).
//
// MMIM's text fields, number boxes, chat field and dropdowns were made for a
// mouse: a 0.96 tint when hovered, or (the Character dropdown, the Map
// button) a white box whose white text then vanishes. With a controller the
// cursor got lost on them. While a controller is in use, the selected box is
// drawn the way MMIM marks the selected row of an open dropdown list (the
// Character list: a gray bar, 0.61 gray at half opacity over black = 78,78,78,
// white text): the box's own pixel art with its fill made that gray and its
// edge kept (Resources/SwitchPort/<sprite>_selected, from optionbox_0,
// pausemenu and pausescrollbar_1), its text white. Everything is put back
// when the selection moves on.
//
// In the local room each player's cursor is drawn in their colour (tint: red,
// green, yellow, blue): the box's edge in that colour, its fill a darker one
// (optionbox_selected_tint: the fill lighter, so the colour shows).
//
// The room's Add Player tab: MMIM's player tabs were made for a mouse (their
// button is an invisible image; its colour tint showed nothing), so the cursor
// on it drew nothing. Its gray strip takes the cursor's colour (MMIM's light
// gray without one) and a faint white lights the tab, as long as it is selected.

using System.Collections.Generic;
using TMPro;
using UnityEngine;
using UnityEngine.UI;

public class SelectedBoxLook
{
    GameObject s_shown;
    Image s_image;
    Sprite s_shownGray;
    Color s_imageColor;
    Color s_shownTint = Color.white;
    public Color tint = Color.white; // the cursor's colour (white: none, MMIM's gray)
    readonly List<KeyValuePair<Graphic, Color>> s_recoloured = new List<KeyValuePair<Graphic, Color>>();
    static readonly Dictionary<string, Sprite> s_gray = new Dictionary<string, Sprite>();
    static Sprite s_tintBox;
    Image s_tabStrip, s_tabGlow;

    static bool Tinted(Color c) => c.r < 0.99f || c.g < 0.99f || c.b < 0.99f;

    // the gray-filled version of one of MMIM's box sprites, or null
    static Sprite Gray(Sprite normal)
    {
        if (!normal)
            return null;
        if (s_gray.TryGetValue(normal.name, out Sprite gray))
            return gray;
        gray = null;
        switch (normal.name)
        {
            case "optionbox_0":
            case "optionbox_7": // a SpriteSwap box already showing its selected sprite
                gray = Resources.Load<Sprite>("SwitchPort/optionbox_selected");
                break;
            case "pausemenu":
            case "pausescrollbar_1":
                gray = Resources.Load<Sprite>("SwitchPort/" + normal.name + "_selected");
                break;
        }
        s_gray[normal.name] = gray;
        return gray;
    }

    // every frame: the box shown selected (null: none, touch in use)
    public void Show(GameObject selected)
    {
        if (ReferenceEquals(selected, s_shown) && tint != s_shownTint)
            Restore(); // the room opened or closed: drawn again in the cursor's colour
        if (ReferenceEquals(selected, s_shown))
        {
            // the box's own transition (a sprite swap when its state is set
            // again: the room's panel fading in) put its white sprite back
            if (s_image && s_shownGray && s_image.overrideSprite != s_shownGray)
                s_image.overrideSprite = s_shownGray;
            return;
        }
        Restore();
        s_shown = selected;
        s_shownTint = tint;
        PlayerListEntry tab = selected ? selected.GetComponentInParent<PlayerListEntry>() : null;
        if (tab && tab.addPlayerTab && tab.ColorStripImage)
        {
            s_tabStrip = tab.ColorStripImage;
            s_tabStrip.color = Tinted(tint) ? new Color(tint.r, tint.g, tint.b, 1f) : new Color(0.85f, 0.85f, 0.85f, 1f);
            s_tabGlow = selected.GetComponent<Image>();
            if (s_tabGlow)
                s_tabGlow.color = new Color(1f, 1f, 1f, 0.16f);
            return;
        }
        var sel = selected ? selected.GetComponent<Selectable>() : null;
        var image = sel ? sel.targetGraphic as Image : null;
        Sprite gray = image ? Gray(image.sprite) : null;
        if (!gray)
            return;
        if (Tinted(tint) && gray.name == "optionbox_selected")
        {
            if (!s_tintBox)
                s_tintBox = Resources.Load<Sprite>("SwitchPort/optionbox_selected_tint");
            if (s_tintBox)
                gray = s_tintBox;
        }
        s_image = image;
        s_shownGray = gray;
        s_shownTint = tint;
        s_imageColor = image.color;
        image.overrideSprite = gray;
        // the server box's olive tint off while selected; a local player's colour on
        image.color = new Color(tint.r, tint.g, tint.b, s_imageColor.a);
        Canvas canvas = image.canvas;
        foreach (Graphic g in selected.GetComponentsInChildren<Graphic>())
        {
            if (g == image || g.canvas != canvas) // not the box, nor an open dropdown's list
                continue;
            if (!(g is TMP_Text) && !(g is Text) && !(g is Image))
                continue;
            Color c = g.color;
            bool grey = Mathf.Abs(c.r - c.g) < 0.1f && Mathf.Abs(c.g - c.b) < 0.1f;
            if (!grey || c.r > 0.95f)
                continue; // coloured, or white already
            s_recoloured.Add(new KeyValuePair<Graphic, Color>(g, c));
            g.color = new Color(1f, 1f, 1f, c.a); // white on gray, as the list's rows
        }
    }

    public void Restore()
    {
        // back to the tab's own look (not the colour seen when selected: two
        // cursors on the tab at once would leave one player's colour on it)
        if (s_tabStrip)
            s_tabStrip.color = PlayerListEntry.AddPlayerStripColor;
        if (s_tabGlow)
            s_tabGlow.color = new Color(1f, 1f, 1f, 0f);
        s_tabStrip = null;
        s_tabGlow = null;
        if (s_image)
        {
            s_image.overrideSprite = null; // a SpriteSwap box sets its own again
            s_image.color = s_imageColor;
        }
        s_image = null;
        s_shownGray = null;
        foreach (var kv in s_recoloured)
            if (kv.Key)
                kv.Key.color = kv.Value;
        s_recoloured.Clear();
        s_shown = null;
    }
}
