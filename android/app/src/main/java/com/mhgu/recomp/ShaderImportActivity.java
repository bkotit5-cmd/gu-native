package com.mhgu.recomp;

import android.app.Activity;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.util.Log;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.Locale;

/**
 * Imports a user GLSL file through Android's system document picker. There is intentionally no custom
 * orange import/management screen: shader selection and removal stay in Settings -> Shader in-game.
 */
public final class ShaderImportActivity extends Activity {
    private static final String TAG = "MHGU";
    private static final int PICK_SHADER = 1;
    private static final long MAX_SHADER_BYTES = 2L << 20;

    private static native void nativeShaderFilesChanged();

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        pick.addCategory(Intent.CATEGORY_OPENABLE);
        pick.setType("*/*");
        try {
            startActivityForResult(pick, PICK_SHADER);
        } catch (Exception e) {
            Log.e(TAG, "shader picker: " + e);
            Toast.makeText(this, "No file picker is available on this device.", Toast.LENGTH_LONG).show();
            finish();
        }
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_SHADER) return;
        if (result == RESULT_OK && data != null && data.getData() != null) {
            try {
                String name = displayName(data.getData());
                String lower = name.toLowerCase(Locale.ROOT);
                if (!lower.endsWith(".glsl")) throw new Exception("Choose a .glsl file.");
                name = safeName(name);
                if (!name.toLowerCase(Locale.ROOT).endsWith(".glsl")) throw new Exception("Choose a .glsl file.");
                copyShader(data.getData(), name);
                nativeShaderFilesChanged();  // refresh after the atomic file install succeeds
                Toast.makeText(this, "Imported " + name + ". Select it in Settings → Shader.", Toast.LENGTH_LONG).show();
            } catch (Exception e) {
                Log.e(TAG, "shader import: " + e);
                Toast.makeText(this, "Shader import failed: " + e.getMessage(), Toast.LENGTH_LONG).show();
            }
        }
        finish();
    }

    private String displayName(Uri uri) throws Exception {
        try (Cursor c = getContentResolver().query(uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                int column = c.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (column >= 0) {
                    String name = c.getString(column);
                    if (name != null && !name.trim().isEmpty()) return name.trim();
                }
            }
        }
        String tail = uri.getLastPathSegment();
        if (tail == null || tail.trim().isEmpty()) throw new Exception("The selected file has no filename.");
        return tail.trim();
    }

    private static String safeName(String input) {
        String name = input.replace('\\', '_').replace('/', '_')
                .replaceAll("[^A-Za-z0-9 ._()\\[\\]-]+", "_").trim();
        while (name.contains("..")) name = name.replace("..", ".");
        if (name.isEmpty() || name.startsWith(".") || name.startsWith("_")) name = "shader_" + name;
        return name;
    }

    private void copyShader(Uri uri, String name) throws Exception {
        File dir = new File(AppHome.home(this), "shaders");
        if (!dir.isDirectory() && !dir.mkdirs()) throw new Exception("Cannot create the shaders folder.");
        File target = new File(dir, name);
        File tmp = new File(dir, name + ".importing");
        File backup = new File(dir, name + ".previous");
        if (tmp.exists() && !tmp.delete()) throw new Exception("Cannot clear the temporary import file.");

        long total = 0;
        try (InputStream in = getContentResolver().openInputStream(uri);
             FileOutputStream out = new FileOutputStream(tmp)) {
            if (in == null) throw new Exception("Cannot read the selected document.");
            byte[] buffer = new byte[32 * 1024];
            for (int n; (n = in.read(buffer)) != -1; ) {
                if (n == 0) continue;
                total += n;
                if (total > MAX_SHADER_BYTES) throw new Exception("GLSL files must be no larger than 2 MB.");
                out.write(buffer, 0, n);
            }
            out.flush();
            out.getFD().sync();
        } catch (Exception e) {
            tmp.delete();
            throw e;
        }
        if (total == 0) {
            tmp.delete();
            throw new Exception("The selected GLSL file is empty.");
        }

        if (backup.exists() && !backup.delete()) {
            tmp.delete();
            throw new Exception("Cannot clear the previous-file backup.");
        }
        boolean hadTarget = target.exists();
        if (hadTarget && !target.renameTo(backup)) {
            tmp.delete();
            throw new Exception("Cannot replace the existing shader.");
        }
        if (!tmp.renameTo(target)) {
            if (hadTarget) backup.renameTo(target);
            tmp.delete();
            throw new Exception("Cannot install the shader file.");
        }
        if (backup.exists()) backup.delete();
        Log.i(TAG, "shader imported through system picker: " + target);
    }
}
