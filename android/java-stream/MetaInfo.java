package dev.headless.sequence;

import java.lang.reflect.Method;

/**
 * MetaInfo helper.
 * Attempts to detect display rotation using several reflection-based fallbacks.
 * Returns rotation in degrees (0, 90, 180, 270). If detection fails, returns 0.
 *
 * Notes:
 * - This runs inside app_process on many Android devices, so we use reflection to call
 *   hidden framework methods (WindowManager / DisplayManagerGlobal / SystemProperties).
 * - Different Android versions expose rotation via different services; we try several.
 * - This is best-effort and must not throw exceptions to caller.
 */
public final class MetaInfo {

    private MetaInfo() {}

    public static int getRotation() {
        try {
            Class<?> wmgClass = Class.forName("android.view.WindowManagerGlobal");
            Method getWMS = wmgClass.getMethod("getWindowManagerService");
            Object wms = getWMS.invoke(null);
            if (wms != null) {
                String[] candidates = new String[] {
                    "getDefaultDisplayRotation",
                    "getDefaultDisplayRotationForUser",
                    "getRotation",
                    "getRotationForDisplay",
                    "getDisplayRotation",
                    "getRotation"
                };
                for (String name : candidates) {
                    try {
                        Method m = null;
                        for (Method mm : wms.getClass().getMethods()) {
                            if (mm.getName().equals(name)) {
                                m = mm;
                                break;
                            }
                        }
                        if (m == null) continue;
                        Object val;
                        if (m.getParameterTypes().length == 0) {
                            val = m.invoke(wms);
                        } else if (m.getParameterTypes().length == 1 &&
                                   (m.getParameterTypes()[0] == int.class || m.getParameterTypes()[0] == Integer.class)) {
                            val = m.invoke(wms, 0);
                        } else {
                            continue;
                        }
                        if (val != null) {
                            int rot = toInt(val);
                            return normalizeRotation(rot);
                        }
                    } catch (Throwable ignored) {
                    }
                }
            }
        } catch (Throwable ignored) {}
        try {
            Class<?> dmgClass = Class.forName("android.hardware.display.DisplayManagerGlobal");
            Method getInstance = dmgClass.getMethod("getInstance");
            Object instance = getInstance.invoke(null);
            if (instance != null) {
                Method getDisplay = null;
                for (Method mm : dmgClass.getMethods()) {
                    if (mm.getName().equals("getDisplay") && mm.getParameterTypes().length == 1 &&
                        (mm.getParameterTypes()[0] == int.class || mm.getParameterTypes()[0] == Integer.class)) {
                        getDisplay = mm;
                        break;
                    }
                }
                if (getDisplay != null) {
                    Object display = getDisplay.invoke(instance, 0);
                    if (display != null) {
                        try {
                            Method getRotation = display.getClass().getMethod("getRotation");
                            Object val = getRotation.invoke(display);
                            if (val != null) {
                                int rot = toInt(val);
                                return normalizeRotation(rot);
                            }
                        } catch (Throwable ignored) {}
                    }
                }
            }
        } catch (Throwable ignored) {}
        try {
            Class<?> wmgClass = Class.forName("android.view.WindowManagerGlobal");
            Method getInstance = wmgClass.getMethod("getInstance");
            Object wg = getInstance.invoke(null);
            if (wg != null) {
                try {
                    Method getDefaultDisplay = wmgClass.getMethod("getDefaultDisplay");
                    Object display = getDefaultDisplay.invoke(wg);
                    if (display != null) {
                        Method getRotation = display.getClass().getMethod("getRotation");
                        Object val = getRotation.invoke(display);
                        if (val != null) {
                            int rot = toInt(val);
                            return normalizeRotation(rot);
                        }
                    }
                } catch (Throwable ignored) {}
            }
        } catch (Throwable ignored) {}
        try {
            Class<?> sp = Class.forName("android.os.SystemProperties");
            Method getInt = sp.getMethod("getInt", String.class, int.class);
            int r = (Integer) getInt.invoke(null, "ro.sf.hwrotation", 0);
            if (r != 0) return normalizeRotation(r);
            r = (Integer) getInt.invoke(null, "persist.sys.rotation", 0);
            if (r != 0) return normalizeRotation(r);
            r = (Integer) getInt.invoke(null, "persist.display.rotation", 0);
            if (r != 0) return normalizeRotation(r);
        } catch (Throwable ignored) {}
        return 0;}

    private static int toInt(Object val) {
        if (val instanceof Integer) return ((Integer) val).intValue();
        if (val instanceof Number) return ((Number) val).intValue();
        try {
            return Integer.parseInt(String.valueOf(val));
        } catch (Exception e) {
            return 0;}}

    private static int normalizeRotation(int rot) {
        if (rot < 0) return 0;
        if (rot >= 0 && rot <= 3) return rot * 90;
        rot = (rot % 360 + 360) % 360;
        if (rot == 0 || rot == 90 || rot == 180 || rot == 270) return rot;
        return 0;
    }
}
