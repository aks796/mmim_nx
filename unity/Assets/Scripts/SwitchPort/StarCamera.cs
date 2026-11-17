// StarCamera.cs -- three players' fourth quarter: the star's camera (Switch port, mmim_nx).
//
// With three players the fourth quarter of the split screen (SplitScreen)
// watches the match for everyone: the Big Star where it waits, else a star
// knocked loose from a player, else whoever leads (most stars). It pans to
// each new star (the short way round on a looping stage) and keeps to the
// stage's camera bounds as a player's camera does (CameraController). Its rig
// is a copy of the players' (backgrounds, world, wrap), so it looks like
// the game's own camera.

using System.Collections.Generic;
using UnityEngine;

public class StarCamera : MonoBehaviour
{
    Camera cam;
    BackgroundLoop loop;
    readonly List<SecondaryCameraPositioner> wrapCameras = new List<SecondaryCameraPositioner>();
    Vector3 position, velocity;
    float z, nextLook;
    bool placed;
    StarBouncer star;

    void Start()
    {
        cam = GetComponent<Camera>();
        loop = GetComponent<BackgroundLoop>();
        GetComponentsInChildren(true, wrapCameras);
        z = transform.position.z;
        position = transform.position;
    }

    void LateUpdate()
    {
        GameManager gm = GameManager.Instance;
        if (!gm || !cam)
            return;
        Vector3 want = Target(gm);
        if (gm.loopingLevel)
        {
            // the short way round: one stage width over is the same view
            float width = gm.levelWidthTile * 0.5f;
            if (Mathf.Abs(want.x - position.x) > width * 0.5f)
            {
                position.x += Mathf.Sign(want.x - position.x) * width;
                if (loop)
                    loop.wrap = true;
            }
        }
        if (!placed)
        {
            position = want;
            placed = true;
        }
        position = Vector3.SmoothDamp(position, want, ref velocity, 0.45f);

        // the stage's camera bounds (CameraController.CalculateNewPosition)
        float ortho = cam.orthographicSize;
        float halfWidth = ortho * cam.aspect;
        Vector3 p = position;
        float minY = gm.cameraMinY + ortho;
        float maxY = gm.cameraHeightY == 0f ? minY : gm.cameraMinY + gm.cameraHeightY - ortho;
        p.y = Mathf.Clamp(p.y, minY, Mathf.Max(minY, maxY));
        if (gm.cameraMaxX - gm.cameraMinX < halfWidth * 2f)
            p.x = (gm.cameraMinX + gm.cameraMaxX) * 0.5f;
        else
            p.x = Mathf.Clamp(p.x, gm.cameraMinX + halfWidth, gm.cameraMaxX - halfWidth);
        p.z = z;
        transform.position = p;
        if (loop)
            loop.Reposition();
        wrapCameras.RemoveAll(w => w == null);
        foreach (SecondaryCameraPositioner w in wrapCameras)
            w.UpdatePosition();
    }

    // the Big Star, a loose star, else the leader
    Vector3 Target(GameManager gm)
    {
        if (Time.unscaledTime >= nextLook || !star || star.Collected)
        {
            nextLook = Time.unscaledTime + 0.25f;
            star = null;
            foreach (StarBouncer s in FindObjectsOfType<StarBouncer>())
            {
                if (!s || s.Collected)
                    continue;
                if (!star || (s.stationary && !star.stationary))
                    star = s;
            }
        }
        if (star)
            return star.transform.position;
        PlayerController lead = null;
        foreach (PlayerController p in gm.players)
            if (p && (!lead || p.stars > lead.stars))
                lead = p;
        return lead ? lead.transform.position : position;
    }
}
