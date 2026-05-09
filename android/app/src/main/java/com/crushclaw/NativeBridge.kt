package com.crushclaw

/**
 * JNI bridge between Android Java/Kotlin layer and C native code.
 *
 * The native library (libcrushclaw.so) contains the complete esp-claw
 * agent core, compiled via NDK CMake. This class provides the JNI
 * entry points and callback interface.
 */
class NativeBridge {

    companion object {
        init {
            System.loadLibrary("crushclaw")
        }
    }

    // ================================================================
    // Java → Native (called from Kotlin)
    // ================================================================

    /**
     * Start the native agent core.
     * @param dataDir  Absolute path to ~/.crush-claw/ data directory
     * @param callback The FloatingWindowService for display callbacks
     */
    external fun nativeStart(dataDir: String, callback: FloatingWindowService)

    /** Stop the native agent core. */
    external fun nativeStop()

    /**
     * Notify native that the display surface is ready (bitmap allocated).
     * This unblocks the renderer thread.
     */
    external fun nativeNotifyFrameReady()

    /**
     * Inject a touch/click event into the native input system.
     * @param action  0=press, 1=release, -1=cancel
     * @param x       Logical x coordinate in display space
     * @param y       Logical y coordinate in display space
     */
    external fun nativeInjectTouch(action: Int, x: Int, y: Int)

    /**
     * Reload configuration from config.json.
     */
    external fun nativeReloadConfig()

    /**
     * Request native to switch display to emote mode.
     * This goes through the display arbiter so C/Java state stay in sync.
     */
    external fun nativeSwitchToEmote()

    // ================================================================
    // Native → Java callbacks (called from C via JNI)
    // ================================================================

    // These are called from the C layer via the jobject reference
    // stored at nativeStart() time. Implementations are in
    // android_entry.c / display_hal_android.c
}
