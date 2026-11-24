package com.mhgu.recomp;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.ProgressDialog;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.os.Process;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.util.Log;
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Settings -> Cheats / Mods: a no-screen import trampoline.
 * Android's system document picker selects files/folders; staging, validation and installation stay in this activity.
 * A native confirmation dialog is shown only after a successful import.
 */
public class ImportActivity extends Activity {
    public static final String KIND = "kind";
    public static final int CHEATS = 0, MOD = 1, DLC = 2;
    private static final String TAG = "MHGU";
    private static final int PICK_FILE = 41, PICK_TREE = 42;
    private static final Pattern TITLE_ID = Pattern.compile("(?i)\\b(0100770008dd[9a-f][0-9a-f]{3})\\b");

    private int kind;
    private boolean es, busy;
    private ProgressDialog progressDialog;

    private File home() { return AppHome.home(this); }
    private String sub() { return kind == CHEATS ? "cheats" : kind == MOD ? "mods" : "dlc"; }

    /** The game's folder (GameFolderActivity's choice), or null. */
    private File gameFolder() {
        try (FileInputStream in = new FileInputStream(new File(home(), "game_folder.txt"))) {
            byte[] b = new byte[4096];
            int n = in.read(b);
            String path = n > 0 ? new String(b, 0, n, "UTF-8").trim() : "";
            return path.isEmpty() ? null : new File(path);
        } catch (Exception e) {
            return null;
        }
    }

    /**
     * Where they go: the game folder's cheats/, mods/ or dlc/ (the game reads them there, and a player can put them
     * there by hand with any file manager); the app's own folder if the game folder can't be written (an SD card).
     */
    private File target() {
        File game = gameFolder();
        if (game != null) {
            File in = new File(game, sub());
            if ((in.isDirectory() || in.mkdirs()) && in.canWrite()) return in;
        }
        return new File(home(), sub());
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        es = "es".equals(Locale.getDefault().getLanguage());
        kind = getIntent().getIntExtra(KIND, CHEATS);
        if (kind < CHEATS || kind > DLC) kind = CHEATS;
        // No intermediate import page: launch Android's file/folder picker immediately.
        chooseImportSource();
    }

    private void say(String message, boolean bad) {
        Toast.makeText(this, message, Toast.LENGTH_LONG).show();
        if (bad) Log.w(TAG, message);
    }

    private void askRestart(String imported) {
        new AlertDialog.Builder(this)
                .setTitle(es ? "Importación completada" : "Import complete")
                .setMessage((es ? "Importado: " : "Imported: ") + imported
                        + (es ? ". Reinicia el juego para aplicar los cambios." : ". Restart the game to apply the changes."))
                .setPositiveButton(es ? "Reiniciar ahora" : "Restart now", (dialog, which) -> restart())
                .setNegativeButton(es ? "Más tarde" : "Later", (dialog, which) -> finish())
                .setOnCancelListener(dialog -> finish())
                .show();
    }

