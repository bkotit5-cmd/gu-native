package com.mhgu.recomp;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Date;
import java.util.Locale;

/**
 * Ajustes -> Importar partida: trae el archivo system de un emulador o de otro dispositivo.
 * El juego la tiene en memoria y puede guardar en cualquier momento, así que acá no se toca la partida: el archivo elegido se
 * comprueba, queda en save_import/ y la app se reinicia. GameActivity.prepareFiles lo aplica con applyPending, antes de que
 * arranque el juego: guarda una copia de la partida actual en MHGURecomp/save_backups y reemplaza system y system_backup.
 */
public class SaveImportActivity extends Activity {
    private static final String TAG = "MHGU";
    private static final int PICK = 1;
    private static final int WINE = Color.rgb(38, 18, 14), PANEL = Color.rgb(26, 12, 9), CREAM = Color.rgb(240, 228, 200);
    private static final int GOLD = Color.rgb(226, 182, 92), BRONZE = Color.rgb(62, 38, 22), EDGE = Color.rgb(196, 150, 70);
    private static final int ALERT = Color.rgb(255, 150, 120);
    // (the save's size is fixed; the same mark at +0x14 in saves from the game on a console, yuzu and this port)
    private static final int SIZE = 5159100, MARK_AT = 0x14;
    private static final byte[] MARK = {0x74, (byte) 0xee, (byte) 0xb2, 0x36};

    private boolean es, ready, committed;
    private TextView status;
    private Button main, leave;

    static File home(Context c) { return AppHome.home(c); }
    private static File importDir(Context c) { return new File(home(c), "save_import"); }
    private File tmpFile() { return new File(importDir(this), "system.tmp"); }

    private static void copy(File from, File to) throws IOException {
        try (FileInputStream in = new FileInputStream(from); FileOutputStream out = new FileOutputStream(to)) {
            byte[] buf = new byte[1 << 16];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        }
    }

    /** Before the game starts: the save chosen last time takes the place of the current one, which is kept in a copy. */
    static void applyPending(Context c) {
        File pending = new File(importDir(c), "system");
        if (!pending.exists()) return;
        File saves = SaveFolder.active(c);  // (the save folder's, when one is chosen; the backup stays here)
        File[] now = {new File(saves, "system_backup"), new File(saves, "system")};
        try {
            if (!saves.isDirectory() && !saves.mkdirs()) throw new IOException("no save folder");
            if (now[1].exists()) {
                String stamp = new SimpleDateFormat("yyyy-MM-dd_HH-mm-ss", Locale.US).format(new Date());
                File keep = new File(home(c), "MHGURecomp/save_backups/" + stamp);
                if (!keep.mkdirs()) throw new IOException("no backup folder");
                for (File f : now) if (f.exists()) copy(f, new File(keep, f.getName()));
                Log.i(TAG, "save import: the old save is in " + keep);
            }
            File tmp = new File(saves, "system.import");
            for (File to : now) {
                copy(pending, tmp);
                if (!tmp.renameTo(to) && !(to.delete() && tmp.renameTo(to))) throw new IOException("can't replace " + to);
            }
            if (!pending.delete()) Log.w(TAG, "save import: pending file stays");
            Log.i(TAG, "save import: done");
        } catch (IOException e) {  // (the chosen save stays pending, and the next start tries again)
            Log.e(TAG, "save import: " + e);
        }
    }

