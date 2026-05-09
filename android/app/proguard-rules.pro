# ProGuard rules for Crush Claw

# Keep JNI methods
-keepclasseswithmembernames class com.crushclaw.NativeBridge {
    native <methods>;
}

# Keep FloatingWindowService callback methods (called from JNI)
-keepclassmembers class com.crushclaw.FloatingWindowService {
    void onDisplayCreate(int, int);
    void onDisplayDestroy();
    void onDisplayOwnerChanged(int, int, int);
    void onFrameReady(byte[], int, int);
    void onEmoteText(java.lang.String);
    void onDisplayEnable(boolean);
}

# Keep config structures
-keepclassmembers class com.crushclaw.CrushClawApp {
    public *;
}
