using System.Globalization;
using System.IO;
using UnityEditor;
using UnityEngine;

// Switch port: the Android-only audio quality of each clip (the phone APK
// and the Switch's APK), calibrated against MMIM's own bitrates by
// tools/match_pc_import.py --measured into tools/audio/android_quality.tsv
// (asset path TAB Vorbis quality). Each clip gets it as its Android
// override; the default settings stay for the browser build (AAC, already
// at MMIM's rates). MiiBumm, 2026-11-18: the music sounded worse than MMIM's.
//
//   Unity -batchmode -quit -projectPath mmim_project -buildTarget Android
//         -executeMethod MvlAudio.PhoneBuild   (or AndroidBuild, ApplyAndroid)
public static class MvlAudio
{
    const string Table = "../tools/audio/android_quality.tsv";

    public static void ApplyAndroid()
    {
        if (!File.Exists(Table))
        {
            Debug.LogWarning("[MvlAudio] no " + Table + ": Android audio left as it is");
            return;
        }
        int changed = 0, same = 0, missing = 0;
        AssetDatabase.StartAssetEditing();
        try
        {
            foreach (string line in File.ReadAllLines(Table))
            {
                if (line.Length == 0 || line[0] == '#')
                    continue;
                string[] f = line.Split('\t');
                var importer = AssetImporter.GetAtPath(f[0]) as AudioImporter;
                if (importer == null)
                {
                    missing++;
                    Debug.LogWarning("[MvlAudio] no audio clip at " + f[0]);
                    continue;
                }
                float quality = float.Parse(f[1], CultureInfo.InvariantCulture);
                bool had = importer.ContainsSampleSettingsOverride("Android");
                AudioImporterSampleSettings s = had ? importer.GetOverrideSampleSettings("Android")
                                                    : importer.defaultSampleSettings;
                // the override copies the default's load type and format:
                // keep those in step with it, only the quality differs
                AudioImporterSampleSettings d = importer.defaultSampleSettings;
                if (had && Mathf.Abs(s.quality - quality) < 0.001f && s.loadType == d.loadType
                    && s.compressionFormat == d.compressionFormat)
                {
                    same++;
                    continue;
                }
                d.quality = quality;
                importer.SetOverrideSampleSettings("Android", d);
                importer.SaveAndReimport();
                changed++;
            }
        }
        finally
        {
            AssetDatabase.StopAssetEditing();
        }
        AssetDatabase.SaveAssets();
        Debug.Log($"[MvlAudio] Android quality: {changed} set, {same} already, {missing} missing");
    }

    // Unity's Vorbis quality is flat over most of its range for this music
    // (castle: 63.4 kbps at both 0.21 and 0.49, MMIM's is 102), so a table
    // cannot say which quality gives MMIM's rate. This finds it per clip:
    // for each clip in tools/audio/calibrate_list.tsv (asset, MMIM kbps,
    // length, last build's kbps) the lowest Android quality whose imported
    // size reaches 95% of MMIM's rate (1.0 if none does), by re-importing
    // the clip and reading its imported size, then writes it into Table.
    const string List = "../tools/audio/calibrate_list.tsv";

    static readonly System.Reflection.PropertyInfo s_compSize =
        typeof(AudioImporter).GetProperty("compSize", System.Reflection.BindingFlags.Instance |
                                                      System.Reflection.BindingFlags.NonPublic |
                                                      System.Reflection.BindingFlags.Public);

    static float KbpsAt(AudioImporter importer, float quality, float length)
    {
        AudioImporterSampleSettings s = importer.defaultSampleSettings;
        s.quality = quality;
        importer.SetOverrideSampleSettings("Android", s);
        importer.SaveAndReimport();
        long size = System.Convert.ToInt64(s_compSize.GetValue(importer));
        return size * 8f / length / 1000f;
    }

    public static void Calibrate()
    {
        var table = new System.Collections.Generic.List<string>(File.ReadAllLines(Table));
        int done = 0;
        var watch = System.Diagnostics.Stopwatch.StartNew();
        foreach (string line in File.ReadAllLines(List))
        {
            if (line.Length == 0 || line[0] == '#')
                continue;
            string[] f = line.Split('\t');
            string path = f[0];
            float mmim = float.Parse(f[1], CultureInfo.InvariantCulture);
            float length = float.Parse(f[2], CultureInfo.InvariantCulture);
            float built = float.Parse(f[3], CultureInfo.InvariantCulture);
            var importer = AssetImporter.GetAtPath(path) as AudioImporter;
            int row = table.FindIndex(l => l.StartsWith(path + "\t"));
            if (importer == null || row < 0)
            {
                Debug.LogWarning("[MvlAudio] skipped " + path);
                continue;
            }
            float qNow = float.Parse(table[row].Split('\t')[1], CultureInfo.InvariantCulture);
            float goal = mmim * 0.95f;
            float kNow = KbpsAt(importer, qNow, length);
            // the imported size must be what the build has (else this measures the wrong thing)
            string check = Mathf.Abs(kNow - built) <= built * 0.03f ? "" : $" (build had {built:0.0}: MISMATCH)";
            float lo = qNow, hi = 1f, best = 1f, kBest;
            float kHi = KbpsAt(importer, hi, length);
            kBest = kHi;
            if (kNow >= goal)
            {
                best = qNow;
                kBest = kNow;
            }
            else if (kHi >= goal)
            {
                for (int i = 0; i < 6; i++)
                {
                    float mid = Mathf.Round((lo + hi) * 50f) / 100f;
                    if (mid <= lo || mid >= hi)
                        break;
                    float k = KbpsAt(importer, mid, length);
                    if (k >= goal)
                    {
                        hi = mid;
                        kHi = k;
                    }
                    else
                        lo = mid;
                }
                best = hi;
                kBest = kHi;
            }
            KbpsAt(importer, best, length); // leave the clip imported at the result
            table[row] = path + "\t" + best.ToString("0.##", CultureInfo.InvariantCulture);
            File.WriteAllLines(Table, table); // after each clip: a stop keeps what is done
            done++;
            Debug.Log($"[MvlAudio] {path}: q {qNow:0.00} -> {best:0.00}, {kNow:0.0} -> {kBest:0.0} kbps (MMIM {mmim:0.0}){check} [{done}, {watch.Elapsed.TotalMinutes:0.0} min]");
        }
        AssetDatabase.SaveAssets();
        Debug.Log($"[MvlAudio] calibrated {done} clips in {watch.Elapsed.TotalMinutes:0.0} min");
    }

    public static void PhoneBuild()
    {
        ApplyAndroid();
        MvlBuild.Phone();
    }

    public static void AndroidBuild()
    {
        ApplyAndroid();
        MvlBuild.Android();
    }
}
