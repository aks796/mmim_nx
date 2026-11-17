// TouchControls.cs -- on-screen controls in a match, for the phone build and
// for a browser on a touch screen (PortPlatform.TouchControls).
//
// The controls press a virtual gamepad (the Input System's on-screen
// controls), so MMIM's own bindings (<Gamepad>/...) take them as they take a
// controller:
//   left side   a stick that appears under the thumb (<Gamepad>/leftStick);
//               pushed far to a side, the player runs (Running, read by
//               PlayerController.HandleMovement: MMIM's Sprint button would
//               also throw a fireball each time, its "Fireball from Sprint")
//   JUMP        <Gamepad>/buttonSouth
//   SPIN/power  <Gamepad>/rightShoulder (MMIM's Power-up Action): small, big
//               and Mini, the twirl ("SPIN"); with a power-up, its action --
//               the button shows that power-up's icon (a throw, the Propeller's
//               launch); grey for the four without one
//   pause       GameManager.Pause (MMIM's Pause has no gamepad binding)
//   reserve     a tap on the HUD's own reserve box drops the item
//               (<Gamepad>/select), as the touch screen did in NSMB DS
// Shapes are drawn here (plain circles and rings), labels in the game's font.

using System.Collections.Generic;
using TMPro;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.InputSystem.Layouts;
using UnityEngine.InputSystem.OnScreen;
using UnityEngine.UI;

public class TouchControls : MonoBehaviour
{
    // the stick pushed far to a side (PlayerController.HandleMovement: the
    // local player runs)
    public static bool Running { get; private set; }

    // the web build: a touch screen in use (PortPlatform.TouchControls)
    public static bool WebTouch { get; private set; }

    static TouchControls s_instance;
    static Sprite s_circle, s_ring;

    GameObject overlay;
    GameManager builtFor;
    TouchStick stick;
    Image powerIcon;
    CanvasGroup powerGroup;
    TMP_Text powerLabel;
    Enums.PowerupState? shownState; // null: not shown yet
    GameObject reserveTap;
    Rect safeArea;
    RectTransform safeRect;
    readonly Dictionary<Enums.PowerupState, Sprite> icons = new Dictionary<Enums.PowerupState, Sprite>();

    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
    static void Create()
    {
        if (s_instance)
            return;
        var go = new GameObject("TouchControls");
        DontDestroyOnLoad(go);
        s_instance = go.AddComponent<TouchControls>();
    }

    void Update()
    {
        if (PortPlatform.Web)
            WatchWebTouch();
        GameManager game = GameManager.Instance;
        bool inMatch = PortPlatform.TouchControls && game && game.localPlayer && game.started && !game.gameover;
        if (!inMatch)
        {
            Running = false;
            if (overlay)
                overlay.SetActive(false);
            return;
        }
        if (!overlay || builtFor != game)
            Build(game);
        bool show = !game.paused;
        if (overlay.activeSelf != show)
            overlay.SetActive(show);
        Running = show && stick && Mathf.Abs(stick.Value.x) > 0.75f;
        if (show)
        {
            FitSafeArea();
            ShowPowerup(game);
            HookReserve();
        }
    }

    // a phone's browser: touch from the start; any other touch screen (an
    // iPad says it is a Mac) once it is touched; a key pressed: the keyboard
    static void WatchWebTouch()
    {
        var touch = UnityEngine.InputSystem.Touchscreen.current;
        if (touch != null && (Application.isMobilePlatform || touch.primaryTouch.press.wasPressedThisFrame))
            WebTouch = true;
        var keys = UnityEngine.InputSystem.Keyboard.current;
        if (keys != null && keys.anyKey.wasPressedThisFrame)
            WebTouch = false;
    }

    // --- building ---------------------------------------------------------------------------

