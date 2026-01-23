package dev.headless.sequence;

import java.lang.reflect.Method;

public final class MetaInfo {

    private MetaInfo() {}

    public static int getRotation() {
        // Try WindowManagerGlobal -> window manager service methods
        try {
            Class<?> wmgClass = Class.forName("android.view.WindowManagerGlobal");
            Method getWMS = wmgClass.getMethod("getWindowManagerService");
            Object wms = getWMS.invoke(null);
            if (wms != null) {
                // Try several possible method names that exist across API levels
                String[] candidates = new String[] {
                    "getDefaultDisplayRotation", // some OEM/hidden variants
                    "getDefaultDisplayRotationForUser",
                    "getRotation",               // possible candidate on IWindowManager
                    "getRotationForDisplay",
                    "getDisplayRotation",
                    "getRotation"                // duplicate safe-check
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
                            // try displayId = 0
                            val = m.invoke(wms, 0);
                        } else {
                            continue;
                        }
                        if (val != null) {
                            int rot = toInt(val);
                            return normalizeRotation(rot);
                        }
                    } catch (Throwable ignored) {
                        // continue to next candidate
                    }
                }
            }
        } catch (Throwable ignored) {}
// Try DisplayManagerGlobal -> getDisplay(0) -> getRotation()
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
                        // display.getRotation() is public on android.view.Display
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

        // Try android.view.Display via WindowManager (alternative path)
        try {
            // Try to obtain default Display through WindowManagerGlobal.getWindowManagerService()
            Class<?> wmgClass = Class.forName("android.view.WindowManagerGlobal");
            Method getInstance = wmgClass.getMethod("getInstance");
            Object wg = getInstance.invoke(null);
            if (wg != null) {
                // Some OS versions expose getDefaultDisplay() on WindowManagerGlobal
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

        // Try reading common system properties (some devices store physical rotation)
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

        // default fallback
        return 0;
    }

    // Convert returned value to int robustly
    private static int toInt(Object val) {
        if (val instanceof Integer) return ((Integer) val).intValue();
        if (val instanceof Number) return ((Number) val).intValue();
        try {
            return Integer.parseInt(String.valueOf(val));
        } catch (Exception e) {
            return 0;
        }
    }

    // Normalize rotation value to degrees.
    // Many framework methods return 0..3 meaning 0/90/180/270, while some system props may store degrees.
    private static int normalizeRotation(int rot) {
        if (rot < 0) return 0;
        if (rot >= 0 && rot <= 3) return rot * 90;
        // If it already looks like degrees (0/90/180/270), clamp to nearest valid
        rot = (rot % 360 + 360) % 360;
        if (rot == 0 || rot == 90 || rot == 180 || rot == 270) return rot;
        // otherwise fallback to 0
        return 0;
    }
}