    private void restart() {
        Intent intent = new Intent(this, PhoenixActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        intent.putExtra(PhoenixActivity.EXTRA_PID, Process.myPid());
        startActivity(intent);
        finishAffinity();
    }

    private static void delete(File f) {
        File[] kids = f.listFiles();
        if (kids != null) for (File k : kids) delete(k);
        f.delete();
    }

    // ---- Android's system picker: files for cheats/ZIPs, document trees for mod/DLC folders
    private void chooseImportSource() {
        if (busy) return;
        if (kind == CHEATS) {
            openFilePicker(new String[] {"text/plain"});
        } else if (kind == DLC) {
            openTreePicker();
        } else {
            String[] choices = es ? new String[] {"Archivo ZIP (.zip)", "Carpeta del mod"}
                                  : new String[] {"ZIP file (.zip)", "Mod folder"};
            new AlertDialog.Builder(this)
                    .setTitle(es ? "Agregar un mod" : "Add a mod")
                    .setItems(choices, (dialog, which) -> {
                        if (which == 0)
                            openFilePicker(new String[] {"application/zip", "application/x-zip-compressed", "application/octet-stream"});
                        else
                            openTreePicker();
                    })
                    .setNegativeButton(es ? "Cancelar" : "Cancel", (dialog, which) -> finish())
                    .setOnCancelListener(dialog -> finish())
                    .show();
        }
    }

    private void openFilePicker(String[] mimeTypes) {
        Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        pick.addCategory(Intent.CATEGORY_OPENABLE);
        pick.setType("*/*");
        if (mimeTypes != null && mimeTypes.length > 0)
            pick.putExtra(Intent.EXTRA_MIME_TYPES, mimeTypes);
        pick.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        try {
            startActivityForResult(pick, PICK_FILE);
        } catch (ActivityNotFoundException e) {
            Log.e(TAG, "import file picker: " + e);
            say(es ? "Este dispositivo no tiene un selector de archivos." : "No file picker is available on this device.", true);
            finish();
        }
    }

    private void openTreePicker() {
        Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        pick.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        try {
            startActivityForResult(pick, PICK_TREE);
        } catch (ActivityNotFoundException e) {
            Log.e(TAG, "import folder picker: " + e);
            say(es ? "Este dispositivo no tiene un selector de carpetas." : "No folder picker is available on this device.", true);
            finish();
        }
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_FILE && request != PICK_TREE) return;
        if (result != RESULT_OK || data == null || data.getData() == null) {
            finish();
            return;
        }

        Uri uri = data.getData();
        if (request == PICK_FILE) {
            String name = displayName(uri, false);
            String lower = name.toLowerCase(Locale.ROOT);
            if (kind == CHEATS && !lower.endsWith(".txt")) {
                showError(es ? "Elige un archivo de trucos .txt." : "Choose a .txt cheat file.");
                return;
            }
            if (kind == MOD && !lower.endsWith(".zip")) {
                showError(es ? "Elige un archivo de mod .zip." : "Choose a .zip mod file.");
                return;
            }
            startImportFromPicker(uri, false, name);
        } else {
            startImportFromPicker(uri, true, displayName(uri, true));
        }
    }

    private void showError(String message) {
        new AlertDialog.Builder(this)
                .setTitle(es ? "No se pudo importar" : "Import failed")
                .setMessage(message)
                .setPositiveButton(es ? "Aceptar" : "OK", (dialog, which) -> finish())
                .setOnCancelListener(dialog -> finish())
                .show();
    }