    void Build(GameManager game)
    {
        if (overlay)
            Destroy(overlay);
        builtFor = game;
        shownState = null;
        reserveTap = null;
        MakeSprites();

        // in the level's scene: gone with it
        overlay = new GameObject("Touch Controls", typeof(RectTransform));
        var canvas = overlay.AddComponent<Canvas>();
        canvas.renderMode = RenderMode.ScreenSpaceOverlay;
        canvas.sortingOrder = 50;
        var scaler = overlay.AddComponent<CanvasScaler>();
        scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
        scaler.referenceResolution = new Vector2(1280f, 720f);
        scaler.matchWidthOrHeight = 1f; // sized by the screen's height: the same thumb reach on any phone
        overlay.AddComponent<GraphicRaycaster>();

        safeRect = Child(overlay.transform, "Safe Area");
        Stretch(safeRect);
        safeArea = Rect.zero;
        FitSafeArea();

        // the stick: anywhere on the left, under the lower three quarters
        RectTransform zone = Child(safeRect, "Stick Zone");
        zone.anchorMin = Vector2.zero;
        zone.anchorMax = new Vector2(0.42f, 0.78f);
        zone.offsetMin = zone.offsetMax = Vector2.zero;
        zone.gameObject.AddComponent<Image>().color = Color.clear; // catches the touch
        RectTransform stickBase = Child(zone, "Base");
        Place(stickBase, Vector2.zero, new Vector2(190f, 170f), 230f);
        AddImage(stickBase, s_ring, 0.35f);
        RectTransform knob = Child(stickBase, "Knob");
        Place(knob, new Vector2(0.5f, 0.5f), Vector2.zero, 110f);
        AddImage(knob, s_circle, 0.45f);
        zone.gameObject.SetActive(false);
        stick = zone.gameObject.AddComponent<TouchStick>();
        stick.baseRect = stickBase;
        stick.knob = knob;
        stick.range = 80f;
        zone.gameObject.SetActive(true);

        // the right thumb: JUMP big, the power-up below-left of it, both clear
        // of the HUD's reserve box in the corner (centre ~(-62, 80), radius
        // ~60, and its tap area 24 more around it)
        Button(safeRect, "Jump", new Vector2(1f, 0f), new Vector2(-195f, 258f), 170f, "<Gamepad>/buttonSouth", "JUMP", 38f);
        RectTransform power = Button(safeRect, "Power", new Vector2(1f, 0f), new Vector2(-380f, 128f), 136f,
                                     "<Gamepad>/rightShoulder", "", 30f);
        powerGroup = power.gameObject.AddComponent<CanvasGroup>();
        powerGroup.blocksRaycasts = true;
        RectTransform iconRect = Child(power, "Icon");
        Place(iconRect, new Vector2(0.5f, 0.5f), new Vector2(0f, 6f), 76f);
        powerIcon = AddImage(iconRect, null, 1f);
        powerIcon.preserveAspect = true;
        powerIcon.raycastTarget = false;
        powerLabel = Label(power, "SPIN", 32f, Vector2.zero);

        // pause, top centre under the level's progress bar (the HUD's row:
        // stars left, coins right, the bar between them along the top)
        RectTransform pause = Child(safeRect, "Pause");
        Place(pause, new Vector2(0.5f, 1f), new Vector2(0f, -96f), 76f);
        Image pauseImage = AddImage(pause, s_circle, 0.3f);
        Label(pause, "II", 40f, Vector2.zero);
        var pauseButton = pause.gameObject.AddComponent<Button>();
        pauseButton.targetGraphic = pauseImage;
        pauseButton.transition = Selectable.Transition.None;
        pauseButton.navigation = new Navigation { mode = Navigation.Mode.None };
        pauseButton.onClick.AddListener(() =>
        {
            if (GameManager.Instance && !GameManager.Instance.paused)
                GameManager.Instance.Pause();
        });
        Debug.LogWarning("[touch] on-screen controls made");
    }

    RectTransform Button(RectTransform parent, string name, Vector2 anchor, Vector2 at, float size, string path,
                         string text, float fontSize)
    {
        RectTransform rect = Child(parent, name);
        Place(rect, anchor, at, size);
        Image image = AddImage(rect, s_circle, 0.3f);
        if (!string.IsNullOrEmpty(text))
            Label(rect, text, fontSize, Vector2.zero);
        rect.gameObject.SetActive(false); // the control path is set before the button turns on
        var button = rect.gameObject.AddComponent<OnScreenButton>();
        button.controlPath = path;
        rect.gameObject.AddComponent<TouchPressLook>().image = image;
        rect.gameObject.SetActive(true);
        return rect;
    }

    // --- every frame ---------------------------------------------------------------------------

    void FitSafeArea()
    {
        Rect area = Screen.safeArea;
        if (area == safeArea || !safeRect || Screen.width <= 0 || Screen.height <= 0)
            return;
        safeArea = area;
        safeRect.anchorMin = new Vector2(area.xMin / Screen.width, area.yMin / Screen.height);
        safeRect.anchorMax = new Vector2(area.xMax / Screen.width, area.yMax / Screen.height);
        safeRect.offsetMin = safeRect.offsetMax = Vector2.zero;
    }

