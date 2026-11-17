// SwitchControls.cs -- the Controls screen as a Switch game shows it (Switch port, mmim_nx).
//
// MvLO's Controls screen lists every action with two keyboard and two gamepad
// bindings, named as an Xbox pad names them ("LS", "RB", "Submit"). On the
// Switch there is only the controller, so the screen (RebindManager) keeps the
// controller's two columns, and every binding is named by the Switch button the
// player presses: the loader hands the Switch's A/B/X/Y to the game as Android's
// BUTTON_A/B/X/Y (input.c), which the Input System reads as south/east/west/
// north, so <Gamepad>/buttonSouth is the A button and so on.

using UnityEngine;
using UnityEngine.InputSystem;

public static class SwitchControls
{
    // the Switch port (an Android build run by mmim_nx); the editor and the
    // PC builds keep MvLO's screen
    public static bool Active => PortPlatform.Switch;

    // the Menus rows: what each UI action does, in the words a player knows
    public static string ActionName(InputAction action)
    {
        switch (action.name)
        {
            case "Navigate": return "Move Cursor";
            case "Submit": return "Confirm";
            case "Cancel": return "Back";
            default: return action.name;
        }
    }

    // a binding path, by the Switch button it reads
    public static string ButtonName(string path)
    {
        if (string.IsNullOrEmpty(path))
            return "";
        int slash = path.IndexOf('/');
        string control = slash >= 0 ? path.Substring(slash + 1) : path;
        switch (control)
        {
            case "buttonSouth": case "{Submit}": case "{PrimaryAction}": return "A";
            case "buttonEast": case "{Back}": case "{Cancel}": return "B";
            case "buttonWest": return "X";
            case "buttonNorth": return "Y";
            // Switch port: one binding for both (the Controls screen has two
            // columns): the power-up action / twirl on R, ZL and ZR, the
            // scoreboard on either stick press (Reserve Item took -). A full
            // controller's L is - (the loader, gamepad.c); a single Joy-Con's
            // SL/SR are both still the power-up action
            case "*Shoulder": return "R";
            case "*Trigger": return "ZL / ZR";
            case "*StickPress": return "Press a Stick";
            case "leftShoulder": return "L";
            case "rightShoulder": return "R";
            case "leftTrigger": return "ZL";
            case "rightTrigger": return "ZR";
            case "start": case "{Menu}": return "+ Button";
            case "select": return "L / -";
            case "leftStick": return "L Stick";
            case "rightStick": return "R Stick";
            case "leftStickPress": return "Press L Stick";
            case "rightStickPress": return "Press R Stick";
            case "dpad": return "D-Pad";
        }
        // a direction of a stick or the D-Pad: "L Stick Up", "D-Pad Left"
        int part = control.IndexOf('/');
        if (part > 0)
        {
            string whole = ButtonName(control.Substring(0, part));
            string dir = control.Substring(part + 1);
            if (dir.Length > 0)
                return whole + " " + char.ToUpperInvariant(dir[0]) + dir.Substring(1);
        }
        return InputControlPath.ToHumanReadableString(path,
            InputControlPath.HumanReadableStringOptions.OmitDevice |
            InputControlPath.HumanReadableStringOptions.UseShortNames);
    }

    // a binding the controller reads (not the keyboard's, and not an empty
    // slot: MvLO's asset has Gamepad bindings without a path, e.g. for
    // Debug Info and as the second button of Submit/Cancel/Pause)
    public static bool IsGamepad(InputBinding binding)
    {
        if (binding.isComposite || binding.isPartOfComposite || string.IsNullOrEmpty(binding.groups) ||
            string.IsNullOrEmpty(binding.effectivePath))
            return false;
        foreach (string g in binding.groups.Split(';'))
            if (g == "Gamepad")
                return true;
        return false;
    }

    // the columns of a row once the keyboard's are gone: the name, then the
    // controller's two, each wider
    public static void Columns(Transform row, string nameChild)
    {
        Hide(row, "KP");
        Hide(row, "KS");
        Hide(row, "KeyboardPrimary");
        Hide(row, "KeyboardSecondary");
        Span(row.Find(nameChild), 0.025f, 0.43f);
        Span(row.Find("GP"), 0.45f, 0.705f);
        Span(row.Find("GS"), 0.72f, 0.975f);
        Span(row.Find("GamepadPrimary"), 0.45f, 0.705f);
        Span(row.Find("GamepadSecondary"), 0.72f, 0.975f);
    }

    static void Hide(Transform row, string child)
    {
        Transform t = row.Find(child);
        if (t)
            t.gameObject.SetActive(false);
    }

    static void Span(Transform t, float from, float to)
    {
        var rt = t as RectTransform;
        if (!rt)
            return;
        rt.anchorMin = new Vector2(from, rt.anchorMin.y);
        rt.anchorMax = new Vector2(to, rt.anchorMax.y);
        rt.sizeDelta = new Vector2(0f, rt.sizeDelta.y);
        rt.anchoredPosition = new Vector2(0f, rt.anchoredPosition.y);
    }
}
