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
import java.io.RandomAccessFile;

/**
 * Imports the player's own Lossless.dll from their Steam Lossless Scaling installation.
 *
 * The Windows DLL is never loaded or executed by Android. It is stored privately so the native
 * Lossless Scaling backend can read its PE resources and extract the frame-generation shaders.
 */
public final class LosslessDllImportActivity extends Activity {
    private static final String TAG = "MHGU";
    private static final int PICK_DLL = 1;
    private static final long MAX_DLL_BYTES = 128L << 20;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        pick.addCategory(Intent.CATEGORY_OPENABLE);
        pick.setType("*/*");
        try {
            startActivityForResult(pick, PICK_DLL);
        } catch (Exception e) {
            Log.e(TAG, "Lossless.dll picker: " + e);
            Toast.makeText(this, "No file picker is available on this device.", Toast.LENGTH_LONG).show();
            finish();
        }
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_DLL) return;
        if (result == RESULT_OK && data != null && data.getData() != null) {
            try {
                String name = displayName(data.getData());
                if (!"Lossless.dll".equalsIgnoreCase(name))
                    throw new Exception("Choose Lossless.dll from the Steam Lossless Scaling installation.");
                copyDll(data.getData());
                GameActivity.nativeLosslessDllChanged(new File(AppHome.home(this), "lossless/Lossless.dll").getAbsolutePath());
                Toast.makeText(this, "Lossless.dll imported. The app will validate its shaders when Lossless FG starts.",
                        Toast.LENGTH_LONG).show();
            } catch (Exception e) {
                Log.e(TAG, "Lossless.dll import: " + e);
                Toast.makeText(this, "Lossless.dll import failed: " + e.getMessage(), Toast.LENGTH_LONG).show();
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

    private void copyDll(Uri uri) throws Exception {
        File dir = new File(AppHome.home(this), "lossless");
        if (!dir.isDirectory() && !dir.mkdirs()) throw new Exception("Cannot create the Lossless folder.");
        File target = new File(dir, "Lossless.dll");
        File tmp = new File(dir, "Lossless.dll.importing");
        File backup = new File(dir, "Lossless.dll.previous");
        if (tmp.exists() && !tmp.delete()) throw new Exception("Cannot clear the temporary import file.");

        long total = 0;
        try (InputStream in = getContentResolver().openInputStream(uri);
             FileOutputStream out = new FileOutputStream(tmp)) {
            if (in == null) throw new Exception("Cannot read the selected document.");
            byte[] buffer = new byte[64 * 1024];
            for (int n; (n = in.read(buffer)) != -1; ) {
                if (n == 0) continue;
                total += n;
                if (total > MAX_DLL_BYTES) throw new Exception("Lossless.dll exceeds the 128 MB import limit.");
                out.write(buffer, 0, n);
            }
            out.flush();
            out.getFD().sync();
        } catch (Exception e) {
            tmp.delete();
            throw e;
        }
        if (total < 0x100) {
            tmp.delete();
            throw new Exception("The selected file is empty or too small to be a Windows DLL.");
        }

        try {
            validatePe64(tmp);
        } catch (Exception e) {
            tmp.delete();
            throw e;
        }

        if (backup.exists() && !backup.delete()) {
            tmp.delete();
            throw new Exception("Cannot clear the previous DLL backup.");
        }
        boolean hadTarget = target.exists();
        if (hadTarget && !target.renameTo(backup)) {
            tmp.delete();
            throw new Exception("Cannot replace the existing Lossless.dll.");
        }
        if (!tmp.renameTo(target)) {
            if (hadTarget) backup.renameTo(target);
            tmp.delete();
            throw new Exception("Cannot install Lossless.dll.");
        }
        if (backup.exists()) backup.delete();
        Log.i(TAG, "Lossless.dll imported to private app storage: " + target);
    }

    private static int readU16LE(RandomAccessFile file) throws Exception {
        int a = file.readUnsignedByte();
        int b = file.readUnsignedByte();
        return a | (b << 8);
    }

    private static long readU32LE(RandomAccessFile file) throws Exception {
        long a = file.readUnsignedByte();
        long b = file.readUnsignedByte();
        long c = file.readUnsignedByte();
        long d = file.readUnsignedByte();
        return a | (b << 8) | (c << 16) | (d << 24);
    }

    /** Lossless Scaling ships a 64-bit Windows PE DLL; Android only parses its resources later. */
    private static void validatePe64(File file) throws Exception {
        try (RandomAccessFile f = new RandomAccessFile(file, "r")) {
            if (f.readUnsignedByte() != 0x4d || f.readUnsignedByte() != 0x5a)
                throw new Exception("This is not a Windows PE file (missing MZ header).");
            f.seek(0x3c);
            long pe = readU32LE(f);
            if (pe < 0x40 || pe > file.length() - 26)
                throw new Exception("The Windows PE header is invalid or truncated.");
            f.seek(pe);
            if (readU32LE(f) != 0x00004550L)
                throw new Exception("This file has no valid PE signature.");
            int machine = readU16LE(f);
            f.seek(pe + 24);
            int optionalMagic = readU16LE(f);
            if (machine != 0x8664 || optionalMagic != 0x020b)
                throw new Exception("Choose the 64-bit x86-64 Lossless.dll from Steam.");
        }
    }
}
