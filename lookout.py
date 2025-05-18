import time
import math
from ovrsdk import *

# Configuration
LOOK_THRESHOLD = math.radians(20)   # 20°
WARNING_TIME = 2.0                  # seconds
POLL_INTERVAL = 0.1                 # seconds

def main():
    # Initialize SDK
    ovr_Initialize()
    hmd = ovrHmd_Create(0)
    hmdDesc = ovrHmdDesc()
    ovrHmd_GetDesc(hmd, byref(hmdDesc))
    print(hmdDesc.ProductName)
    ovrHmd_StartSensor(
        hmd,
        ovrSensorCap_Orientation |
        ovrSensorCap_YawCorrection,
        0
    )
    no_look_time = 0.0

    try:
        while True:
            # Get current sensor state
            ss = ovrHmd_GetSensorState(hmd, ovr_GetTimeInSeconds())
            pose = ss.Predicted.Pose

            # Extract yaw from quaternion
            w, x, y, z = pose.Orientation.w, pose.Orientation.x, pose.Orientation.y, pose.Orientation.z
            # Yaw extraction from quaternion
            siny_cosp = 2.0 * (w * y + x * z)
            cosy_cosp = 1.0 - 2.0 * (y * y + z * z)
            yaw = math.atan2(siny_cosp, cosy_cosp)

            # Lookout logic
            if abs(yaw) < LOOK_THRESHOLD:
                no_look_time += POLL_INTERVAL
                if no_look_time >= WARNING_TIME:
                    print("⚠️  Please perform a visual lookout!")  # or play sound
                    no_look_time = 0.0
            else:
                no_look_time = 0.0

            time.sleep(POLL_INTERVAL)
    except KeyboardInterrupt:
        pass
    finally:
        ovr_Shutdown()

if __name__ == "__main__":
    main()