    // what the button does now (PlayerController.ActivatePowerupAction): the
    // twirl ("SPIN"), the power-up's own action (its icon), or nothing (grey)
    void ShowPowerup(GameManager game)
    {
        PlayerController player = game.localPlayer ? game.localPlayer.GetComponent<PlayerController>() : null;
        if (!player || player.state == shownState)
            return;
        Enums.PowerupState state = player.state;
        shownState = state;
        bool twirl = Twirls(state), none = NoAction(state);
        Sprite icon = twirl || none ? null : Icon(state);
        powerIcon.sprite = icon;
        powerIcon.enabled = icon;
        powerLabel.text = twirl ? "SPIN" : "POWER";
        powerLabel.gameObject.SetActive(!icon);
        powerGroup.alpha = none ? 0.45f : 1f;
    }

    static bool Twirls(Enums.PowerupState state)
    {
        return state == Enums.PowerupState.Small || state == Enums.PowerupState.Mushroom ||
               state == Enums.PowerupState.MiniMushroom;
    }

    static bool NoAction(Enums.PowerupState state)
    {
        return state == Enums.PowerupState.BlueShell || state == Enums.PowerupState.MegaMushroom ||
               state == Enums.PowerupState.MagmaFlower || state == Enums.PowerupState.GummyFlower;
    }

    Sprite Icon(Enums.PowerupState state)
    {
        if (!icons.TryGetValue(state, out Sprite sprite))
        {
            var powerup = Resources.Load("Scriptables/Powerups/" + state) as Powerup;
            sprite = powerup ? powerup.reserveSprite : null;
            icons[state] = sprite;
        }
        return sprite;
    }

    // a tap on the HUD's reserve box drops the item: an invisible button over it
    void HookReserve()
    {
        if (reserveTap)
            return;
        UIUpdater hud = UIUpdater.Instance;
        if (!hud || !hud.itemReserve)
            return;
        Canvas canvas = hud.itemReserve.GetComponentInParent<Canvas>();
        if (canvas && !canvas.rootCanvas.GetComponent<GraphicRaycaster>())
            canvas.rootCanvas.gameObject.AddComponent<GraphicRaycaster>();
        RectTransform tap = Child(hud.itemReserve.transform, "Reserve Tap");
        tap.anchorMin = Vector2.zero;
        tap.anchorMax = Vector2.one;
        tap.offsetMin = new Vector2(-24f, -24f); // a thumb's worth around the box
        tap.offsetMax = new Vector2(24f, 24f);
        tap.gameObject.AddComponent<Image>().color = Color.clear;
        tap.gameObject.SetActive(false);
        tap.gameObject.AddComponent<OnScreenButton>().controlPath = "<Gamepad>/select";
        tap.gameObject.SetActive(true);
        reserveTap = tap.gameObject;
    }

    // --- helpers ---------------------------------------------------------------------------------

    static RectTransform Child(Transform parent, string name)
    {
        var go = new GameObject(name, typeof(RectTransform));
        go.transform.SetParent(parent, false);
        return (RectTransform)go.transform;
    }

    static void Stretch(RectTransform rect)
    {
        rect.anchorMin = Vector2.zero;
        rect.anchorMax = Vector2.one;
        rect.offsetMin = rect.offsetMax = Vector2.zero;
    }

    static void Place(RectTransform rect, Vector2 anchor, Vector2 at, float size)
    {
        rect.anchorMin = rect.anchorMax = anchor;
        rect.pivot = new Vector2(0.5f, 0.5f);
        rect.anchoredPosition = at;
        rect.sizeDelta = new Vector2(size, size);
    }

    static Image AddImage(RectTransform rect, Sprite sprite, float alpha)
    {
        var image = rect.gameObject.AddComponent<Image>();
        image.sprite = sprite;
        image.color = new Color(1f, 1f, 1f, alpha);
        return image;
    }

    static TMP_Text Label(RectTransform parent, string text, float size, Vector2 at)
    {
        RectTransform rect = Child(parent, "Label");
        Stretch(rect);
        rect.anchoredPosition = at;
        var label = rect.gameObject.AddComponent<TextMeshProUGUI>();
        label.text = text;
        label.fontSize = size;
        label.alignment = TextAlignmentOptions.Center;
        label.color = new Color(1f, 1f, 1f, 0.9f);
        label.raycastTarget = false;
        label.outlineWidth = 0.25f;
        label.outlineColor = new Color32(0, 0, 0, 200);
        return label;
    }

