// SafeAudio.cs -- AudioSource.time on the web only once the browser knows the
// clip's length.
//
// In the web build a compressed clip plays through an <audio> element, and
// Unity's WebGL code takes its length as mediaElement.duration * 44100:
// NaN, so 0, until the browser has read the file's header. AudioSource.time
// (get and set) takes the position modulo that length; WebAssembly traps on
// a remainder by zero and the whole page died ("RuntimeError: remainder by
// zero" in AudioSource_Get_Custom_PropTime from LoopingMusic.Update, going
// back to the main menu after a match: the menu's music had just started).
// Unity's C# side reports the clip loaded with its real length all along, so
// the only safe sign is time: a source that has been playing the same clip
// for a second. Other platforms always know the length: nothing changes.

using System.Collections.Generic;
using UnityEngine;

public static class SafeAudio
{
    const float Settle = 1f; // seconds of play before the browser surely has the header

    static readonly Dictionary<AudioSource, (AudioClip clip, float since)> s_seen =
        new Dictionary<AudioSource, (AudioClip, float)>();

    public static bool TimeKnown(AudioSource source)
    {
        if (!source || !source.clip)
            return false;
        if (!PortPlatform.Web)
            return true;
        float now = Time.unscaledTime;
        if (!source.isPlaying || !s_seen.TryGetValue(source, out var seen) || seen.clip != source.clip)
        {
            s_seen[source] = (source.isPlaying ? source.clip : null, now);
            return false;
        }
        return now - seen.since >= Settle;
    }

    // a source started (again) or got a new clip: its second starts now
    public static void Started(AudioSource source)
    {
        if (source && PortPlatform.Web)
            s_seen[source] = (source.clip, Time.unscaledTime);
    }
}
