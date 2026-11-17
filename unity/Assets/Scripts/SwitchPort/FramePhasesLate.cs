// FramePhasesLate.cs -- the last LateUpdate of a frame: FramePhases' scripts phase ends here (Switch port, mmim_nx)

using UnityEngine;

[DefaultExecutionOrder(32000)]
public class FramePhasesLate : MonoBehaviour
{
    void LateUpdate()
    {
        FramePhases.ScriptsDone();
    }
}