    // a filled circle and a ring, smooth at any size the canvas scales them to
    static void MakeSprites()
    {
        if (s_circle)
            return;
        s_circle = Disc(128, 0f);
        s_ring = Disc(256, 0.82f);
    }

    static Sprite Disc(int size, float inner)
    {
        var tex = new Texture2D(size, size, TextureFormat.RGBA32, false);
        tex.wrapMode = TextureWrapMode.Clamp;
        var px = new Color32[size * size];
        float r = size * 0.5f, edge = 1.5f;
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++)
            {
                float d = Vector2.Distance(new Vector2(x + 0.5f, y + 0.5f), new Vector2(r, r));
                float a = Mathf.Clamp01((r - d) / edge);
                if (inner > 0f)
                    a *= Mathf.Clamp01((d - r * inner) / edge);
                px[y * size + x] = new Color32(255, 255, 255, (byte)(a * 255f));
            }
        tex.SetPixels32(px);
        tex.Apply(false, true);
        return Sprite.Create(tex, new Rect(0, 0, size, size), new Vector2(0.5f, 0.5f), 100f);
    }
}

// the stick: where the thumb lands in its zone it appears, and pushes the
// virtual gamepad's left stick as the thumb moves from there
public class TouchStick : OnScreenControl, IPointerDownHandler, IDragHandler, IPointerUpHandler
{
    [InputControl(layout = "Vector2")]
    [SerializeField]
    string m_ControlPath = "<Gamepad>/leftStick";

    protected override string controlPathInternal
    {
        get => m_ControlPath;
        set => m_ControlPath = value;
    }

    public RectTransform baseRect, knob;
    public float range = 80f;
    public Vector2 Value { get; private set; }

    int pointer = int.MinValue;
    Vector2 rest;
    bool restKnown;

    public void OnPointerDown(PointerEventData e)
    {
        if (pointer != int.MinValue || !baseRect)
            return;
        pointer = e.pointerId;
        if (!restKnown)
        {
            rest = baseRect.anchoredPosition;
            restKnown = true;
        }
        var zone = (RectTransform)transform;
        if (RectTransformUtility.ScreenPointToLocalPointInRectangle(zone, e.position, e.pressEventCamera, out Vector2 local))
            baseRect.anchoredPosition = local - zone.rect.min; // the base's anchor is the zone's lower left
        Move(e);
    }

    public void OnDrag(PointerEventData e)
    {
        if (e.pointerId == pointer)
            Move(e);
    }

    public void OnPointerUp(PointerEventData e)
    {
        if (e.pointerId != pointer)
            return;
        pointer = int.MinValue;
        Value = Vector2.zero;
        if (knob)
            knob.anchoredPosition = Vector2.zero;
        if (restKnown)
            baseRect.anchoredPosition = rest;
        SendValueToControl(Vector2.zero);
    }

    void Move(PointerEventData e)
    {
        if (!RectTransformUtility.ScreenPointToLocalPointInRectangle(baseRect, e.position, e.pressEventCamera, out Vector2 local))
            return;
        Vector2 v = Vector2.ClampMagnitude(local / Mathf.Max(range, 1f), 1f);
        Value = v;
        if (knob)
            knob.anchoredPosition = v * range;
        SendValueToControl(v);
    }

    protected override void OnDisable()
    {
        pointer = int.MinValue;
        Value = Vector2.zero;
        base.OnDisable();
    }
}

// a pressed button looks pressed
public class TouchPressLook : MonoBehaviour, IPointerDownHandler, IPointerUpHandler
{
    public Image image;
    float upAlpha = -1f;

    public void OnPointerDown(PointerEventData e)
    {
        if (!image)
            return;
        if (upAlpha < 0f)
            upAlpha = image.color.a;
        image.color = new Color(1f, 1f, 1f, Mathf.Min(1f, upAlpha + 0.3f));
        transform.localScale = Vector3.one * 0.94f;
    }

    public void OnPointerUp(PointerEventData e)
    {
        if (!image || upAlpha < 0f)
            return;
        image.color = new Color(1f, 1f, 1f, upAlpha);
        transform.localScale = Vector3.one;
    }

    void OnDisable()
    {
        if (image && upAlpha >= 0f)
            image.color = new Color(1f, 1f, 1f, upAlpha);
        transform.localScale = Vector3.one;
    }
}
