package io.github.iosue_iulianus.retrods;

import android.app.AlertDialog;
import android.app.PendingIntent;
import android.content.ActivityNotFoundException;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageInstaller;
import android.graphics.Point;
import android.hardware.display.DisplayManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.system.ErrnoException;
import android.system.Os;
import android.text.InputType;
import android.util.DisplayMetrics;
import android.util.Log;
import android.util.TypedValue;
import android.view.Display;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.widget.EditText;
import android.widget.LinearLayout;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * The game. SDL draws on the display this activity is on; the other display
 * gets a {@link SecondScreen} (a Presentation) that the native code draws
 * into.
 *
 * Intent extra "rom": a ROM path to play (e.g. adb shell am start -n
 * io.github.iosue_iulianus.retrods/io.github.iosue_iulianus.retrods.LauncherActivity --es rom /storage/emulated/0/ROMs/snes/x.sfc).
 */
public class RetroDsActivity extends SDLActivity {
    /* kind: 1 down, 2 up, 3 motion; x, y: 0..1 across this display */
    static native void nativeMainTouch(int kind, float x, float y);

    /**
     * Touches on this activity's display, by their place in the view (as
     * the second screen's are), instead of SDL's: on the Retroid Pocket
     * Duo SDL's came out wrong about half the time, as if measured on the
     * other display (x half as much again, y squeezed toward the middle),
     * so taps on the second screen's map missed what they were aimed at.
     */
    /**
     * Two fingers (a pinch, for the map's zoom): kind 4 while two or more
     * are down, at their middle, their distance apart (as a fraction of
     * the view's width, in 1/4096ths) in the bits above the kind's four;
     * kind 5 when one of two lifts, at the one left (which drags on).
     */
    private static void pinch(View v, MotionEvent e, int kind) {
        float w = Math.max(1, v.getWidth()), h = Math.max(1, v.getHeight());
        if (kind == 5) {
            int keep = e.getActionIndex() == 0 ? 1 : 0;
            nativeMainTouch(5, e.getX(keep) / w, e.getY(keep) / h);
            return;
        }
        if (e.getPointerCount() < 2) return;
        float x0 = e.getX(0), y0 = e.getY(0), x1 = e.getX(1), y1 = e.getY(1);
        int span = Math.min(0x7FFFFF, Math.round((float) Math.hypot(x1 - x0, y1 - y0) / w * 4096));
        nativeMainTouch(4 | span << 4, (x0 + x1) / 2 / w, (y0 + y1) / 2 / h);
    }

