package io.github.iosue_iulianus.retrods;

import android.app.Presentation;
import android.content.Context;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.Display;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

/**
 * The second display: a full-screen SurfaceView whose Surface the native
 * code draws into (map, logo, menus), and whose touches go to the native
 * pointer queue.
 */
public class SecondScreen extends Presentation implements SurfaceHolder.Callback {
    private static final String TAG = "RETRODS";
    private SurfaceView view;

    static native void nativeSetDisplay(int width, int height);
    /* the displays' pixels per inch: the activity's and this one's */
    static native void nativeSetPpi(float app, float second);
    static native void nativeSetSurface(Surface surface);
    static native void nativeTouch(int kind, float x, float y);

    public SecondScreen(Context context, Display display) {
        super(context, display);
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        Window w = getWindow();
        /* never take key focus: the buttons must keep going to the game */
        w.addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        view = new SurfaceView(getContext());
        view.getHolder().addCallback(this);
        view.setOnTouchListener(this::onTouch);
        setContentView(view);
        hideSystemBars();
    }

    @SuppressWarnings("deprecation")
    private void hideSystemBars() {
        Window w = getWindow();
        if (Build.VERSION.SDK_INT >= 30) {
            w.setDecorFitsSystemWindows(false);
            WindowInsetsController c = w.getInsetsController();
            if (c != null) {
                c.hide(WindowInsets.Type.systemBars());
                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            w.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
    }

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
            nativeTouch(5, e.getX(keep) / w, e.getY(keep) / h);
            return;
        }
        if (e.getPointerCount() < 2) return;
        float x0 = e.getX(0), y0 = e.getY(0), x1 = e.getX(1), y1 = e.getY(1);
        int span = Math.min(0x7FFFFF, Math.round((float) Math.hypot(x1 - x0, y1 - y0) / w * 4096));
        nativeTouch(4 | span << 4, (x0 + x1) / 2 / w, (y0 + y1) / 2 / h);
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
        float x = e.getX() / Math.max(1, v.getWidth()), y = e.getY() / Math.max(1, v.getHeight());
        if (kind != 3) {
            Log.i(TAG, "second screen touch " + (kind == 1 ? "down" : "up") + " at "
                    + (int) e.getX() + "," + (int) e.getY() + " of " + v.getWidth() + "x" + v.getHeight());
        }
        nativeTouch(kind, x, y);
        return true;
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        Log.i(TAG, "second screen surface " + width + "x" + height);
        nativeSetSurface(holder.getSurface());
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        nativeSetSurface(null);
    }
}