    // ---- the screen
    private int dp(int v) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }

    private TextView text(String s, int size, int color, boolean bold) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, size);
        t.setTextColor(color);
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    private GradientDrawable plate(int fill) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(fill);
        d.setStroke(dp(2), EDGE);
        d.setCornerRadius(dp(6));
        return d;
    }

    private Button button(String s, boolean bold) {
        Button b = new Button(this);
        b.setText(s);
        b.setAllCaps(false);
        b.setTextColor(bold ? GOLD : CREAM);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        if (bold) b.setTypeface(Typeface.DEFAULT_BOLD);
        b.setBackground(plate(BRONZE));
        b.setPadding(dp(18), 0, dp(18), 0);
        return b;
    }

    private LinearLayout column(String title, String body, boolean framed) {
        LinearLayout c = new LinearLayout(this);
        c.setOrientation(LinearLayout.VERTICAL);
        if (framed) {
            c.setBackground(plate(PANEL));
            c.setPadding(dp(14), dp(10), dp(14), dp(10));
        } else {
            c.setPadding(dp(18), 0, 0, 0);
        }
        c.addView(text(title, 13, GOLD, true));
        TextView t = text(body, 13, CREAM, false);
        t.setPadding(0, dp(4), 0, 0);
        t.setLineSpacing(dp(1), 1);
        c.addView(t);
        return c;
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        es = "es".equals(Locale.getDefault().getLanguage());
        // Open Android's system document picker immediately; no intermediate import screen.
        openPicker();
    }

    private void openPicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        try {
            startActivityForResult(intent, PICK);
        } catch (ActivityNotFoundException e) {
            Toast.makeText(this, es ? "No se encontró un selector de archivos." : "No file manager is available.", Toast.LENGTH_LONG).show();
            finish();
        }
    }

    @Override
    protected void onActivityResult(int code, int result, Intent data) {
        super.onActivityResult(code, result, data);
        if (code != PICK) return;
        if (result != RESULT_OK || data == null || data.getData() == null) {
            finish();
            return;
        }
        check(data.getData());
    }

    /** Copies the chosen file here (a picked document can only be read through its provider) and looks at it. */
    private void check(Uri uri) {
        new Thread(() -> {
            String wrong = problem(uri);
            runOnUiThread(() -> {
                if (wrong != null) {
                    tmpFile().delete();
                    new AlertDialog.Builder(this)
                            .setTitle(es ? "Archivo no válido" : "Invalid save file")
                            .setMessage(wrong)
                            .setPositiveButton(es ? "Aceptar" : "OK", (dialog, which) -> finish())
                            .setOnCancelListener(dialog -> finish())
                            .show();
                    return;
                }
                new AlertDialog.Builder(this)
                        .setTitle(es ? "¿Importar partida?" : "Import this save?")
                        .setMessage(es
                                ? "Se reemplazarán los personajes de este dispositivo. La partida actual se guardará en una copia de seguridad. El juego se reiniciará para aplicar la nueva partida."
                                : "The hunters on this device will be replaced. Your current save will first be copied to MHGURecomp/save_backups. The game will restart to apply the imported save.")
                        .setPositiveButton(es ? "Importar y reiniciar" : "Import and restart", (dialog, which) -> confirm())
                        .setNegativeButton(es ? "Cancelar" : "Cancel", (dialog, which) -> finish())
                        .setOnCancelListener(dialog -> finish())
                        .show();
            });
        }).start();
    }

    private String problem(Uri uri) {
        File dir = importDir(this);
        if (!dir.isDirectory() && !dir.mkdirs()) return es ? "No se pudo preparar la carpeta de la app." : "The app's folder could not be prepared.";
        byte[] head = new byte[MARK_AT + MARK.length];
        long total = 0;
        try (InputStream in = getContentResolver().openInputStream(uri); FileOutputStream out = new FileOutputStream(tmpFile())) {
            if (in == null) throw new IOException("no stream");
            byte[] buf = new byte[1 << 16];
            int n;
            while ((n = in.read(buf)) > 0) {
                if (total < head.length) System.arraycopy(buf, 0, head, (int) total, Math.min(n, head.length - (int) total));
                total += n;
                if (total > SIZE) break;  // (too big already)
                out.write(buf, 0, n);
            }
        } catch (IOException | SecurityException e) {
            Log.e(TAG, "save import: " + e);
            return es ? "No se pudo leer ese archivo. Cópialo a la memoria del dispositivo y vuelve a intentarlo."
                      : "That file could not be read. Copy it to the device's storage and try again.";
        }
        if (total != SIZE || !Arrays.equals(Arrays.copyOfRange(head, MARK_AT, MARK_AT + MARK.length), MARK))
            return es ? "Ese archivo no es una partida de este juego. Elige el archivo llamado system."
                      : "That file is not a save of this game. Choose the file named system.";
        return null;
    }

    private void confirm() {
        File pending = new File(importDir(this), "system");
        pending.delete();
        if (!tmpFile().renameTo(pending)) {
            Toast.makeText(this, es ? "No se pudo preparar la partida; no se cambió nada." : "The save could not be prepared; nothing was changed.",
                    Toast.LENGTH_LONG).show();
            finish();
            return;
        }
        committed = true;
        Intent intent = new Intent(this, PhoenixActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        intent.putExtra(PhoenixActivity.EXTRA_PID, Process.myPid());
        startActivity(intent);
        // The game is paused behind this activity; terminate this process so applyPending runs before the next launch.
        new Handler(Looper.getMainLooper()).postDelayed(() -> Process.killProcess(Process.myPid()), 200);
    }

    @Override
    protected void onDestroy() {
        if (!committed) tmpFile().delete();
        super.onDestroy();
    }
}
