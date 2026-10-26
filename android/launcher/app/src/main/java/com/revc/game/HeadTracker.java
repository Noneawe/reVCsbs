package com.revc.game;

import android.app.Activity;
import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.view.Surface;

/**
 * Head orientation from the phone's game rotation vector (no magnetometer, so yaw drifts slowly;
 * the game offers a recenter). Sends the orientation of the screen frame (X right, Y up, Z out of
 * the screen, relative to the world) as a quaternion to native code.
 */
public class HeadTracker implements SensorEventListener {
    private static native void nativeHeadQuat(float x, float y, float z, float w);

    private final Activity activity;
    private SensorManager sensorManager;
    private final float[] rot = new float[9];
    private final float[] remapped = new float[9];

    public HeadTracker(Activity activity) {
        this.activity = activity;
    }

    public void start() {
        sensorManager = (SensorManager) activity.getSystemService(Context.SENSOR_SERVICE);
        if (sensorManager == null) return;
        Sensor sensor = sensorManager.getDefaultSensor(Sensor.TYPE_GAME_ROTATION_VECTOR);
        if (sensor != null)
            sensorManager.registerListener(this, sensor, SensorManager.SENSOR_DELAY_GAME);
    }

    public void stop() {
        if (sensorManager != null) sensorManager.unregisterListener(this);
    }

    @Override
    public void onSensorChanged(SensorEvent event) {
        SensorManager.getRotationMatrixFromVector(rot, event.values);
        int axisX, axisY;
        switch (activity.getWindowManager().getDefaultDisplay().getRotation()) {
            case Surface.ROTATION_90:
                axisX = SensorManager.AXIS_Y; axisY = SensorManager.AXIS_MINUS_X; break;
            case Surface.ROTATION_270:
                axisX = SensorManager.AXIS_MINUS_Y; axisY = SensorManager.AXIS_X; break;
            case Surface.ROTATION_180:
                axisX = SensorManager.AXIS_MINUS_X; axisY = SensorManager.AXIS_MINUS_Y; break;
            default:
                axisX = SensorManager.AXIS_X; axisY = SensorManager.AXIS_Y; break;
        }
        if (!SensorManager.remapCoordinateSystem(rot, axisX, axisY, remapped)) return;
        float[] m = remapped;
        float w, x, y, z;
        float trace = m[0] + m[4] + m[8];
        if (trace > 0f) {
            float s = (float) Math.sqrt(trace + 1f) * 2f;
            w = 0.25f * s; x = (m[7] - m[5]) / s; y = (m[2] - m[6]) / s; z = (m[3] - m[1]) / s;
        } else if (m[0] > m[4] && m[0] > m[8]) {
            float s = (float) Math.sqrt(1f + m[0] - m[4] - m[8]) * 2f;
            w = (m[7] - m[5]) / s; x = 0.25f * s; y = (m[1] + m[3]) / s; z = (m[2] + m[6]) / s;
        } else if (m[4] > m[8]) {
            float s = (float) Math.sqrt(1f + m[4] - m[0] - m[8]) * 2f;
            w = (m[2] - m[6]) / s; x = (m[1] + m[3]) / s; y = 0.25f * s; z = (m[5] + m[7]) / s;
        } else {
            float s = (float) Math.sqrt(1f + m[8] - m[0] - m[4]) * 2f;
            w = (m[3] - m[1]) / s; x = (m[2] + m[6]) / s; y = (m[5] + m[7]) / s; z = 0.25f * s;
        }
        nativeHeadQuat(x, y, z, w);
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {
    }
}
