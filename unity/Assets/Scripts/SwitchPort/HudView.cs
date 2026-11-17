// HudView.cs -- one local player's part of the HUD (Switch port, mmim_nx; SplitScreen).

using UnityEngine;

// a box over one part of the screen (`view`, 0..1 from the bottom left, as a
// camera's viewport), sized so that at `scale` it covers exactly that part:
// what is laid out in it for the full screen shows there at `scale` (the
// canvas size follows the screen, so it is fitted again every frame)
public class HudView : MonoBehaviour
{
    public Rect view = new Rect(0f, 0f, 1f, 1f);
    public float scale = 0.5f;
    RectTransform rt, parent;

    public void Fit()
    {
        if (!rt)
        {
            rt = (RectTransform)transform;
            parent = rt.parent as RectTransform;
        }
        if (!parent)
            return;
        Vector2 size = parent.rect.size;
        rt.anchorMin = rt.anchorMax = view.min;
        rt.pivot = Vector2.zero;
        rt.anchoredPosition = Vector2.zero;
        rt.sizeDelta = new Vector2(size.x * view.width / scale, size.y * view.height / scale);
        rt.localScale = new Vector3(scale, scale, 1f);
    }

    void LateUpdate()
    {
        Fit();
    }
}