    private String displayName(Uri uri, boolean tree) {
        Uri queryUri = uri;
        if (tree) {
            String id = DocumentsContract.getTreeDocumentId(uri);
            queryUri = DocumentsContract.buildDocumentUriUsingTree(uri, id);
        }
        try (Cursor c = getContentResolver().query(queryUri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                int at = c.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (at >= 0) {
                    String name = c.getString(at);
                    if (name != null && !name.trim().isEmpty()) return name.trim();
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "import picker: display name query failed: " + e);
        }
        String fallback = tree ? DocumentsContract.getTreeDocumentId(uri) : uri.getLastPathSegment();
        if (fallback == null || fallback.trim().isEmpty()) return tree ? "Selected folder" : "selected_file";
        int slash = fallback.lastIndexOf('/');
        if (slash >= 0 && slash + 1 < fallback.length()) fallback = fallback.substring(slash + 1);
        int colon = fallback.lastIndexOf(':');
        if (colon >= 0 && colon + 1 < fallback.length()) fallback = fallback.substring(colon + 1);
        return fallback;
    }

    private long documentSize(Uri uri) {
        try (Cursor c = getContentResolver().query(uri, new String[] {OpenableColumns.SIZE}, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                int at = c.getColumnIndex(OpenableColumns.SIZE);
                if (at >= 0 && !c.isNull(at)) return Math.max(0, c.getLong(at));
            }
        } catch (Exception e) {
            Log.w(TAG, "import picker: size query failed: " + e);
        }
        return 0;
    }

    private static String stageName(String name) {
        String safe = name == null ? "" : name.replace('/', '_').replace('\\', '_').trim();
        if (safe.isEmpty() || safe.equals(".") || safe.equals("..")) return "selected";
        return safe;
    }

    private void startImportFromPicker(Uri uri, boolean tree, String selectedName) {
        if (busy) return;
        busy = true;
        copied = 0;
        total = 0;
        lastTick = 0;
        lastProgressLog = 0;

        progressDialog = new ProgressDialog(this);
        progressDialog.setMessage(es ? "Importando contenido…" : "Importing content…");
        progressDialog.setIndeterminate(true);
        progressDialog.setCancelable(false);
        progressDialog.show();

        new Thread(() -> {
            File staging = new File(getCacheDir(), "import-staging-" + System.nanoTime());
            String done = null, error = null;
            try {
                if (!staging.mkdirs()) throw new IOException("cannot create temporary import folder");
                File source;
                if (tree) {
                    String documentId = DocumentsContract.getTreeDocumentId(uri);
                    source = new File(staging, stageName(selectedName));
                    int[] entries = {0};
                    copyDocumentFolder(uri, documentId, source, 0, entries);
                } else {
                    source = new File(staging, stageName(selectedName));
                    total = documentSize(uri);
                    try (InputStream in = getContentResolver().openInputStream(uri);
                         OutputStream out = new FileOutputStream(source)) {
                        if (in == null) throw new IOException("cannot open the selected document");
                        copyStream(in, out);
                        out.flush();
                    }
                    if (source.length() <= 0) throw new IOException("the selected file is empty");
                }

                copied = 0;
                total = 0;
                lastTick = 0;
                lastProgressLog = 0;
                done = kind == CHEATS ? importCheats(source) : kind == MOD ? importMod(source) : importDlc(source);
            } catch (Exception e) {
                Log.e(TAG, "system-picker import: " + e);
                error = e.getMessage() != null ? e.getMessage() : e.getClass().getSimpleName();
            } finally {
                delete(staging);
            }

            final String ok = done, why = error;
            runOnUiThread(() -> {
                busy = false;
                if (progressDialog != null && progressDialog.isShowing()) progressDialog.dismiss();
                progressDialog = null;
                if (ok != null) {
                    askRestart(ok);
                } else {
                    showError((es ? "No se pudo: " : "Import failed: ")
                            + (why != null ? why : "unknown error"));
                }
            });
        }).start();
    }

    private void copyDocumentFolder(Uri treeUri, String documentId, File destination, int depth, int[] entries) throws IOException {
        if (depth > 32) throw new IOException("selected folder is nested too deeply");
        if (!destination.isDirectory() && !destination.mkdirs())
            throw new IOException("cannot create staging folder " + destination.getName());

        Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, documentId);
        String[] columns = {
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE
        };
        try (Cursor c = getContentResolver().query(childrenUri, columns, null, null, null)) {
            if (c == null) throw new IOException("the selected folder cannot be listed by this document provider");
            int idColumn = c.getColumnIndex(DocumentsContract.Document.COLUMN_DOCUMENT_ID);
            int nameColumn = c.getColumnIndex(DocumentsContract.Document.COLUMN_DISPLAY_NAME);
            int typeColumn = c.getColumnIndex(DocumentsContract.Document.COLUMN_MIME_TYPE);
            if (idColumn < 0 || nameColumn < 0 || typeColumn < 0)
                throw new IOException("the document provider returned incomplete folder information");

            while (c.moveToNext()) {
                if (++entries[0] > 100000) throw new IOException("selected folder contains too many entries");
                String childId = c.getString(idColumn);
                String childName = stageName(c.getString(nameColumn));
                String mime = c.getString(typeColumn);
                File child = new File(destination, childName);
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    copyDocumentFolder(treeUri, childId, child, depth + 1, entries);
                } else {
                    Uri childUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, childId);
                    File parent = child.getParentFile();
                    if (parent != null && !parent.isDirectory() && !parent.mkdirs())
                        throw new IOException("cannot create folder " + parent.getName());
                    try (InputStream in = getContentResolver().openInputStream(childUri);
                         OutputStream out = new FileOutputStream(child)) {
                        if (in == null) throw new IOException("cannot read " + childName);
                        copyStream(in, out);
                        out.flush();
                    }
                }
            }
        } catch (SecurityException e) {
            throw new IOException("permission to read the selected folder was denied", e);
        }
    }

    // ---- the copy (a background thread)
    private long copied, total;

    private long lastProgressLog;

    private void progress() {
        long now = System.currentTimeMillis();
        if (now - lastProgressLog < 2000) return;
        lastProgressLog = now;
        long c = copied, t = total;
        Log.d(TAG, "import progress: " + (t > 0 ? (c * 100 / t) + "%" : (c >> 20) + " MiB"));
    }

    private void copyFile(File from, File to) throws IOException {
        to.getParentFile().mkdirs();
        File tmp = new File(to.getPath() + ".part");
        try (InputStream in = new FileInputStream(from); OutputStream out = new FileOutputStream(tmp)) {
            copyStream(in, out);
        }
        to.delete();
        if (!tmp.renameTo(to)) throw new IOException("can't write " + to.getName());
    }

    private long lastTick;
    private void copyStream(InputStream in, OutputStream out) throws IOException {
        byte[] buf = new byte[1 << 16];
        for (int n; (n = in.read(buf)) > 0; ) {
            out.write(buf, 0, n);
            copied += n;
            long now = System.currentTimeMillis();
            if (now - lastTick > 400) { lastTick = now; progress(); }
        }
    }

    private static long sizeOf(File f) {
        if (f.isFile()) return f.length();
        long s = 0;
        File[] kids = f.listFiles();
        if (kids != null) for (File k : kids) s += sizeOf(k);
        return s;
    }

    private void copyTree(File from, File to) throws IOException {
        if (from.isFile()) { copyFile(from, to); return; }
        if (!to.isDirectory() && !to.mkdirs()) throw new IOException("can't make " + to.getName());
        File[] kids = from.listFiles();
        if (kids != null) for (File k : kids) copyTree(k, new File(to, k.getName()));
    }

    private static String safeName(String s) {
        String n = s.replaceAll("[^A-Za-z0-9 ._()\\[\\]-]+", "_").trim();
        return n.isEmpty() || n.startsWith(".") || n.startsWith("_") ? "x" + n : n;
    }

    private String importCheats(File txt) throws IOException {
        String name = safeName(txt.getName());
        if (!name.toLowerCase(Locale.ROOT).endsWith(".txt")) name += ".txt";
        total = txt.length();
        copyFile(txt, new File(target(), name));
        // (each one starts off, to be turned on on the Cheats page: the game's list of those off, cheats/_off.txt in the
        // app's folder, gets "<file>\t<name>" for each [name] of the file; {master} codes always run)
        StringBuilder off = new StringBuilder();
        try (java.io.BufferedReader r = new java.io.BufferedReader(new java.io.InputStreamReader(new FileInputStream(txt), "UTF-8"))) {
            for (String l; (l = r.readLine()) != null; ) {
                l = l.trim();
                if (l.length() > 2 && l.startsWith("[") && l.endsWith("]")) off.append(name).append('\t').append(l, 1, l.length() - 1).append('\n');
            }
        }
        File list = new File(home(), "cheats/_off.txt");
        list.getParentFile().mkdirs();
        try (OutputStream o = new FileOutputStream(list, true)) {
            o.write(("\n" + off).getBytes("UTF-8"));
        }
        return name;
    }

    private static boolean hasModParts(File d, int depth) {
        if (depth > 3 || !d.isDirectory()) return false;
        File[] kids = d.listFiles(File::isDirectory);
        if (kids == null) return false;
        for (File k : kids) {
            String n = k.getName().toLowerCase(Locale.ROOT);
            // (cheats/: yuzu's and Ryujinx's layout of a mod's cheats, <mod>/cheats/<build id>.txt: runtime/cheats.c reads them)
            if (n.equals("romfs") || n.equals("exefs") || n.equals("nativenx") || n.equals("cheats") || hasModParts(k, depth + 1))
                return true;
        }
        return false;
    }

    private String importMod(File from) throws IOException {
        String base = from.getName();
        if (from.isFile()) {  // a .zip: unpacked into mods/<its name>/
            String name = safeName(base.replaceAll("(?i)\\.zip$", ""));
            File to = new File(target(), name), tmp = new File(target(), ".new");
            delete(tmp);
            total = from.length();
            String canon = tmp.getCanonicalPath() + File.separator;
            try (ZipInputStream z = new ZipInputStream(new FileInputStream(from))) {
                for (ZipEntry e; (e = z.getNextEntry()) != null; ) {
                    File out = new File(tmp, e.getName());
                    if (!out.getCanonicalPath().startsWith(canon)) continue;  // (nothing outside the folder)
                    if (e.isDirectory()) { out.mkdirs(); continue; }
                    out.getParentFile().mkdirs();
                    try (OutputStream o = new FileOutputStream(out)) { copyStream(z, o); }
                }
            }
            if (!hasModParts(tmp, 0)) { delete(tmp); throw new IOException(es ? "el .zip no trae romfs ni exefs" : "the .zip has no romfs or exefs"); }
            delete(to);
            if (!tmp.renameTo(to)) throw new IOException("can't write " + name);
            return name;
        }
        if (!hasModParts(from, 0))
            throw new IOException(es ? "esa carpeta no tiene romfs ni exefs adentro" : "that folder has no romfs or exefs in it");
        String name = safeName(base);
        File to = new File(target(), name), tmp = new File(target(), ".new");
        delete(tmp);
        total = sizeOf(from);
        copyTree(from, tmp);
        delete(to);
        if (!tmp.renameTo(to)) throw new IOException("can't write " + name);
        return name;
    }

    private static void findDlc(File d, int depth, List<File> out) {
        if (depth > 6 || out.size() >= 256) return;
        File[] kids = d.listFiles();
        if (kids == null) return;
        for (File k : kids)
            if (k.isFile() && k.getName().equalsIgnoreCase("DLC.dlcp")) out.add(d);
        for (File k : kids)
            if (k.isDirectory() && !k.getName().startsWith(".")) findDlc(k, depth + 1, out);
    }

    private String importDlc(File from) throws IOException {
        File start = from.isFile() ? from.getParentFile() : from;
        List<File> found = new ArrayList<>();
        findDlc(start, 0, found);
        if (found.isEmpty()) throw new IOException(es ? "no hay ningún DLC.dlcp en esa carpeta" : "there is no DLC.dlcp in that folder");
        total = 0;
        for (File d : found) total += sizeOf(d);
        int n = 0, next = 1;
        target().mkdirs();
        for (File d : found) {
            String id = null;  // (the DLC's title id, if its path says it: the index the game knows it by)
            for (File p = d; p != null && id == null; p = p.getParentFile()) {
                Matcher m = TITLE_ID.matcher(p.getName());
                if (m.find()) id = m.group(1).toUpperCase(Locale.ROOT);
            }
            String name = id;
            if (name == null) {
                while (new File(target(), String.format(Locale.ROOT, "%03d", next)).exists()) next++;
                name = String.format(Locale.ROOT, "%03d", next++);
            }
            File to = new File(target(), name);
            delete(to);
            copyTree(d, to);
            n++;
        }
        return n + (es ? (n == 1 ? " DLC copiado" : " DLC copiados") : " DLC copied");
    }
}