    private boolean onTouch(View v, MotionEvent e) {
        int kind;
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN: kind = 1; break;
            case MotionEvent.ACTION_UP: case MotionEvent.ACTION_CANCEL: kind = 2; break;
            case MotionEvent.ACTION_MOVE: kind = e.getPointerCount() >= 2 ? 4 : 3; break;
            case MotionEvent.ACTION_POINTER_DOWN: kind = 4; break;
            case MotionEvent.ACTION_POINTER_UP: kind = e.getPointerCount() == 2 ? 5 : 4; break;
            default: return true;
        }
        if (kind >= 4) {
            pinch(v, e, kind);
            return true;
        }
        nativeMainTouch(kind, e.getX() / Math.max(1, v.getWidth()), e.getY() / Math.max(1, v.getHeight()));
        return true;
    }

    /**
     * SDL's surface, its touch handler {@link #onTouch}. SDLSurface makes
     * itself its own touch listener again on every resume (handleResume),
     * which undid a listener set on it from here: SDL's positions came
     * back, mixed with these (a tap's down from one, its up from the
     * other; a drag jumping between the two).
     */
    @Override
    protected SDLSurface createSDLSurface(Context context) {
        return new SDLSurface(context) {
            @Override
            public boolean onTouch(View v, MotionEvent e) {
                return RetroDsActivity.this.onTouch(v, e);
            }
        };
    }

    private static final String TAG = "RETRODS";
    private SecondScreen second;

    /** The game activity in this process, if one is running (this class or
     * {@link RetroDsHereActivity}): the launcher sends a new request to it,
     * in its task, rather than starting a second one. */
    static volatile RetroDsActivity running;

    @Override
    protected void onCreate(Bundle state) {
        running = this;
        /* before SDL starts the native thread: where config, states and the log go */
        File home = dataFolder();
        try {
            Os.setenv("RETRODS_HOME", home.getAbsolutePath(), true);
            /* data-folder.cfg there: the folder the player chose instead (android.c android_data_setup) */
            Os.setenv("RETRODS_APP_FILES", getFilesDir().getAbsolutePath(), true);
            /* the emulators' cores (libsnes_a.so ..., libswanstation.so: src/emu.h) */
            Os.setenv("RETRODS_CORE_DIR", getApplicationInfo().nativeLibraryDir, true);
        } catch (ErrnoException e) {
            Log.e(TAG, "setenv: " + e);
        }
        Log.i(TAG, "RetroDS " + BuildConfig.VERSION_NAME + ", data in " + home);
        /* launch.log: the steps up to the native side's own log, so a start
         * that dies before retrods.log exists can still be followed */
        LauncherActivity.launchLog(this, "starting RetroDS " + BuildConfig.VERSION_NAME + " on " + Build.MANUFACTURER + " "
                + Build.MODEL + " (" + Build.HARDWARE + "), Android " + Build.VERSION.RELEASE + " (API "
                + Build.VERSION.SDK_INT + "), ABIs " + java.util.Arrays.toString(Build.SUPPORTED_ABIS)
                + ", data in " + home);
        /* before the window is laid out: on a Surface Duo a landscape lock
         * here takes a spanned app back to one screen (issue #48) */
        setRequestedOrientation(orientationWanted());
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        LauncherActivity.launchLog(this, mBrokenLibraries ? "native libraries didn't load (SDL shows why)" : "native libraries loaded");
        if (!mBrokenLibraries) openSecondScreen();
        IntentFilter f = new IntentFilter(INSTALL_STATUS);
        if (Build.VERSION.SDK_INT >= 33) registerReceiver(installStatus, f, Context.RECEIVER_NOT_EXPORTED);
        else registerReceiver(installStatus, f);
    }

    /**
     * "RetroDS" on the internal storage, where people can reach it (cheats,
     * overlays, screenshots, save states), laid out like the ROCKNIX install
     * folder. Needs All files access; without it, the app's own folder.
     * Data from builds that used the app's folder is copied over once, and
     * so is SNES DS's folder (importSnesDs). This is the default: a folder
     * the player chose instead (Settings > Folders) is set up by the native
     * side (android_data_setup), and then "RetroDS" isn't made.
     */
    private File dataFolder() {
        File appDir = getExternalFilesDir(null);
        if (appDir == null) appDir = getFilesDir();
        File shared = new File(Environment.getExternalStorageDirectory(), "RetroDS");
        boolean canShare = Build.VERSION.SDK_INT < 30 || Environment.isExternalStorageManager();
        String now = LauncherActivity.dataFolderSetting(this, "data_dir_now");
        if (canShare && now != null && !new File(now).equals(shared) && !shared.isDirectory()) return shared;
        if (!canShare || !(shared.isDirectory() || shared.mkdirs())) {
            Log.w(TAG, "no All files access: data stays in " + appDir);
            appDir.mkdirs();
            return appDir;
        }
        File oldStates = new File(appDir, "states"), newStates = new File(shared, "states");
        if (oldStates.isDirectory() && !newStates.exists()) {
            Log.i(TAG, "moving " + oldStates + " to " + newStates);
            if (copyTree(oldStates, newStates)) deleteTree(oldStates);
        }
        importSnesDs(shared);
        return shared;
    }

    /* RetroDS was called SNES DS (another application ID, so Android gives
     * it none of SNES DS's data). Once, while RetroDS has no states of its
     * own, copy what's in "SNES DS" on the internal storage into "RetroDS":
     * states, cheats, logos, screenshots, overlays, the settings. Copied,
     * never moved: SNES DS keeps all of it. snes-ds.cfg becomes retrods.cfg,
     * and paths into "SNES DS" in the .cfg files point into "RetroDS".
     * Logs and the cores' cache aren't copied. The list of what was copied
     * is in RetroDS/imported-from-snes-ds.txt, which also marks it done. */
    static final String IMPORT_RECORD = "imported-from-snes-ds.txt";

    private static void importSnesDs(File to) {
        File from = new File(Environment.getExternalStorageDirectory(), "SNES DS");
        File record = new File(to, IMPORT_RECORD);
        if (!from.isDirectory() || record.exists() || new File(to, "states").exists()) return;
        StringBuilder done = new StringBuilder("# copied from " + from + " when RetroDS first started;\n"
                + "# SNES DS's own folder is unchanged\n");
        boolean ok = importTree(from, to, from.getPath(), to.getPath(), done);
        Log.i(TAG, "copied SNES DS's files into " + to + (ok ? "" : " (some failed: see " + IMPORT_RECORD + ")"));
        try (OutputStream os = new FileOutputStream(record)) {
            os.write(done.toString().getBytes("UTF-8"));
        } catch (IOException e) {
            Log.e(TAG, "writing " + record + ": " + e);
        }
    }

    private static boolean importTree(File from, File to, String oldRoot, String newRoot, StringBuilder done) {
        String name = from.getName();
        if (from.isDirectory()) {
            if (name.equals("cache")) return true; /* the cores' copies: made again */
            if (!to.isDirectory() && !to.mkdirs()) return false;
            File[] list = from.listFiles();
            boolean ok = true;
            if (list != null) for (File f : list) {
                String n = f.getName();
                if (n.endsWith(".log")) continue;
                ok &= importTree(f, new File(to, n.equals("snes-ds.cfg") ? "retrods.cfg" : n), oldRoot, newRoot, done);
            }
            return ok;
        }
        if (to.exists()) return true;
        boolean ok;
        if (name.endsWith(".cfg")) {
            ok = copyCfg(from, to, oldRoot, newRoot);
        } else {
            ok = copyTree(from, to);
        }
        done.append(ok ? "" : "FAILED ").append(to.getPath().substring(newRoot.length() + 1)).append('\n');
        return ok;
    }

    /* a settings file, with its paths into SNES DS's folder moved to RetroDS's */
    private static boolean copyCfg(File from, File to, String oldRoot, String newRoot) {
        try (InputStream is = new FileInputStream(from)) {
            java.io.ByteArrayOutputStream b = new java.io.ByteArrayOutputStream();
            byte[] buf = new byte[1 << 16];
            for (int n; (n = is.read(buf)) > 0; ) b.write(buf, 0, n);
            String text = b.toString("UTF-8").replace(oldRoot + "/", newRoot + "/");
            try (OutputStream os = new FileOutputStream(to)) {
                os.write(text.getBytes("UTF-8"));
            }
            return true;
        } catch (IOException e) {
            Log.e(TAG, "copying " + from + ": " + e);
            return false;
        }
    }

    private static boolean copyTree(File from, File to) {
        if (from.isDirectory()) {
            if (!to.isDirectory() && !to.mkdirs()) return false;
            File[] list = from.listFiles();
            boolean ok = true;
            if (list != null) for (File f : list) ok &= copyTree(f, new File(to, f.getName()));
            return ok;
        }
        try (InputStream is = new FileInputStream(from); OutputStream os = new FileOutputStream(to)) {
            byte[] buf = new byte[1 << 16];
            for (int n; (n = is.read(buf)) > 0; ) os.write(buf, 0, n);
            return true;
        } catch (IOException e) {
            Log.e(TAG, "copying " + from + ": " + e);
            return false;
        }
    }

    private static void deleteTree(File f) {
        File[] list = f.listFiles();
        if (list != null) for (File c : list) deleteTree(c);
        f.delete();
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    private String currentRom; /* the game started with (null: the game list) */
    private Intent switchTo;   /* a frontend asked for another game: restart with it */

    @Override
    protected String[] getArguments() {
        String rom = getIntent() != null ? getIntent().getStringExtra("rom") : null;
        if (rom != null && !rom.isEmpty()) {
            Log.i(TAG, "rom from intent: " + rom);
            currentRom = rom;
            return new String[] { rom };
        }
        return new String[0];
    }

    /** Already running and started again (app icon, or a frontend with a game). */
    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        String rom = intent.getStringExtra("rom");
        if (rom == null || rom.equals(currentRom)) return; /* just brought to the front */
        Log.i(TAG, "asked for another game (" + rom + "): closing this one first");
        switchTo = new Intent(this, LauncherActivity.class).putExtra("rom", rom).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        /* the game saves its resume point and returns from main; SDL then finishes this activity */
        SDLActivity.nativeSendQuit();
    }

    /* ------------------------------------------------------------ orientation */

    /** Another display beside this activity's (the one openSecondScreen uses). */
    private boolean hasSecondDisplay() {
        DisplayManager dm = (DisplayManager) getSystemService(Context.DISPLAY_SERVICE);
        int mine = getWindowManager().getDefaultDisplay().getDisplayId();
        for (Display d : dm.getDisplays()) if (d.getDisplayId() != mine) return true;
        return false;
    }

    /**
     * Two displays (RG DS, AYN Thor, Retroid Pocket Duo...): landscape, as
     * before. One display (a phone, or a Surface Duo, whose two screens are
     * one display; issue #48): whichever way the player holds it, both ways
     * round (a controller's cable can be on either side), keeping the
     * player's rotation lock. The native side lays the screens out from the
     * window's shape (ui.c layout_screens).
     */
    private int orientationWanted() {
        boolean two = hasSecondDisplay();
        String mode = rotationMode();
        int o;
        if (two) o = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE;
        else if (mode.equals("portrait")) o = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT;
        else if (mode.equals("landscape")) o = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE;
        else o = ActivityInfo.SCREEN_ORIENTATION_FULL_USER;
        Log.i(TAG, two ? "two displays: landscape"
                : "one display: " + (mode.equals("auto") ? "any orientation (the rotation lock is kept)" : mode + " (Settings > Rotation)"));
        return o;
    }

    /**
     * Settings > Rotation (issue #60): "auto" (follow the phone, the default),
     * "portrait" or "landscape", kept in rotation.cfg in the app's own storage
     * (read here before the native side starts; the native side writes it, as
     * with data-folder.cfg). Only used with one display.
     */
    private String rotationMode() {
        try (java.io.BufferedReader r = new java.io.BufferedReader(new java.io.FileReader(new File(getFilesDir(), "rotation.cfg")))) {
            String l = r.readLine();
            if (l != null) {
                l = l.trim();
                if (l.equals("portrait") || l.equals("landscape")) return l;
            }
        } catch (java.io.IOException e) {
            /* none: auto */
        }
        return "auto";
    }

    /** Called by the native side when Settings > Rotation changes (the file is written already). */
    public void applyRotation(String mode) {
        runOnUiThread(() -> setRequestedOrientation(orientationWanted()));
    }

    /** SDL asks when it makes the window (its hint: LandscapeLeft, ui.c). */
    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        setRequestedOrientation(orientationWanted());
    }

    private void openSecondScreen() {
        DisplayManager dm = (DisplayManager) getSystemService(Context.DISPLAY_SERVICE);
        int mine = getWindowManager().getDefaultDisplay().getDisplayId();
        Display other = null;
        for (Display d : dm.getDisplays()) {
            Point size = new Point();
            d.getRealSize(size);
            Log.i(TAG, "display " + d.getDisplayId() + " \"" + d.getName() + "\" " + size.x + "x" + size.y
                    + " rotation " + d.getRotation() + " flags 0x" + Integer.toHexString(d.getFlags())
                    + (d.getDisplayId() == mine ? " (game activity)" : ""));
            if (d.getDisplayId() != mine && other == null) other = d;
        }
        if (other == null) {
            Log.i(TAG, "only one display: no second screen");
            return;
        }
        Point size = new Point();
        other.getRealSize(size);
        SecondScreen.nativeSetDisplay(size.x, size.y);
        /* pixels per inch of each (screen_match = size; some devices report
         * made-up values, which the native side leaves out) */
        DisplayMetrics mineDm = new DisplayMetrics(), otherDm = new DisplayMetrics();
        getWindowManager().getDefaultDisplay().getRealMetrics(mineDm);
        other.getRealMetrics(otherDm);
        Log.i(TAG, "display ppi: " + mineDm.xdpi + " x " + mineDm.ydpi + " (game activity), " + otherDm.xdpi + " x " + otherDm.ydpi + " (second)");
        SecondScreen.nativeSetPpi((mineDm.xdpi + mineDm.ydpi) / 2, (otherDm.xdpi + otherDm.ydpi) / 2);
        second = new SecondScreen(this, other);
        try {
            second.show();
            Log.i(TAG, "second screen on display " + other.getDisplayId() + " (" + size.x + "x" + size.y + ")");
        } catch (WindowManager.InvalidDisplayException e) {
            Log.e(TAG, "second screen: " + e);
            SecondScreen.nativeSetDisplay(0, 0);
            second = null;
        }
    }

    /* ------------------------------------------------------------ ROM folder picker */

    private static final int PICK_FOLDER = 4711;

    static native void nativeFolderPicked(String path);

    /* ------------------------------------------------------------ RetroAchievements sign-in */

    /** user/password, or nulls when cancelled. */
    static native void nativeRaCredentials(String user, String password);

    /**
     * Called by the native side (game thread), from the game list's Settings:
     * a dialog for the RetroAchievements account. The password goes to the
     * native side once, which signs in with it and keeps only the token.
     */
    public void raSignInDialog(String user) {
        runOnUiThread(() -> {
            int pad = (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 20, getResources().getDisplayMetrics());
            LinearLayout box = new LinearLayout(this);
            box.setOrientation(LinearLayout.VERTICAL);
            box.setPadding(pad, pad / 2, pad, 0);
            EditText name = new EditText(this);
            name.setHint("Username");
            name.setSingleLine(true);
            name.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
            name.setImeOptions(EditorInfo.IME_ACTION_NEXT);
            if (user != null) name.setText(user);
            EditText pass = new EditText(this);
            pass.setHint("Password");
            pass.setSingleLine(true);
            pass.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
            pass.setImeOptions(EditorInfo.IME_ACTION_DONE);
            box.addView(name);
            box.addView(pass);
            final boolean[] answered = { false };
            AlertDialog d = new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                    .setTitle("RetroAchievements")
                    .setMessage("Sign in with your retroachievements.org account. RetroDS keeps a sign-in token, not your password.")
                    .setView(box)
                    .setPositiveButton("Sign in", (dialog, which) -> {
                        answered[0] = true;
                        nativeRaCredentials(name.getText().toString().trim(), pass.getText().toString());
                    })
                    .setNegativeButton("Cancel", (dialog, which) -> {
                        answered[0] = true;
                        nativeRaCredentials(null, null);
                    })
                    .create();
            d.setOnDismissListener(dialog -> {
                if (!answered[0]) nativeRaCredentials(null, null); /* BACK, or a tap outside */
            });
            d.show();
            (name.getText().length() > 0 ? pass : name).requestFocus();
        });
    }

    /** Called by the native side (game thread): opens Android's folder picker. */
    public void pickRomFolder() {
        runOnUiThread(() -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            try {
                startActivityForResult(i, PICK_FOLDER);
            } catch (ActivityNotFoundException e) {
                Log.e(TAG, "no folder picker: " + e);
                nativeFolderPicked("");
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != PICK_FOLDER) {
            super.onActivityResult(requestCode, resultCode, data);
            return;
        }
        String path = "";
        if (resultCode == RESULT_OK && data != null && data.getData() != null) {
            path = treeUriToPath(data.getData());
            Log.i(TAG, "folder picked: " + data.getData() + " -> " + path);
        }
        nativeFolderPicked(path);
    }

    /** A picked folder (tree URI) as a plain path (readable with All files access). */
    private static String treeUriToPath(Uri uri) {
        try {
            String path = LauncherActivity.documentIdToPath(DocumentsContract.getTreeDocumentId(uri));
            return path != null ? path : "";
        } catch (IllegalArgumentException e) {
            return "";
        }
    }

    /* ------------------------------------------------------------ updates */

    private static final String INSTALL_STATUS = "io.github.iosue_iulianus.retrods.INSTALL_STATUS";

    /** 1 asking to confirm, 2 installed, -1 failed, -2 cancelled, -3 another key, -4 no room, -5 older build (android.h). */
    static native void nativeInstallResult(int state, String message);

    /**
     * Called by the native side (the update's worker thread): hands a
     * downloaded APK to Android's package installer, which then asks the
     * player to confirm (installStatus). Returns "" once it's handed over,
     * else why it couldn't be.
     */
    public String installApk(String path) {
        PackageInstaller pi = getPackageManager().getPackageInstaller();
        PackageInstaller.SessionParams params = new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        params.setAppPackageName(getPackageName());
        int id = -1;
        try {
            id = pi.createSession(params);
            File apk = new File(path);
            try (PackageInstaller.Session s = pi.openSession(id)) {
                try (InputStream in = new FileInputStream(apk); OutputStream out = s.openWrite("retrods.apk", 0, apk.length())) {
                    byte[] buf = new byte[1 << 16];
                    for (int n; (n = in.read(buf)) > 0; ) out.write(buf, 0, n);
                    s.fsync(out);
                }
                Intent i = new Intent(INSTALL_STATUS).setPackage(getPackageName());
                int flags = PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE : 0);
                s.commit(PendingIntent.getBroadcast(this, id, i, flags).getIntentSender());
            }
            Log.i(TAG, "update: " + path + " handed to the package installer (session " + id + ")");
            return "";
        } catch (IOException | RuntimeException e) {
            Log.e(TAG, "update: installer: " + e);
            if (id >= 0) {
                try {
                    pi.abandonSession(id);
                } catch (RuntimeException ignored) {
                }
            }
            String m = e.getMessage();
            return m != null && !m.isEmpty() ? m : e.getClass().getSimpleName();
        }
    }

    private final BroadcastReceiver installStatus = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            int st = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
            String msg = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE);
            Log.i(TAG, "update: install status " + st + (msg != null ? " (" + msg + ")" : ""));
            if (st != PackageInstaller.STATUS_SUCCESS && msg != null && msg.contains("DOWNGRADE")) {
                /* the copy installed is a later build (a test APK built after the release) */
                nativeInstallResult(-5, msg);
                return;
            }
            switch (st) {
            case PackageInstaller.STATUS_PENDING_USER_ACTION: {
                @SuppressWarnings("deprecation")
                Intent confirm = intent.getParcelableExtra(Intent.EXTRA_INTENT);
                if (confirm == null) {
                    nativeInstallResult(-1, "Android's installer didn't ask to confirm");
                    return;
                }
                confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                try {
                    startActivity(confirm);
                    nativeInstallResult(1, null);
                } catch (ActivityNotFoundException | SecurityException e) {
                    nativeInstallResult(-1, "Android's installer couldn't open (" + e.getMessage() + ")");
                }
                return;
            }
            case PackageInstaller.STATUS_SUCCESS:
                nativeInstallResult(2, null);
                return;
            case PackageInstaller.STATUS_FAILURE_ABORTED:
                nativeInstallResult(-2, msg);
                return;
            case PackageInstaller.STATUS_FAILURE_CONFLICT:
            case PackageInstaller.STATUS_FAILURE_INCOMPATIBLE:
                nativeInstallResult(-3, msg);
                return;
            case PackageInstaller.STATUS_FAILURE_STORAGE:
                nativeInstallResult(-4, msg);
                return;
            default:
                nativeInstallResult(-1, msg);
            }
        }
    };

    /* ------------------------------------------------------------ in the background (issue #28) */

    /* Android leaves a Presentation up while its activity is in the
     * background: the second display stayed RetroDS's (and awake) until the
     * app closed. So it's hidden while the app can't be seen and shown
     * again when it can. Hiding takes its surface away (the native side lets
     * go of it: nativeSetSurface(null)) and showing brings a new one, which
     * the native side draws on again (ui.c retries a redraw until the window
     * is there). Dialog.hide() keeps the window, so show() can't fail on a
     * display that's still there. */
    private boolean secondHidden;

    @Override
    protected void onStart() {
        super.onStart();
        if (second != null && secondHidden) {
            secondHidden = false;
            try {
                second.show();
                Log.i(TAG, "second screen shown again (the app is in the foreground)");
            } catch (RuntimeException e) { /* (its display gone meanwhile: Android cancels it) */
                Log.e(TAG, "second screen: " + e);
            }
        }
    }

    @Override
    protected void onStop() {
        super.onStop();
        if (second != null && !secondHidden && !isFinishing()) {
            second.hide();
            secondHidden = true;
            Log.i(TAG, "second screen hidden (the app is in the background)");
        }
    }

    @Override
    protected void onDestroy() {
        try {
            unregisterReceiver(installStatus);
        } catch (IllegalArgumentException ignored) {
        }
        if (second != null) {
            second.dismiss();
            second = null;
        }
        boolean finishing = isFinishing();
        if (running == this) running = null;
        super.onDestroy();
        /* the native side keeps its state in statics: start fresh next time
         * (with the game a frontend asked for, if that's why we closed) */
        if (finishing) {
            if (switchTo != null) startActivity(switchTo);
            System.exit(0);
        }
    }
}
